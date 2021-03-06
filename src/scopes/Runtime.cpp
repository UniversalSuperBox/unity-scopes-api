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

#include <unity/scopes/Runtime.h>

#include <unity/scopes/internal/RuntimeImpl.h>

using namespace std;

namespace unity
{

namespace scopes
{

//! @cond

Runtime::Runtime(string const& scope_id, string const& configfile) :
    p(internal::RuntimeImpl::create(scope_id, configfile))
{
}

Runtime::~Runtime()
{
}

//! @endcond

Runtime::UPtr Runtime::create(string const& configfile)
{
    return UPtr(new Runtime("", configfile));
}

Runtime::UPtr Runtime::create_scope_runtime(string const& scope_id, string const& configfile)
{
    return UPtr(new Runtime(scope_id, configfile));
}


void Runtime::destroy()
{
    p->destroy();
}

RegistryProxy Runtime::registry() const
{
    return p->registry();
}

void Runtime::run_scope(ScopeBase* const scope_base, std::string const& scope_ini_file)
{
    p->run_scope(scope_base, scope_ini_file);
}

ObjectProxy Runtime::string_to_proxy(string const& s) const
{
    return p->string_to_proxy(s);
}

string Runtime::proxy_to_string(ObjectProxy const& proxy) const
{
    return p->proxy_to_string(proxy);
}

} // namespace scopes

} // namespace unity
