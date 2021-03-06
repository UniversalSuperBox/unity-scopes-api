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

#include <unity/scopes/utility/internal/BufferedResultForwarderImpl.h>
#include <unity/scopes/utility/internal/BufferedSearchReplyImpl.h>
#include <unity/scopes/SearchReply.h>
#include <unity/UnityExceptions.h>
#include <cassert>

namespace unity
{

namespace scopes
{

namespace utility
{

namespace internal
{

BufferedResultForwarderImpl::BufferedResultForwarderImpl(unity::scopes::SearchReplyProxy const& upstream,
        unity::scopes::utility::BufferedResultForwarder::SPtr const& next_forwarder)
    : ready_(false),
      has_previous_(false),
      previous_ready_(false),
      upstream_(std::make_shared<internal::BufferedSearchReplyImpl>(upstream)),
      next_(next_forwarder)
{
    if (next_forwarder)
    {
        if (next_forwarder->p->has_previous_.exchange(true))
        {
            throw LogicException("The next forwarder has already been linked to another BufferedResultForwarder");
        }
    }
}

unity::scopes::SearchReplyProxy BufferedResultForwarderImpl::upstream() const
{
    return upstream_;
}

void BufferedResultForwarderImpl::push(CategorisedResult result)
{
    upstream_->push(result);
    set_ready();
}

bool BufferedResultForwarderImpl::is_ready() const
{
    return ready_;
}

void BufferedResultForwarderImpl::set_ready()
{
    // scope author tells us that results for this forwarder are now ready
    // to be displayed (or the query has finished); set the 'ready' flag
    // and if previous forwarder is also ready, then push and disable
    // further buffering.
    if (!ready_.exchange(true))
    {
        if (previous_ready_ || !has_previous_)
        {
            flush_and_notify();
        }
    }
}

void BufferedResultForwarderImpl::notify_ready()
{
    // we got notified that previous forwarder is ready;
    // if we are ready then notify following forwarder
    previous_ready_ = true;
    if (ready_)
    {
        flush_and_notify();
    }
}

void BufferedResultForwarderImpl::flush_and_notify()
{
    auto buf = std::dynamic_pointer_cast<internal::BufferedSearchReplyImpl>(upstream_);
    assert(buf);

    buf->flush();

    // notify next forwarder that this one is ready
    if (next_)
    {
        next_->p->notify_ready();
    }
}

void BufferedResultForwarderImpl::finished(CompletionDetails const&)
{
    set_ready();
}

} // namespace internal

} // utility

} // namespace scopes

} // namespace unity
