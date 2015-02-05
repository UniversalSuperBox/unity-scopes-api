/*
 * Copyright (C) 2015 Canonical Ltd
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
 * Authored by: Xavi Garcia <xavi.garcia.mena@canonical.com>
 */

#pragma once

#include <QtCore/QVariant>
#include <unity/scopes/Variant.h>

#define EXPORT __attribute__((visibility("default")))

namespace unity
{

namespace scopes
{

namespace qt
{

///@cond
EXPORT QVariant scopeVariantToQVariant(unity::scopes::Variant const& variant);
EXPORT unity::scopes::Variant qVariantToScopeVariant(QVariant const& variant);
EXPORT QVariantMap scopeVariantMapToQVariantMap(unity::scopes::VariantMap const& variant);
EXPORT VariantMap qVariantMapToScopeVariantMap(QVariantMap const& variant);
///@endcond

}  // namespace qt

}  // namespace scopes

}  // namespace unity