/*
 * Copyright (C) 2013 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the Lesser GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Lesser GNU General Public License for more details.
 *
 * You should have received a copy of the Lesser GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Michi Henning <michi.henning@canonical.com>
 */

#ifndef UNITY_API_SCOPES_INTERNAL_MWREGISTRY_H
#define UNITY_API_SCOPES_INTERNAL_MWREGISTRY_H

#include <unity/api/scopes/internal/MWObject.h>
#include <unity/api/scopes/Registry.h>

namespace unity
{

namespace api
{

namespace scopes
{

namespace internal
{

class MWRegistry : public virtual MWObject
{
public:
    // Remote operation implementation
    virtual ScopeProxy find(std::string const& scope_name) = 0;
    virtual ScopeMap list() = 0;

    virtual ~MWRegistry() noexcept;

protected:
    MWRegistry(MiddlewareBase* mw_base);
};

} // namespace internal

} // namespace scopes

} // namespace api

} // namespace unity

#endif