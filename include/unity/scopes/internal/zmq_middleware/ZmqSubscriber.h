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
 * Authored by: Marcus Tomlinson <marcus.tomlinson@canonical.com>
 */

#pragma once

#include <condition_variable>
#include <thread>
#include <unity/scopes/internal/MWSubscriber.h>
#include <zmqpp/context.hpp>

namespace unity
{

namespace scopes
{

namespace internal
{

namespace zmq_middleware
{

class StopPublisher;

class ZmqSubscriber : public virtual MWSubscriber
{
public:
    ZmqSubscriber(zmqpp::context* context, std::string const& publisher_id,
                  std::string const& endpoint_dir, std::string const& topic);
    virtual ~ZmqSubscriber();

    std::string endpoint() const override;

private:
    enum ThreadState
    {
        NotRunning,
        Running,
        Stopping,
        Stopped
    };

    zmqpp::context* const context_;
    std::string const endpoint_;
    std::string const topic_;

    std::thread thread_;
    std::unique_ptr<StopPublisher> thread_stopper_;
    std::mutex mutex_;
    std::condition_variable cond_;
    ThreadState thread_state_;
    std::exception_ptr thread_exception_;

    void subscriber_thread();
};

} // namespace zmq_middleware

} // namespace internal

} // namespace scopes

} // namespace unity
