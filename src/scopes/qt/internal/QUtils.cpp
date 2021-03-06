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

#include <unity/scopes/qt/internal/QUtils.h>

#include <unity/UnityExceptions.h>

#include <cassert>

using namespace unity::scopes::qt;
using namespace std;

namespace sc = unity::scopes;
namespace qti = unity::scopes::qt::internal;

namespace unity
{

namespace scopes
{

namespace qt
{

namespace internal
{

QVariant variant_to_qvariant(sc::Variant const& variant)
{
    switch (variant.which())
    {
        case sc::Variant::Type::Null:
            return QVariant();
        case sc::Variant::Type::Int:
            return QVariant(variant.get_int());
        case sc::Variant::Type::Bool:
            return QVariant(variant.get_bool());
        case sc::Variant::Type::String:
            return QVariant(QString::fromStdString(variant.get_string()));
        case sc::Variant::Type::Double:
            return QVariant(variant.get_double());
        case sc::Variant::Type::Dict:
        {
            sc::VariantMap dict(variant.get_dict());
            QVariantMap result_dict;
            for (auto it = dict.begin(); it != dict.end(); ++it)
            {
                result_dict.insert(QString::fromStdString(it->first), qti::variant_to_qvariant(it->second));
            }
            return result_dict;
        }
        case sc::Variant::Type::Array:
        {
            sc::VariantArray arr(variant.get_array());
            QVariantList result_list;
            for (unsigned i = 0; i < arr.size(); i++)
            {
                result_list.append(qti::variant_to_qvariant(arr[i]));
            }
            return result_list;
        }
        default:
        {
            assert(false);  // LCOV_EXCL_LINE
            return QVariant();
        }
    }
}

sc::Variant qvariant_to_variant(QVariant const& variant)
{
    if (variant.isNull())
    {
        return sc::Variant();
    }

    switch (variant.type())
    {
        case QMetaType::Bool:
            return sc::Variant(variant.toBool());
        case QMetaType::Int:
            return sc::Variant(variant.toInt());
        case QMetaType::Double:
            return sc::Variant(variant.toDouble());
        case QMetaType::QString:
            return sc::Variant(variant.toString().toStdString());
        case QMetaType::QVariantMap:
        {
            sc::VariantMap vm;
            QVariantMap m(variant.toMap());
            for (auto it = m.begin(); it != m.end(); ++it)
            {
                vm[it.key().toStdString()] = qti::qvariant_to_variant(it.value());
            }
            return sc::Variant(vm);
        }
        case QMetaType::QVariantList:
        {
            QVariantList l(variant.toList());
            sc::VariantArray arr;
            for (int i = 0; i < l.size(); i++)
            {
                arr.push_back(qti::qvariant_to_variant(l[i]));
            }
            return sc::Variant(arr);
        }
        default:
        {
            throw unity::InvalidArgumentException(string("qvariant_to_variant(): invalid source type: ") +
                                                  variant.typeName());
        }
    }
}

QVariantMap variantmap_to_qvariantmap(unity::scopes::VariantMap const& variant)
{
    QVariantMap ret_map;
    for (auto item : variant)
    {
        ret_map[QString::fromUtf8(item.first.c_str())] = qti::variant_to_qvariant(item.second);
    }

    return ret_map;
}

VariantMap qvariantmap_to_variantmap(QVariantMap const& variant)
{
    VariantMap ret_map;
    QMapIterator<QString, QVariant> it(variant);
    while (it.hasNext())
    {
        it.next();
        ret_map[it.key().toUtf8().data()] = qti::qvariant_to_variant(it.value());
    }

    return ret_map;
}

}  // namespace internal

}  // namespace qt

}  // namespace scopes

}  // namespace unity
