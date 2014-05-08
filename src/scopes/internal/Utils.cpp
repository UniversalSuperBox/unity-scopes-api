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

#include <unity/scopes/internal/Utils.h>
#include <unity/UnityExceptions.h>
#include <sstream>
#include <iomanip>

namespace unity
{

namespace scopes
{

namespace internal
{

VariantMap::const_iterator find_or_throw(std::string const& context, VariantMap const& var, std::string const& key)
{
    auto it = var.find(key);
    if (it == var.end())
    {
        std::stringstream str;
        str << context << ": missing '"  << key << "' element";
        throw unity::InvalidArgumentException(str.str());
    }
    return it;
}

std::string to_percent_encoding(std::string const& str)
{
    std::ostringstream result;
    for (auto const& c: str)
    {
        if ((!isalnum(c)))
        {
            result << '%' << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << static_cast<int>(static_cast<unsigned char>(c)) << std::nouppercase;
        }
        else
        {
            result << c;
        }
    }
    return result.str();
}

std::string from_percent_encoding(std::string const& str)
{
    std::ostringstream result;
    for (auto it = str.begin(); it != str.end(); it++)
    {
        auto c = *it;
        if (c == '%')
        {
            bool valid = false;
            // take two characters and covert them from hex to actual char
            if (++it != str.end())
            {
                c = *it;
                if (++it != str.end())
                {
                    std::string const hexnum { c, *it };
                    try
                    {
                        auto k = std::stoi(hexnum, nullptr, 16);
                        result << static_cast<char>(k);
                        valid = true;
                    }
                    catch (std::logic_error const& e) // covers both std::invalid_argument and std::out_of_range
                    {
                        std::stringstream err;
                        err << "from_percent_encoding(): unsupported conversion of '" << hexnum << "'";
                        throw unity::InvalidArgumentException(err.str());
                    }
                }
            }
            if (!valid)
            {
                throw unity::InvalidArgumentException("from_percent_encoding(): too few characters for percent-encoded value");
            }
        }
        else
        {
            result << c;
        }
    }
    return result.str();
}

} // namespace internal

} // namespace scopes

} // namespace unity