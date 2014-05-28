/*
 * Copyright (C) 2014 Canonical Ltd
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
 * Authored by: Pawel Stolowski <pawel.stolowski@canonical.com>
 */

#include <unity/scopes/Runtime.h>
#include <unity/scopes/Registry.h>
#include <unity/scopes/SearchMetadata.h>
#include <unity/scopes/SearchListenerBase.h>
#include <unity/scopes/CategorisedResult.h>
#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <signal.h>
#include <thread>
#include <unistd.h>

using namespace boost;
using namespace unity::scopes;

class Receiver : public SearchListenerBase
{
public:
    Receiver()
        : done_(false)
        , finished_ok_(false)
    {
    }

    virtual void push(CategorisedResult /* result */) override
    {
    }

    virtual void finished(ListenerBase::Reason reason, std::string const& error_message ) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        EXPECT_EQ(Finished, reason);
        EXPECT_EQ("", error_message);
        finished_ok_ = reason == Finished;
        done_ = true;
        cond_.notify_all();
    }

    bool wait_until_finished()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto expiry_time = now + std::chrono::seconds(5);
        EXPECT_TRUE(cond_.wait_until(lock, expiry_time, [this]{ return done_; })) << "finished message not delivered";
        done_ = false;
        return finished_ok_;
    }

private:
    bool done_;
    bool finished_ok_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

TEST(Registry, metadata)
{
    Runtime::UPtr rt = Runtime::create(TEST_RUNTIME_FILE);
    RegistryProxy r = rt->registry();

    auto meta = r->get_metadata("testscopeA");
    EXPECT_EQ("testscopeA", meta.scope_id());
    EXPECT_EQ("Canonical Ltd.", meta.author());
    EXPECT_EQ("scope-A.DisplayName", meta.display_name());
    EXPECT_EQ("scope-A.Description", meta.description());
    EXPECT_EQ("/foo/scope-A.Art", meta.art());
    EXPECT_EQ("/foo/scope-A.Icon", meta.icon());
    EXPECT_EQ("scope-A.HotKey", meta.hot_key());
    EXPECT_EQ("scope-A.SearchHint", meta.search_hint());
    EXPECT_EQ(TEST_RUNTIME_PATH "/scopes/testscopeA", meta.scope_directory());

    const char *bart = TEST_RUNTIME_PATH "/scopes/testscopeB/data/scope-B.Art";
    const char *bicon = TEST_RUNTIME_PATH "/scopes/testscopeB/data/scope-B.Icon";

    meta = r->get_metadata("testscopeB");
    EXPECT_EQ("testscopeB", meta.scope_id());
    EXPECT_EQ("Canonical Ltd.", meta.author());
    EXPECT_EQ("scope-B.DisplayName", meta.display_name());
    EXPECT_EQ("scope-B.Description", meta.description());
    EXPECT_EQ(bart, meta.art());
    EXPECT_EQ(bicon, meta.icon());
    EXPECT_EQ("scope-B.HotKey", meta.hot_key());
    EXPECT_EQ("scope-B.SearchHint", meta.search_hint());
    EXPECT_EQ(TEST_RUNTIME_PATH "/scopes/testscopeB", meta.scope_directory());
}

TEST(Registry, scope_state_notify)
{
    bool update_received = false;
    bool testscopeA_state = false;
    bool testscopeB_state = false;
    std::mutex mutex;
    std::condition_variable cond;

    Runtime::UPtr rt = Runtime::create(TEST_RUNTIME_FILE);
    RegistryProxy r = rt->registry();

    // Configure testscopeA scope_state_callback
    auto connA = r->set_scope_state_callback("testscopeA", [&update_received, &testscopeA_state, &mutex, &cond](bool is_running)
    {
        std::lock_guard<std::mutex> lock(mutex);
        update_received = true;
        testscopeA_state = is_running;
        cond.notify_one();
    });
    // Configure testscopeB scope_state_callback
    auto connB = r->set_scope_state_callback("testscopeB", [&update_received, &testscopeB_state, &mutex, &cond](bool is_running)
    {
        std::lock_guard<std::mutex> lock(mutex);
        update_received = true;
        testscopeB_state = is_running;
        cond.notify_one();
    });
    auto wait_for_state_update = [&update_received, &mutex, &cond]
    {
        // Wait for an update notification
        std::unique_lock<std::mutex> lock(mutex);
        bool success = cond.wait_for(lock, std::chrono::milliseconds(500), [&update_received] { return update_received; });
        update_received = false;
        return success;
    };

    auto receiver = std::make_shared<Receiver>();
    SearchListenerBase::SPtr reply(receiver);
    SearchMetadata metadata("C", "desktop");

    auto meta = r->get_metadata("testscopeA");
    auto sp = meta.proxy();

    // testscopeA should not be running at this point
    EXPECT_FALSE(r->is_scope_running("testscopeA"));
    EXPECT_FALSE(wait_for_state_update());

    // search would fail if testscopeA can't be executed
    auto ctrl = sp->search("foo", metadata, reply);
    EXPECT_TRUE(receiver->wait_until_finished());

    // testscopeA should now be running
    EXPECT_TRUE(wait_for_state_update());
    EXPECT_TRUE(testscopeA_state);
    EXPECT_TRUE(r->is_scope_running("testscopeA"));

    meta = r->get_metadata("testscopeB");
    sp = meta.proxy();

    // testscopeB should not be running at this point
    EXPECT_FALSE(r->is_scope_running("testscopeB"));
    EXPECT_FALSE(wait_for_state_update());

    // search would fail if testscopeB can't be executed
    ctrl = sp->search("foo", metadata, reply);
    EXPECT_TRUE(receiver->wait_until_finished());

    // testscopeB should now be running
    EXPECT_TRUE(wait_for_state_update());
    EXPECT_TRUE(testscopeB_state);
    EXPECT_TRUE(r->is_scope_running("testscopeB"));
}

TEST(Registry, list_update_notify)
{
    bool update_received = false;
    std::mutex mutex;
    std::condition_variable cond;

    Runtime::UPtr rt = Runtime::create(TEST_RUNTIME_FILE);
    RegistryProxy r = rt->registry();

    // Configure registry update callback
    auto conn = r->set_list_update_callback([&update_received, &mutex, &cond]
    {
        std::lock_guard<std::mutex> lock(mutex);
        update_received = true;
        cond.notify_one();
    });
    auto wait_for_update = [&update_received, &mutex, &cond]
    {
        // Flush out update notifications
        std::unique_lock<std::mutex> lock(mutex);
        bool success = false;
        while (cond.wait_for(lock, std::chrono::milliseconds(500), [&update_received] { return update_received; }))
        {
            success = true;
            update_received = false;
        }
        update_received = false;
        return success;
    };

    system::error_code ec;

    // First check that we have 2 scopes registered
    MetadataMap list = r->list();
    EXPECT_EQ(2, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_EQ(list.end(), list.find("testscopeC"));
    EXPECT_EQ(list.end(), list.find("testscopeD"));

    // Move testscopeC into the scopes folder
    std::cout << "Move testscopeC into the scopes folder" << std::endl;
    filesystem::rename(TEST_RUNTIME_PATH "/other_scopes/testscopeC", TEST_RUNTIME_PATH "/scopes/testscopeC", ec);
    ASSERT_EQ("Success", ec.message());
    EXPECT_TRUE(wait_for_update());

    // Now check that we have 3 scopes registered
    list = r->list();
    EXPECT_EQ(3, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_NE(list.end(), list.find("testscopeC"));
    EXPECT_EQ(list.end(), list.find("testscopeD"));

    // Make a symlink to testscopeD in the scopes folder
    std::cout << "Make a symlink to testscopeD in the scopes folder" << std::endl;
    filesystem::create_symlink(TEST_RUNTIME_PATH "/other_scopes/testscopeD", TEST_RUNTIME_PATH "/scopes/testscopeD", ec);
    ASSERT_EQ("Success", ec.message());
    EXPECT_TRUE(wait_for_update());

    // Now check that we have 4 scopes registered
    list = r->list();
    EXPECT_EQ(4, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_NE(list.end(), list.find("testscopeC"));
    EXPECT_NE(list.end(), list.find("testscopeD"));

    // Move testscopeC back into the other_scopes folder
    std::cout << "Move testscopeC back into the other_scopes folder" << std::endl;
    filesystem::rename(TEST_RUNTIME_PATH "/scopes/testscopeC", TEST_RUNTIME_PATH "/other_scopes/testscopeC", ec);
    ASSERT_EQ("Success", ec.message());
    EXPECT_TRUE(wait_for_update());

    // Now check that we have 3 scopes registered again
    list = r->list();
    EXPECT_EQ(3, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_EQ(list.end(), list.find("testscopeC"));
    EXPECT_NE(list.end(), list.find("testscopeD"));

    // Remove symlink to testscopeD from the scopes folder
    std::cout << "Remove symlink to testscopeD from the scopes folder" << std::endl;
    filesystem::remove(TEST_RUNTIME_PATH "/scopes/testscopeD", ec);
    ASSERT_EQ("Success", ec.message());
    EXPECT_TRUE(wait_for_update());

    // Now check that we are back to having 2 scopes registered
    list = r->list();
    EXPECT_EQ(2, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_EQ(list.end(), list.find("testscopeC"));
    EXPECT_EQ(list.end(), list.find("testscopeD"));

    // Make a folder in scopes named "testfolder"
    std::cout << "Make a folder in scopes named \"testfolder\"" << std::endl;
    filesystem::create_directory(TEST_RUNTIME_PATH "/scopes/testfolder", ec);
    ASSERT_EQ("Success", ec.message());
    EXPECT_FALSE(wait_for_update());

    // Check that no scopes were registered
    list = r->list();
    EXPECT_EQ(2, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_EQ(list.end(), list.find("testscopeC"));
    EXPECT_EQ(list.end(), list.find("testscopeD"));

    // Make a symlink to testscopeC.ini in testfolder
    std::cout << "Make a symlink to testscopeC.ini in testfolder" << std::endl;
    filesystem::create_symlink(TEST_RUNTIME_PATH "/other_scopes/testscopeC/testscopeC.ini", TEST_RUNTIME_PATH "/scopes/testfolder/testscopeC.ini", ec);
    ASSERT_EQ("Success", ec.message());
    EXPECT_TRUE(wait_for_update());

    // Now check that we have 3 scopes registered
    list = r->list();
    EXPECT_EQ(3, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_NE(list.end(), list.find("testscopeC"));
    EXPECT_EQ(list.end(), list.find("testscopeD"));

    // Remove testfolder
    std::cout << "Remove testfolder" << std::endl;
    filesystem::remove_all(TEST_RUNTIME_PATH "/scopes/testfolder", ec);
    ASSERT_EQ("Success", ec.message());
    EXPECT_TRUE(wait_for_update());

    // Now check that we are back to having 2 scopes registered
    list = r->list();
    EXPECT_EQ(2, list.size());
    EXPECT_NE(list.end(), list.find("testscopeA"));
    EXPECT_NE(list.end(), list.find("testscopeB"));
    EXPECT_EQ(list.end(), list.find("testscopeC"));
    EXPECT_EQ(list.end(), list.find("testscopeD"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    auto rpid = fork();
    if (rpid == 0)
    {
        const char* const args[] = {"scoperegistry [Registry test]", TEST_RUNTIME_FILE, nullptr};
        if (execv(TEST_REGISTRY_PATH "/scoperegistry", const_cast<char* const*>(args)) < 0)
        {
            perror("Error starting scoperegistry:");
        }
        return 1;
    }
    else if (rpid > 0)
    {
        // Allow the registry process some time to start up,
        // so we don't get an ObjectNotExistException.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto rc = RUN_ALL_TESTS();
        kill(rpid, SIGTERM);
        return rc;
    }
    else
    {
        perror("Failed to fork:");
    }
    return 1;
}
