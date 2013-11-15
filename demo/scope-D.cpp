/*
 * Copyright (C) 2013 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Michi Henning <michi.henning@canonical.com>
 */

#include <scopes/RegistryProxyFwd.h>
#include <scopes/Reply.h>
#include <scopes/Category.h>
#include <scopes/ResultItem.h>
#include <scopes/ScopeBase.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <list>
#include <mutex>

#include <unistd.h>

#define EXPORT __attribute__ ((visibility ("default")))

using namespace std;
using namespace unity::api::scopes;

// Simple queue that stores query-string/reply pairs, using MyQuery* as a key for removal.
// The put() method adds a pair at the tail, and the get() method returns a pair at the head.
// get() suspends the caller until an item is available or until the queue is told to finish.
// get() returns true if it returns a pair, false if the queue was told to finish.
// remove() searches for the entry with the given key and erases it.

class MyQuery;

class Queue
{
public:
    void put(MyQuery const* query, string const& query_string, ReplyProxy const& reply_proxy)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queries_.push_back(QueryData { query, query_string, reply_proxy });
        condvar_.notify_one();
    }

    bool get(string& query_string, ReplyProxy& reply_proxy)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condvar_.wait(lock, [this] { return !queries_.empty() || done_; });
        if (done_)
        {
            condvar_.notify_all();
        }
        else
        {
            auto qd = queries_.front();
            queries_.pop_front();
            query_string = qd.query_string;
            reply_proxy = qd.reply_proxy;
        }
        return !done_;
    }

    void remove(MyQuery const* query)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        QueryData qd { query, "", nullptr };
        auto it = std::find(queries_.begin(), queries_.end(), qd);
        if (it != queries_.end())
        {
            cerr << "Queue: removed query: " << it->query_string << endl;
            queries_.erase(it);
        }
        else
        {
            cerr << "Queue: did not find entry to be removed" << endl;
        }
    }

    void finish()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        queries_.clear();
        done_ = true;
        condvar_.notify_all();
    }

    Queue()
        : done_(false)
    {
    }

private:
    struct QueryData
    {
        MyQuery const* query;
        string query_string;
        ReplyProxy reply_proxy;

        bool operator==(QueryData const& rhs) const
        {
            return query == rhs.query;
        }
    };

    std::list<QueryData> queries_;
    bool done_;
    std::mutex mutex_;
    std::condition_variable condvar_;
};

class MyQuery : public QueryBase
{
public:
    MyQuery(string const& scope_name,
            string const& query,
            Queue& queue) :
        scope_name_(scope_name),
        query_(query),
        queue_(queue)
    {
        cerr << "query instance for \"" << scope_name_ << ":" << query << "\" created" << endl;
    }

    virtual ~MyQuery() noexcept
    {
        cerr << "query instance for \"" << scope_name_ << ":" << query_ << "\" destroyed" << endl;
    }

    virtual void cancelled() override
    {
        // Informational callback to let a query know when it was cancelled. The query should
        // clean up any resources it has allocated, stop pushing results, and arrange for
        // run() to return (if still active).
        // Ignoring query cancelltion does not do any direct harm, but wastes CPU cycles: any
        // results that are pushed once a query is cancelled are ignored anyway.
        // The main purpose of this callback to give queries a chance to stop doing whatever
        // work may still be in progress on a query. Note that cancellations are frequent;
        // not responding to cancelled() correctly causes loss of performance.

        cerr << "query for \"" << scope_name_ << ":" << query_ << "\" cancelled" << endl;
    }

    virtual void run(ReplyProxy const& reply) override
    {
        // The query can do anything it likes with this method, that is, run() can push results
        // directly on the provided reply, or it can save the reply for later use and return from
        // run(). It is OK to push results on the reply from a different thread.
        // The only obligation on run() is that, if cancelled() is called, and run() is still active
        // at that time, run() must tidy up and return in a timely fashion.
        queue_.put(this, query_, reply);
    }

private:
    string scope_name_;
    string query_;
    Queue& queue_;
};

// Example scope D: replies asynchronously to queries.
// The scope's run() method is used as a worker thread that pulls queries from a queue.
// The MyQuery object's run() method adds the query string and the reply proxy to the queue
// and signals the worker thread, and then returns. The worker thread pushes the results.

class MyScope : public ScopeBase
{
public:
    virtual int start(string const& scope_name, RegistryProxy const&) override
    {
        scope_name_ = scope_name;
        return VERSION;
    }

    virtual void stop() override
    {
        queue_.finish();
        done_.store(true);
    }

    virtual void run() override
    {
        // What run() does is up to the scope. For example, we could set up and run an event loop here.
        // It's OK for run() to be empty and return immediately, or to take as long it likes to complete.
        // The only obligation is that, if the scopes run time calls stop(), run() must tidy up and return
        // in as timely a manner as possible.
        while (!done_.load())
        {
            string query;
            ReplyProxy reply_proxy;
            if (queue_.get(query, reply_proxy) && !done_.load())
            {
                for (int i = 1; i < 5; ++i)
                {
                    auto cat = reply_proxy->register_category("cat1", "Category 1", "", "{}");
                    ResultItem result(cat);
                    result.set_uri("uri");
                    result.set_title(scope_name_ + ": result " + to_string(i) + " for query \"" + query + "\"");
                    result.set_icon("icon");
                    result.set_dnd_uri("dnd_uri");
                    if (!reply_proxy->push(result))
                    {
                        break; // Query was cancelled
                    }
                    sleep(1);
                }
                cerr << scope_name_ << ": query \"" << query << "\" complete" << endl;
            }
        }
    }

    virtual QueryBase::UPtr create_query(string const& q, VariantMap const&) override
    {
        QueryBase::UPtr query(new MyQuery(scope_name_, q, queue_));
        cerr << scope_name_ << ": created query: \"" << q << "\"" << endl;
        return query;
    }

    MyScope()
        : done_(false)
    {
    }

private:
    string scope_name_;
    Queue queue_;
    std::atomic_bool done_;
};

// External entry points to allocate and deallocate the scope.

extern "C"
{

    EXPORT
    unity::api::scopes::ScopeBase*
    // cppcheck-suppress unusedFunction
    UNITY_API_SCOPE_CREATE_FUNCTION()
    {
        return new MyScope;
    }

    EXPORT
    void
    // cppcheck-suppress unusedFunction
    UNITY_API_SCOPE_DESTROY_FUNCTION(unity::api::scopes::ScopeBase* scope_base)
    {
        delete scope_base;
    }

}
