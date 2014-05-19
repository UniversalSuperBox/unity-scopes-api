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

#include <unity/scopes/internal/zmq_middleware/ZmqPublisher.h>

#include <unity/scopes/ScopeExceptions.h>
#include <unity/util/ResourcePtr.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace unity
{

namespace scopes
{

namespace internal
{

namespace zmq_middleware
{

ZmqPublisher::ZmqPublisher(zmqpp::context* context, std::string const& publisher_name,
                           std::string const& endpoint_dir)
    : context_(context)
    , endpoint_("ipc://" + endpoint_dir + "/" + publisher_name)
    , thread_state_(NotRunning)
    , thread_exception_(nullptr)
{
    // Validate publisher_name
    if (publisher_name.find('/') != std::string::npos)
    {
        throw MiddlewareException("ZmqPublisher(): A publisher cannot contain a '/' in its id");
    }

    // Start the publisher thread
    thread_ = std::thread(&ZmqPublisher::publisher_thread, this);

    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return thread_state_ == Running || thread_state_ == Failed; });

    if (thread_state_ == Failed)
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
        try
        {
            std::rethrow_exception(thread_exception_);
        }
        catch (...)
        {
            throw MiddlewareException("ZmqPublisher(): publisher thread failed to start (endpoint: " +
                                      endpoint_ + ")");
        }
    }
}

ZmqPublisher::~ZmqPublisher()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        thread_state_ = Stopping;
        cond_.notify_all();
    }

    if (thread_.joinable())
    {
        thread_.join();
    }
}

std::string ZmqPublisher::endpoint() const
{
    return endpoint_;
}

void ZmqPublisher::send_message(std::string const& message, std::string const& topic)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Write message in the format: "<topic>:<message>"
    message_queue_.push(topic + ':' + message);
    cond_.notify_all();
}

void ZmqPublisher::publisher_thread()
{
    try
    {
        // Create the publisher socket
        zmqpp::socket pub_socket(zmqpp::socket(*context_, zmqpp::socket_type::publish));
        pub_socket.set(zmqpp::socket_option::linger, 5000);
        safe_bind(pub_socket);

        // Notify constructor that the thread is now running
        std::unique_lock<std::mutex> lock(mutex_);
        thread_state_ = Running;
        cond_.notify_all();

        // Wait for send_message or stop
        while (true)
        {
            // mutex_ unlocked
            cond_.wait(lock, [this] { return thread_state_ == Stopping || !message_queue_.empty(); });
            // mutex_ locked

            // Flush out the message queue before stopping the thread
            if (!message_queue_.empty())
            {
                pub_socket.send(message_queue_.front());
                message_queue_.pop();
            }
            else if (thread_state_ == Stopping)
            {
                break;
            }
        }

        // Clean up
        pub_socket.close();
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        thread_exception_ = std::current_exception();
        thread_state_ = Failed;
        cond_.notify_all();
    }
}

void ZmqPublisher::safe_bind(zmqpp::socket& socket)
{
    const std::string transport_prefix = "ipc://";
    std::string path = endpoint_.substr(transport_prefix.size());

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
    {
        throw MiddlewareException("ZmqPublisher::safe_bind(): cannot create socket: " +
                                  std::string(strerror(errno)));
    }
    util::ResourcePtr<int, decltype(&::close)> close_guard(fd, ::close);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
    {
        // Connect succeeded, so another server is using the socket already.
        throw MiddlewareException("ZmqPublisher::safe_bind(): endpoint in use: " + endpoint_);
    }
    socket.bind(endpoint_);
}

} // namespace zmq_middleware

} // namespace internal

} // namespace scopes

} // namespace unity
