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
 */

// IMPORTANT: Any change to this source file must be reflected in scopebuild
//            and vice versa. This is so we can test that scopetest.cpp builds and
//            runs as part of the unity tests, instead of finding out only at release
//            time that something is broken.

#include <unity/scopes/PreviewQueryBase.h>
#include <unity/scopes/QueryBase.h>
#include <unity/scopes/ReplyProxyFwd.h>
#include <unity/scopes/ScopeBase.h>

class DemoScope : public unity::scopes::ScopeBase
{
public:
    virtual void start(std::string const&) override;

    virtual void stop() override;

    virtual unity::scopes::PreviewQueryBase::UPtr preview(const unity::scopes::Result&,
                                                          const unity::scopes::ActionMetadata&) override;

    virtual unity::scopes::SearchQueryBase::UPtr search(unity::scopes::CannedQuery const& q,
                                                        unity::scopes::SearchMetadata const&) override;
};

using namespace unity::scopes;

void DemoScope::start(std::string const&)
{
}

void DemoScope::stop()
{
}

SearchQueryBase::UPtr DemoScope::search(unity::scopes::CannedQuery const& /*q*/,
        unity::scopes::SearchMetadata const&)
{
    unity::scopes::SearchQueryBase::UPtr query(nullptr);
    return query;
}

PreviewQueryBase::UPtr DemoScope::preview(Result const& /*result*/, ActionMetadata const& /*metadata*/) {
    unity::scopes::PreviewQueryBase::UPtr preview(nullptr);
    return preview;
}

int main(int, char**)
{
    DemoScope d;
    return 0;
}
