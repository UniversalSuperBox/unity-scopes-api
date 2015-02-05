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

#include <gtest/gtest.h>

#include <unity/scopes/qt/QCategorisedResult.h>
#include <unity/scopes/qt/QCategory.h>

#include <unity/scopes/qt/internal/QCategoryImpl.h>

#include <unity/scopes/Category.h>
#include <unity/scopes/CategoryRenderer.h>
#include <unity/scopes/internal/CategoryRegistry.h>

using namespace unity::scopes;
using namespace unity::scopes::internal;
using namespace unity::scopes::qt;

class QCategorisedResult_test
{
public:
    static std::shared_ptr<QCategory> createCategory()
    {
        CategoryRegistry reg;
        CategoryRenderer rdr;
        auto cat = reg.register_category("1", "title", "icon", nullptr, rdr);

        return unity::scopes::qt::internal::QCategoryImpl::create(cat);
    }
};

TEST(QCategorisedResult, bindings)
{
    std::shared_ptr<QCategory const> qCategory = QCategorisedResult_test::createCategory();

    QCategorisedResult result(qCategory);

    result.set_uri("test_uri");
    EXPECT_EQ(result.uri(),"test_uri");

    // test the [] operator
    EXPECT_EQ(result["uri"].toString(), "test_uri");
    result["test_attr"] = "test_value";
    EXPECT_EQ(result["test_attr"].toString(), "test_value");
    result["test_attr"] = "test_value2";
    EXPECT_EQ(result["test_attr"].toString(), "test_value2");

    // check the category stored
    EXPECT_EQ(result.category()->id(), qCategory->id());
    EXPECT_EQ(result.category()->title(), qCategory->title());
    EXPECT_EQ(result.category()->icon(), qCategory->icon());
    EXPECT_EQ(result.category()->serialize(), qCategory->serialize());


    // check the copy
    result["test_attr"] = "test_value3";
    QCategorisedResult result2(result);
    EXPECT_EQ(result2.category()->id(), qCategory->id());
    EXPECT_EQ(result2.category()->title(), qCategory->title());
    EXPECT_EQ(result2.category()->icon(), qCategory->icon());
    EXPECT_EQ(result2.category()->serialize(), qCategory->serialize());
    EXPECT_EQ(result2["test_attr"], "test_value3");
    EXPECT_EQ(result2.uri(),"test_uri");

    result = result2;
    EXPECT_EQ(result.category()->id(), qCategory->id());
    EXPECT_EQ(result.category()->title(), qCategory->title());
    EXPECT_EQ(result.category()->icon(), qCategory->icon());
    EXPECT_EQ(result.category()->serialize(), qCategory->serialize());
    EXPECT_EQ(result["test_attr"], "test_value3");
    EXPECT_EQ(result.uri(),"test_uri");
}