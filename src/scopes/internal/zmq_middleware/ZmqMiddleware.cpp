/*
 * Copyright (C) 2013 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Michi Henning <michi.henning@canonical.com>
 */

#include <unity/scopes/internal/zmq_middleware/ZmqMiddleware.h>

#include <unity/scopes/internal/RuntimeImpl.h>
#include <unity/scopes/internal/RegistryImpl.h>
#include <unity/scopes/internal/ScopeImpl.h>
#include <unity/scopes/internal/zmq_middleware/ConnectionPool.h>
#include <unity/scopes/internal/zmq_middleware/ObjectAdapter.h>
#include <unity/scopes/internal/zmq_middleware/QueryI.h>
#include <unity/scopes/internal/zmq_middleware/QueryCtrlI.h>
#include <unity/scopes/internal/zmq_middleware/RegistryI.h>
#include <unity/scopes/internal/zmq_middleware/ReplyI.h>
#include <unity/scopes/internal/zmq_middleware/ScopeI.h>
#include <unity/scopes/internal/zmq_middleware/StateReceiverI.h>
#include <unity/scopes/internal/zmq_middleware/ZmqQuery.h>
#include <unity/scopes/internal/zmq_middleware/ZmqQueryCtrl.h>
#include <unity/scopes/internal/zmq_middleware/ZmqRegistry.h>
#include <unity/scopes/internal/zmq_middleware/ZmqReply.h>
#include <unity/scopes/internal/zmq_middleware/ZmqScope.h>
#include <unity/scopes/internal/zmq_middleware/ZmqStateReceiver.h>
#include <unity/scopes/internal/zmq_middleware/RethrowException.h>
#include <unity/scopes/ScopeExceptions.h>
#include <unity/UnityExceptions.h>

#include <iostream>  // TODO: remove this once logging is added
#include <sys/stat.h>

using namespace std;

namespace unity
{

namespace scopes
{

namespace internal
{

namespace zmq_middleware
{

namespace
{

char const* query_suffix = "-q";  // Appended to server_name_ to create query adapter name
char const* ctrl_suffix = "-c";   // Appended to server_name_ to create control adapter name
char const* reply_suffix = "-r";  // Appended to server_name_ to create reply adapter name
char const* state_suffix = "-s";  // Appended to server_name_ to create state adapter name

char const* query_category = "Query";       // query adapter category name
char const* ctrl_category = "QueryCtrl";    // control adapter category name
char const* reply_category = "Reply";       // reply adapter category name
char const* state_category = "State";       // state adapter category name
char const* scope_category = "Scope";       // scope adapter category name
char const* registry_category = "Registry"; // registry adapter category name

// Create a directory with mode rwx------t if it doesn't exist yet.
// We set the sticky bit to prevent the directory from being deleted:
// if it happens to be under $XDG_RUNTIME_DIR and is not accessed
// for more than six hours, the system may delete it.

void create_dir(string const& dir)
{
    if (mkdir(dir.c_str(), 0700 | S_ISVTX) == -1 && errno != EEXIST)
    {
        throw FileException("cannot create endpoint directory " + dir, errno);
    }
}

} // namespace

ZmqMiddleware::ZmqMiddleware(string const& server_name, RuntimeImpl* runtime, string const& configfile)
try :
    MiddlewareBase(runtime),
    server_name_(server_name),
    state_(Stopped),
    config_(configfile),
    twoway_timeout_(300),  // TODO: get timeout from config
    locate_timeout_(2000)  // TODO: get timeout from config
{
    assert(!server_name.empty());

    // Create the endpoint dirs if they don't exist.
    try
    {
        create_dir(config_.public_dir());
        create_dir(config_.private_dir());
    }
    catch (...)
    {
        throw MiddlewareException("cannot initialize zmq middleware for scope " + server_name);
    }
}
catch (zmqpp::exception const& e)
{
    rethrow_zmq_ex(e);
}

ZmqMiddleware::~ZmqMiddleware()
{
    try
    {
        stop();
        wait_for_shutdown();
    }
    catch (std::exception const& e)
    {
        // TODO: log exception
    }
    catch (...)
    {
        // TODO: log exception
    }
}

void ZmqMiddleware::start()
{
    unique_lock<mutex> lock(state_mutex_);

    switch (state_)
    {
        case Started:
        case Starting:
        {
            return; // Already started, or about to start, no-op
        }
        case Stopped:
        {
            {
                lock_guard<mutex> lock(data_mutex_);
                oneway_invoker_.reset(new ThreadPool(1));  // Oneway pool must have a single thread
                // N.B. We absolutely MUST have AT LEAST 2 two-way invoke threads
                // as rebinding is invoked within two-way invocations.
                twoway_invokers_.reset(new ThreadPool(2));  // TODO: get pool size from config
            }
            state_ = Started;
            state_changed_.notify_all();
            break;
        }
        default:
        {
            assert(false);  // LCOV_EXCL_LINE
        }
    }
}

void ZmqMiddleware::stop()
{
    unique_lock<mutex> lock(state_mutex_);
    switch (state_)
    {
        case Stopped:
        {
            break;  // Already stopped, or about to stop, no-op
        }
        case Starting:
        {
            // Wait until start in progress has completed before stopping
            // Coverage excluded here because the window for which we are in this state is too
            // small to hit with a test.
            state_changed_.wait(lock, [this] { return state_ == Started; }); // LCOV_EXCL_LINE
            // FALLTHROUGH
        }
        case Started:
        {
            {
                lock_guard<mutex> lock(data_mutex_);
                // No more outgoing invocations
                twoway_invokers_.reset();
                oneway_invoker_.reset();
            }
            auto adapter_map = move(am_);
            for (auto& pair : adapter_map)
            {
                pair.second->shutdown();
            }
            state_ = Stopped;
            state_changed_.notify_all();
            lock.unlock();
            for (auto& pair : adapter_map)
            {
                pair.second->wait_for_shutdown();
            }
            break;
        }
        default:
        {
            assert(false);  // LCOV_EXCL_LINE
        }
    }
}

void ZmqMiddleware::wait_for_shutdown()
{
    unique_lock<mutex> lock(state_mutex_);
    state_changed_.wait(lock, [this] { return state_ == Stopped; }); // LCOV_EXCL_LINE
}

namespace
{

void bad_proxy_string(string const& msg)
{
    throw MiddlewareException("string_to_proxy(): " + msg);
}

}

// Poor man's URI parser (no boost)

ObjectProxy ZmqMiddleware::string_to_proxy(string const& s)
{
    if (s == "nullproxy:")
    {
        return nullptr;
    }
    if (s.empty())
    {
        bad_proxy_string("proxy string cannot be empty");
    }
    static const string scheme = "ipc://";
    if (s.substr(0, scheme.size()) != scheme)
    {
        bad_proxy_string("invalid proxy scheme prefix: \"" + s + "\" (expected \"" + scheme + "\")");
    }
    auto fragment_pos = s.find_first_of('#');
    if (fragment_pos == string::npos)
    {
        bad_proxy_string("invalid proxy: missing # separator: " + s);
    }

    string endpoint(s.begin(), s.begin() + fragment_pos);  // Everything up to the '#'
    if (endpoint.size() == scheme.size())
    {
        bad_proxy_string("invalid proxy: empty endpoint path: " + s);
    }

    string fields(s.substr(fragment_pos + 1));      // Everything following the '#'

    auto excl_pos = fields.find_first_of('!');
    auto end_it = excl_pos == string::npos ? fields.end() : fields.begin() + excl_pos;

    string identity(fields.begin(), end_it);
    if (identity.empty())
    {
        bad_proxy_string("invalid proxy: empty identity: " + s);
    }

    fields = fields.substr(identity.size());

    // Remaining fields are optional. Field assignments are separated by '!'
    // Collect field assignments in fvals.
    vector<string> fvals;
    while (!fields.empty())
    {
        if (fields[0] == '!')
        {
            fields = fields.substr(1);
        }
        excl_pos = fields.find_first_of('!');
        end_it = excl_pos == string::npos ? fields.end() : fields.begin() + excl_pos;
        string val(fields.begin(), end_it);
        if (val.empty())
        {
            bad_proxy_string("invalid proxy: invalid empty field specification: " + s);
        }
        fvals.push_back(val);
        fields = fields.substr(val.size());
    }

    // fvals now contains field assignments. Insert each assignment into a map, so we
    // check for duplicate fields.
    map<char, string> fmap;
    const string valid_fields = "cmt";
    for (auto const& v : fvals)
    {
        if (v.size() < 2 || v[1] != '=')
        {
            bad_proxy_string("invalid proxy: bad field specification (\"" + v + "\"): " + s);
        }
        if (valid_fields.find(v[0]) == string::npos)
        {
            bad_proxy_string("invalid proxy: invalid field identifier (\"" + v + "\"): " + s);
        }
        if (fmap.find(v[0]) != fmap.end())
        {
            bad_proxy_string("invalid proxy: duplicate field specification (\"" + string(1, v[0]) + "=\"): " + s);
        }
        fmap[v[0]] = string(v.begin() + 2, v.end());
    }

    // Now run over the map and check each value
    string category = scope_category;
    RequestMode mode = RequestMode::Twoway;
    int64_t timeout = -1;
    for (auto const& pair : fmap)
    {
        switch (pair.first)
        {
            case 'c':
            {
                category = pair.second;  // Empty category is OK
                break;
            }
            case 'm':
            {
                if (pair.second.empty() || (pair.second != "o" && pair.second != "t"))
                {
                    bad_proxy_string("invalid proxy: bad mode (\"m=" + pair.second + "\"): " + s);
                }
                mode = pair.second == "o" ? RequestMode::Oneway : RequestMode::Twoway;
                break;
            }
            case 't':
            {
                if (pair.second.empty())
                {
                    bad_proxy_string("invalid proxy: bad timeout value (\"t=" + pair.second + "\"): " + s);
                }
                size_t pos;
                try
                {
                    timeout = std::stol(pair.second, &pos);
                }
                catch (std::exception const&)
                {
                    pos = 0;
                }
                if (pair.second[pos] != '\0')  // Did not consume all of the assignment
                {
                    bad_proxy_string("invalid proxy: bad timeout value (\"t=" + pair.second + "\"): " + s);
                }
                if (timeout < -1)
                {
                    bad_proxy_string("invalid proxy: bad timeout value (\"t=" + pair.second + "\"): " + s);
                }
                break;
            }
            default:
            {
                assert(false);  // LCOV_EXCL_LINE
            }
        }
    }

    return make_typed_proxy(endpoint, identity, category, mode, timeout);
}

string ZmqMiddleware::proxy_to_string(MWProxy const& proxy)
{
    if (!proxy)
    {
        return "nullproxy:";
    }
    return proxy->to_string();
}

MWRegistryProxy ZmqMiddleware::registry_proxy()
{
    lock_guard<mutex> lock(data_mutex_);

    if (!registry_proxy_)
    {
        string r_id = runtime()->registry_identity();  // May be empty, if no registry is configured.
        if (!r_id.empty())
        {
            string r_endp = "ipc://" + config_.public_dir() + "/" + r_id;
            registry_proxy_.reset(new ZmqRegistry(this, r_endp, r_id, registry_category, twoway_timeout_));
        }
    }
    return registry_proxy_;
}

MWRegistryProxy ZmqMiddleware::ss_registry_proxy()
{
    lock_guard<mutex> lock(data_mutex_);

    if (!ss_registry_proxy_)
    {
        string ssr_id = runtime()->ss_registry_identity();  // May be empty, if no registry is configured.
        if (!ssr_id.empty())
        {
            string ssr_endp = "ipc://" + config_.public_dir() + "/" + ssr_id;
            ss_registry_proxy_.reset(new ZmqRegistry(this, ssr_endp, ssr_id, registry_category, twoway_timeout_));
        }
    }
    return ss_registry_proxy_;
}

MWScopeProxy ZmqMiddleware::create_scope_proxy(string const& identity)
{
    string endpoint = "ipc://" + config_.private_dir() + "/" + identity;
    return make_shared<ZmqScope>(this, endpoint, identity, scope_category, twoway_timeout_);
}

MWScopeProxy ZmqMiddleware::create_scope_proxy(string const& identity, string const& endpoint)
{
    return make_shared<ZmqScope>(this, endpoint, identity, scope_category, twoway_timeout_);
}

MWQueryProxy ZmqMiddleware::create_query_proxy(string const& identity, string const& endpoint)
{
    return make_shared<ZmqQuery>(this, endpoint, identity, query_category);
}

MWQueryCtrlProxy ZmqMiddleware::create_query_ctrl_proxy(string const& identity, string const& endpoint)
{
    return make_shared<ZmqQueryCtrl>(this, endpoint, identity, ctrl_category);
}

MWStateReceiverProxy ZmqMiddleware::create_state_receiver_proxy(std::string const& identity)
{
    string endpoint = "ipc://" + config_.private_dir() + "/" + server_name_ + state_suffix;
    return make_shared<ZmqStateReceiver>(this, endpoint, identity, state_category);
}

MWQueryCtrlProxy ZmqMiddleware::add_query_ctrl_object(QueryCtrlObjectBase::SPtr const& ctrl)
{
    assert(ctrl);

    MWQueryCtrlProxy proxy;
    try
    {
        shared_ptr<QueryCtrlI> qci(make_shared<QueryCtrlI>(ctrl));
        auto adapter = find_adapter(server_name_ + ctrl_suffix, config_.private_dir(), ctrl_category);
        function<void()> df;
        auto p = safe_add(df, adapter, "", qci);
        ctrl->set_disconnect_function(df);
        proxy = ZmqQueryCtrlProxy(new ZmqQueryCtrl(this, p->endpoint(), p->identity(), ctrl_category));
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_query_ctrl_object(): " << e.what() << endl;
        throw;
    }
    return proxy;
}

void ZmqMiddleware::add_dflt_query_ctrl_object(QueryCtrlObjectBase::SPtr const& ctrl)
{
    assert(ctrl);

    try
    {
        shared_ptr<QueryCtrlI> qci(make_shared<QueryCtrlI>(ctrl));
        auto adapter = find_adapter(server_name_ + ctrl_suffix, config_.private_dir(), ctrl_category);
        auto df = safe_dflt_add(adapter, ctrl_category, qci);
        ctrl->set_disconnect_function(df);
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_dflt_query_ctrl_object(): " << e.what() << endl;
        throw;
    }
}

MWQueryProxy ZmqMiddleware::add_query_object(QueryObjectBase::SPtr const& query)
{
    assert(query);

    MWQueryProxy proxy;
    try
    {
        shared_ptr<QueryI> qi(make_shared<QueryI>(query));
        auto adapter = find_adapter(server_name_ + query_suffix, config_.private_dir(), query_category);
        function<void()> df;
        auto p = safe_add(df, adapter, "", qi);
        query->set_disconnect_function(df);
        proxy = ZmqQueryProxy(new ZmqQuery(this, p->endpoint(), p->identity(), query_category));
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_query_object(): " << e.what() << endl;
        throw;
    }
    return proxy;
}

void ZmqMiddleware::add_dflt_query_object(QueryObjectBase::SPtr const& query)
{
    assert(query);

    try
    {
        shared_ptr<QueryI> qi(make_shared<QueryI>(query));
        auto adapter = find_adapter(server_name_ + query_suffix, config_.private_dir(), query_category);
        auto df = safe_dflt_add(adapter, query_category, qi);
        query->set_disconnect_function(df);
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_dflt_query_object(): " << e.what() << endl;
        throw;
    }
}

MWRegistryProxy ZmqMiddleware::add_registry_object(string const& identity, RegistryObjectBase::SPtr const& registry)
{
    assert(!identity.empty());
    assert(registry);

    MWRegistryProxy proxy;
    try
    {
        shared_ptr<RegistryI> ri(make_shared<RegistryI>(registry));
        auto adapter = find_adapter(server_name_, config_.public_dir(), registry_category);
        function<void()> df;
        auto p = safe_add(df, adapter, identity, ri);
        registry->set_disconnect_function(df);
        proxy = ZmqRegistryProxy(new ZmqRegistry(this, p->endpoint(), p->identity(), registry_category, twoway_timeout_));
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_registry_object(): " << e.what() << endl;
        throw;
    }
    return proxy;
}

MWReplyProxy ZmqMiddleware::add_reply_object(ReplyObjectBase::SPtr const& reply)
{
    assert(reply);

    MWReplyProxy proxy;
    try
    {
        shared_ptr<ReplyI> ri(make_shared<ReplyI>(reply));
        auto adapter = find_adapter(server_name_ + reply_suffix, config_.public_dir(), reply_category);
        function<void()> df;
        auto p = safe_add(df, adapter, "", ri);
        reply->set_disconnect_function(df);
        proxy = ZmqReplyProxy(new ZmqReply(this, p->endpoint(), p->identity(), reply_category));
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_reply_object(): " << e.what() << endl;
        throw;
    }
    return proxy;
}

MWScopeProxy ZmqMiddleware::add_scope_object(string const& identity, ScopeObjectBase::SPtr const& scope, int64_t idle_timeout)
{
    assert(!identity.empty());
    assert(scope);

    MWScopeProxy proxy;
    try
    {
        shared_ptr<ScopeI> si(make_shared<ScopeI>(scope));
        auto adapter = find_adapter(server_name_, config_.private_dir(), scope_category, idle_timeout);
        function<void()> df;
        auto p = safe_add(df, adapter, identity, si);
        scope->set_disconnect_function(df);
        proxy = ZmqScopeProxy(new ZmqScope(this, p->endpoint(), p->identity(), scope_category, twoway_timeout_));
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_scope_object(): " << e.what() << endl;
        throw;
    }
    return proxy;
}

void ZmqMiddleware::add_dflt_scope_object(ScopeObjectBase::SPtr const& scope)
{
    assert(scope);

    try
    {
        shared_ptr<ScopeI> si(make_shared<ScopeI>(scope));
        auto adapter = find_adapter(server_name_, config_.private_dir(), scope_category);
        auto df = safe_dflt_add(adapter, scope_category, si);
        scope->set_disconnect_function(df);
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_dflt_scope_object(): " << e.what() << endl;
        throw;
    }
}

MWStateReceiverProxy ZmqMiddleware::add_state_receiver_object(std::string const& identity, StateReceiverObject::SPtr const& state_receiver)
{
    assert(!identity.empty());
    assert(state_receiver);

    MWStateReceiverProxy proxy;
    try
    {
        shared_ptr<StateReceiverI> sri(make_shared<StateReceiverI>(state_receiver));
        auto adapter = find_adapter(server_name_ + state_suffix, config_.private_dir(), state_category);
        function<void()> df;
        auto p = safe_add(df, adapter, identity, sri);
        state_receiver->set_disconnect_function(df);
        proxy = ZmqStateReceiverProxy(new ZmqStateReceiver(this, p->endpoint(), p->identity(), state_category));
    }
    catch (std::exception const& e) // Should never happen unless our implementation is broken
    {
        // TODO: log this
        cerr << "unexpected exception in add_state_receiver_object(): " << e.what() << endl;
        throw;
    }
    return proxy;
}

std::string ZmqMiddleware::get_scope_endpoint()
{
    return "ipc://" + config_.private_dir() + "/" +  server_name_;
}

std::string ZmqMiddleware::get_query_endpoint()
{
    return "inproc://" + server_name_ + query_suffix;
}

std::string ZmqMiddleware::get_query_ctrl_endpoint()
{
    return "ipc://" + config_.private_dir() + "/" +  server_name_ + ctrl_suffix;
}

zmqpp::context* ZmqMiddleware::context() const noexcept
{
    return const_cast<zmqpp::context*>(&context_);
}

ThreadPool* ZmqMiddleware::oneway_pool()
{
    lock(state_mutex_, data_mutex_);
    unique_lock<mutex> state_lock(state_mutex_, std::adopt_lock);
    lock_guard<mutex> invokers_lock(data_mutex_, std::adopt_lock);
    if (state_ == Starting)
    {
        state_changed_.wait(state_lock, [this] { return state_ != Starting; }); // LCOV_EXCL_LINE
    }
    if (state_ == Stopped)
    {
        throw MiddlewareException("Cannot invoke operations while middleware is stopped");
    }
    return oneway_invoker_.get();
}

ThreadPool* ZmqMiddleware::twoway_pool()
{
    lock(state_mutex_, data_mutex_);
    unique_lock<mutex> state_lock(state_mutex_, std::adopt_lock);
    lock_guard<mutex> invokers_lock(data_mutex_, std::adopt_lock);
    if (state_ == Starting)
    {
        state_changed_.wait(state_lock, [this] { return state_ != Starting; }); // LCOV_EXCL_LINE
    }
    if (state_ == Stopped)
    {
        throw MiddlewareException("Cannot invoke operations while middleware is stopped");
    }
    return twoway_invokers_.get();
}

int64_t ZmqMiddleware::locate_timeout() const noexcept
{
    return locate_timeout_;
}

ObjectProxy ZmqMiddleware::make_typed_proxy(string const& endpoint,
                                            string const& identity,
                                            string const& category,
                                            RequestMode mode,
                                            int64_t timeout)
{
    // For the time being we only support Scope and Registry types for proxy creation,
    // both of which are twoway interfaces.
    if (mode != RequestMode::Twoway)
    {
        throw MiddlewareException("make_typed_proxy(): cannot create oneway proxies");
    }
    if (category == scope_category)
    {
        auto p = make_shared<ZmqScope>(this, endpoint, identity, category, timeout);
        return ScopeImpl::create(p, runtime(), identity);
    }
    else if (category == registry_category)
    {
        auto p = make_shared<ZmqRegistry>(this, endpoint, identity, category, timeout);
        return make_shared<RegistryImpl>(p, runtime());
    }
    else
    {
        throw MiddlewareException("make_typed_proxy(): unknown category: " + category);
    }
}

shared_ptr<ObjectAdapter> ZmqMiddleware::find_adapter(string const& name, string const& endpoint_dir,
                                                      string const& category, int64_t idle_timeout)
{
    lock_guard<mutex> lock(data_mutex_);

    auto it = am_.find(name);
    if (it != am_.end())
    {
        return it->second;
    }

    // We don't have the requested adapter yet, so we create it on the fly.
    int pool_size;
    RequestMode mode;
    if (category == query_category)
    {
        // The query adapter is single or multi-threaded and supports oneway operations only.
        // TODO: get pool size from config
        pool_size = 1;
        mode = RequestMode::Oneway;
    }
    else if (category == ctrl_category)
    {
        // The ctrl adapter is single-threaded and supports oneway operations only.
        pool_size = 1;
        mode = RequestMode::Oneway;
    }
    else if (category == reply_category)
    {
        // The reply adapter is single- or multi-threaded and supports oneway operations only.
        // TODO: get pool size from config
        pool_size = 1;
        mode = RequestMode::Oneway;
    }
    else if (category == state_category)
    {
        // The state adapter is single- or multi-threaded and supports oneway operations only.
        // TODO: get pool size from config
        pool_size = 1;
        mode = RequestMode::Oneway;
    }
    else if (category == scope_category)
    {
        // The scope adapter is single- or multi-threaded and supports twoway operations only.
        // TODO: get pool size from config
        pool_size = 1;
        mode = RequestMode::Twoway;
    }
    else if (category == registry_category)
    {
        // The registry adapter is multi-threaded and supports twoway operations only.
        // TODO: get pool size from config
        // NB: On rebind, locate() is called on this adapter. A scope may then call registry methods during
        // its start() method, hence we must ensure this adapter has enough threads available to handle this.
        pool_size = 6;
        mode = RequestMode::Twoway;
    }
    else
    {
        // The normal adapter is single- or multi-threaded and supports twoway operations only.
        // TODO: get pool size from config
        pool_size = 1;
        mode = RequestMode::Twoway;
    }

    // The query adapter is always inproc.
    string endpoint;
    if (category == query_category)
    {
        endpoint = "inproc://" + name;
    }
    else
    {
        endpoint = "ipc://" + endpoint_dir + "/" + name;
    }

    shared_ptr<ObjectAdapter> a(new ObjectAdapter(*this, name, endpoint, mode, pool_size));
    a->activate(idle_timeout);

    am_[name] = a;
    return a;
}

ZmqProxy ZmqMiddleware::safe_add(function<void()>& disconnect_func,
                                 shared_ptr<ObjectAdapter> const& adapter,
                                 string const& identity,
                                 shared_ptr<ServantBase> const& servant)
{
    string id = identity.empty() ? unique_id_.gen() : identity;

    disconnect_func = [adapter, id]
    {
        try
        {
            adapter->remove(id);
        }
        catch (...)
        {
            // No error here; for concurrent invocations that each disconnect the servant,
            // only one of them will succeed.
        }
    };
    return adapter->add(id, servant);
}

function<void()> ZmqMiddleware::safe_dflt_add(shared_ptr<ObjectAdapter> const& adapter,
                                              string const& category,
                                              shared_ptr<ServantBase> const& servant)
{
    function<void()> disconnect_func = [adapter, category]
    {
        try
        {
            adapter->remove_dflt_servant(category);
        }
        catch (...)
        {
            // No error here; for concurrent invocations that each disconnect the servant,
            // only one of them will succeed.
        }
    };
    adapter->add_dflt_servant(category, servant);
    return disconnect_func;
}

} // namespace zmq_middleware

} // namespace internal

} // namespace scopes

} // namespace unity
