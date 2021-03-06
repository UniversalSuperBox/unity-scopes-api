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

#pragma once

#include <capnp/message.h>
#include <scopes/internal/zmq_middleware/capnproto/Message.capnp.h>
#include <unity/util/NonCopyable.h>

#include <memory>
#include <vector>

namespace unity
{

namespace scopes
{

namespace internal
{

namespace zmq_middleware
{

struct Current;

void marshal_unknown_exception(capnproto::Response::Builder& r, std::string const& s);
void marshal_object_not_exist_exception(capnproto::Response::Builder& r, Current const& c);
void marshal_operation_not_exist_exception(capnproto::Response::Builder& r, Current const& c);

kj::ArrayPtr<kj::ArrayPtr<capnp::word const> const> create_unknown_response(capnp::MessageBuilder& b,
                                                                            std::string const& s);
kj::ArrayPtr<kj::ArrayPtr<capnp::word const> const> create_object_not_exist_response(capnp::MessageBuilder& b,
                                                                                     Current const& c);

void throw_if_runtime_exception(capnproto::Response::Reader const& response);

std::string decode_runtime_exception(capnproto::Response::Reader const& response);
std::string decode_status(capnproto::Response::Reader const& response);

} // namespace zmq_middleware

} // namespace internal

} // namespace scopes

} // namespace unity
