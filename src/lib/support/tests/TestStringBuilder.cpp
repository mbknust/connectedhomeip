/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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
#include <lib/support/StringBuilder.h>

#include <gtest/gtest.h>

namespace {

using namespace chip;

TEST(TestStringBuilder, TestStringBuilder)
{
    StringBuilder<64> builder;

    EXPECT_TRUE(builder.Fit());
    EXPECT_TRUE(strcmp(builder.c_str(), "") == 0);

    builder.Add("foo");
    EXPECT_TRUE(builder.Fit());
    EXPECT_TRUE(strcmp(builder.c_str(), "foo") == 0);

    builder.Add("bar");
    EXPECT_TRUE(builder.Fit());
    EXPECT_TRUE(strcmp(builder.c_str(), "foobar") == 0);
}

TEST(TestStringBuilder, TestIntegerAppend)
{
    StringBuilder<64> builder;

    builder.Add("nr: ").Add(1234);
    EXPECT_TRUE(builder.Fit());
    EXPECT_TRUE(strcmp(builder.c_str(), "nr: 1234") == 0);

    builder.Add(", ").Add(-22);
    EXPECT_TRUE(builder.Fit());
    EXPECT_TRUE(strcmp(builder.c_str(), "nr: 1234, -22") == 0);
}

TEST(TestStringBuilder, TestOverflow)
{
    {
        StringBuilder<4> builder;

        builder.Add("foo");
        EXPECT_TRUE(builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "foo") == 0);

        builder.Add("bar");
        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "foo") == 0);
    }

    {
        StringBuilder<7> builder;

        builder.Add("x: ").Add(12345);
        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "x: 123") == 0);
    }
}

TEST(TestStringBuilder, TestFormat)
{
    {
        StringBuilder<100> builder;

        builder.AddFormat("Test: %d Hello %s\n", 123, "world");

        EXPECT_TRUE(builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "Test: 123 Hello world\n") == 0);
    }

    {
        StringBuilder<100> builder;

        builder.AddFormat("Align: %-5s", "abc");

        EXPECT_TRUE(builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "Align: abc  ") == 0);
    }

    {
        StringBuilder<100> builder;

        builder.AddFormat("Multi: %d", 1234);
        builder.AddFormat(", then 0x%04X", 0xab);

        EXPECT_TRUE(builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "Multi: 1234, then 0x00AB") == 0);
    }
}

TEST(TestStringBuilder, TestFormatOverflow)
{
    {
        StringBuilder<13> builder;

        builder.AddFormat("Test: %d Hello %s\n", 123, "world");

        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "Test: 123 He") == 0);
    }

    {
        StringBuilder<11> builder;

        builder.AddFormat("%d %d %d %d %d", 1, 2, 3, 4, 1234);

        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "1 2 3 4 12") == 0);

        builder.AddMarkerIfOverflow();
        EXPECT_TRUE(strcmp(builder.c_str(), "1 2 3 4...") == 0);
    }

    {
        StringBuilder<11> builder;

        builder.AddFormat("%d", 1234);
        EXPECT_TRUE(builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "1234") == 0);

        builder.AddFormat("%s", "abc");
        EXPECT_TRUE(builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "1234abc") == 0);

        builder.AddMarkerIfOverflow(); // no overflow
        EXPECT_TRUE(strcmp(builder.c_str(), "1234abc") == 0);

        builder.AddFormat("%08x", 0x123456);
        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "1234abc001") == 0);

        builder.AddMarkerIfOverflow();
        EXPECT_TRUE(strcmp(builder.c_str(), "1234abc...") == 0);
    }
}

TEST(TestStringBuilder, TestOverflowMarker)
{
    {
        StringBuilder<1> builder; // useless builder, but ok

        builder.Add("abc123");

        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "") == 0);

        builder.AddMarkerIfOverflow();
        EXPECT_TRUE(strcmp(builder.c_str(), "") == 0);
    }

    {
        StringBuilder<2> builder;

        builder.Add("abc123");

        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "a") == 0);

        builder.AddMarkerIfOverflow();
        EXPECT_TRUE(strcmp(builder.c_str(), ".") == 0);
    }

    {
        StringBuilder<3> builder;

        builder.Add("abc123");

        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "ab") == 0);

        builder.AddMarkerIfOverflow();
        EXPECT_TRUE(strcmp(builder.c_str(), "..") == 0);
    }

    {
        StringBuilder<4> builder;

        builder.Add("abc123");

        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "abc") == 0);

        builder.AddMarkerIfOverflow();
        EXPECT_TRUE(strcmp(builder.c_str(), "...") == 0);
    }

    {
        StringBuilder<5> builder;

        builder.Add("abc123");

        EXPECT_TRUE(!builder.Fit());
        EXPECT_TRUE(strcmp(builder.c_str(), "abc1") == 0);

        builder.AddMarkerIfOverflow();
        EXPECT_TRUE(strcmp(builder.c_str(), "a...") == 0);
    }
}
} // namespace
