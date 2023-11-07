/*
 *
 *    Copyright (c) 2023 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
#include <lib/support/StringSplitter.h>

#include <gtest/gtest.h>

namespace {

using namespace chip;

TEST(TestStringSplitter, TestStrdupSplitter)
{
    CharSpan out;

    // empty string handling
    {
        StringSplitter splitter("", ',');

        // next stays at nullptr
        EXPECT_TRUE(!splitter.Next(out));
        EXPECT_TRUE(out.data() == nullptr);
        EXPECT_TRUE(!splitter.Next(out));
        EXPECT_TRUE(out.data() == nullptr);
        EXPECT_TRUE(!splitter.Next(out));
        EXPECT_TRUE(out.data() == nullptr);
    }

    // single item
    {
        StringSplitter splitter("single", ',');

        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("single")));

        // next stays at nullptr also after valid data
        EXPECT_TRUE(!splitter.Next(out));
        EXPECT_TRUE(out.data() == nullptr);
        EXPECT_TRUE(!splitter.Next(out));
        EXPECT_TRUE(out.data() == nullptr);
    }

    // multi-item
    {
        StringSplitter splitter("one,two,three", ',');

        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("one")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("two")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("three")));
        EXPECT_TRUE(!splitter.Next(out));
        EXPECT_TRUE(out.data() == nullptr);
    }

    // mixed
    {
        StringSplitter splitter("a**bc*d,e*f", '*');

        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("a")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("bc")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("d,e")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("f")));
        EXPECT_TRUE(!splitter.Next(out));
    }

    // some edge cases
    {
        StringSplitter splitter(",", ',');
        // Note that even though "" is nullptr right away, "," becomes two empty strings
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(!splitter.Next(out));
    }
    {
        StringSplitter splitter("log,", ',');
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("log")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(!splitter.Next(out));
    }
    {
        StringSplitter splitter(",log", ',');
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("log")));
        EXPECT_TRUE(!splitter.Next(out));
    }
    {
        StringSplitter splitter(",,,", ',');
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(splitter.Next(out));
        EXPECT_TRUE(out.data_equal(CharSpan::fromCharString("")));
        EXPECT_TRUE(!splitter.Next(out));
    }
}

TEST(TestStringSplitter, TestNullResilience)
{
    {
        StringSplitter splitter(nullptr, ',');
        CharSpan span;
        EXPECT_TRUE(!splitter.Next(span));
        EXPECT_TRUE(span.data() == nullptr);
    }
}
} // namespace
