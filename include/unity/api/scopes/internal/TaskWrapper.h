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

#ifndef UNITY_API_SCOPES_INTERNAL_TASKWRAPPER_H
#define UNITY_API_SCOPES_INTERNAL_TASKWRAPPER_H

#include <memory>

namespace unity
{

namespace api
{

namespace scopes
{

namespace internal
{

// Simple wrapper for tasks. Allows us to use a std::packaged_task as the functor
// for a task in a thread pool, without having to know the task's return type in advance.

class TaskWrapper final
{
public:
    TaskWrapper() = default;
    ~TaskWrapper() = default;

    template<typename F>
    TaskWrapper(F&& f) :
        task_(new WrapperType<F>(std::move(f)))
    {
    }

    TaskWrapper(TaskWrapper&&) = default;
    TaskWrapper& operator=(TaskWrapper&&) = default;

    TaskWrapper(TaskWrapper&) = delete;
    TaskWrapper(TaskWrapper const&) = delete;
    TaskWrapper& operator=(TaskWrapper const&) = delete;

    void operator()()
    {
        task_->call();
    }

private:
    struct WrapperBase
    {
        virtual void call() = 0;
        virtual ~WrapperBase() noexcept {};
    };

    template<typename F>
    struct WrapperType : WrapperBase
    {
        F f_;
        WrapperType(F&& f) :
            f_(std::move(f))
        {
        }
        void call()
        {
            f_();
        };
        virtual ~WrapperType() noexcept {};
    };

    std::unique_ptr<WrapperBase> task_;
};

} // namespace internal

} // namespace scopes

} // namespace api

} // namespace unity

#endif