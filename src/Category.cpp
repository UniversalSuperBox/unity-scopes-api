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
 * Authored by: Pawel Stolowski <pawel.stolowski@canonical.com>
 */

#include <scopes/Category.h>
#include <scopes/internal/CategoryImpl.h>

namespace unity
{

namespace api
{

namespace scopes
{

//! @cond

Category::Category(std::string const& id, std::string const& title, std::string const &icon, std::string const& renderer_template)
    : p(new internal::CategoryImpl(id, title, icon, renderer_template))
{
}

Category::Category(VariantMap const& variant_map)
    : p(new internal::CategoryImpl(variant_map))
{
}

std::string const& Category::id() const
{
    return p->id();
}

std::shared_ptr<VariantMap> Category::variant_map() const
{
    return p->variant_map();
}

//! @endcond

} // namespace scopes

} // namespace api

} // namespace unity
