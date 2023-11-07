/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
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

#include <gtest/gtest.h>
#include <lib/support/IniEscaping.h>

using namespace chip;
using namespace chip::IniEscaping;

namespace {

struct TestCase
{
    const char * input;
    const char * expectedOutput;
};

TEST(TestIniEscaping, TestEscaping)
{
    EXPECT_TRUE(EscapeKey("") == "");
    EXPECT_TRUE(EscapeKey("abcd1234,!") == "abcd1234,!");
    EXPECT_TRUE(EscapeKey("ab\ncd =12\\34\x7f") == "ab\\x0acd\\x20\\x3d12\\x5c34\\x7f");
    EXPECT_TRUE(EscapeKey(" ") == "\\x20");
    EXPECT_TRUE(EscapeKey("===") == "\\x3d\\x3d\\x3d");
}

TEST(TestIniEscaping, TestUnescaping)
{
    // Test valid cases
    EXPECT_TRUE(UnescapeKey("") == "");
    EXPECT_TRUE(UnescapeKey("abcd1234,!") == "abcd1234,!");
    std::string out = UnescapeKey("abcd1234,!");
    EXPECT_TRUE(UnescapeKey("ab\\x0acd\\x20\\x3d12\\x5c34\\x7f") == "ab\ncd =12\\34\x7f");
    EXPECT_TRUE(UnescapeKey("\\x20") == " ");
    EXPECT_TRUE(UnescapeKey("\\x3d\\x3d\\x3d") == "===");
    EXPECT_TRUE(UnescapeKey("\\x0d") == "\r");

    EXPECT_TRUE(UnescapeKey("\\x01\\x02\\x03\\x04\\x05\\x06\\x07") == "\x01\x02\x03\x04\x05\x06\x07");
    EXPECT_TRUE(UnescapeKey("\\x08\\x09\\x0a\\x0b\\x0c\\x0d\\x0e") == "\x08\x09\x0a\x0b\x0c\x0d\x0e");
    EXPECT_TRUE(UnescapeKey("\\x0f\\x10\\x11\\x12\\x13\\x14\\x15") == "\x0f\x10\x11\x12\x13\x14\x15");
    EXPECT_TRUE(UnescapeKey("\\x16\\x17\\x18\\x19\\x1a\\x1b\\x1c") == "\x16\x17\x18\x19\x1a\x1b\x1c");
    EXPECT_TRUE(UnescapeKey("\\x1d\\x1e\\x1f\\x20\\x7f\\x3d\\x5c") == "\x1d\x1e\x1f \x7f=\\");
    EXPECT_TRUE(UnescapeKey("\\x81\\x82\\xff") == "\x81\x82\xff");

    // Test invalid cases

    // letters should never be escaped
    EXPECT_TRUE(UnescapeKey("\\x5a\55") != "ZU");
    EXPECT_TRUE(UnescapeKey("\\x5a\55") == "");

    // Capitalized hex forbidden
    EXPECT_TRUE(UnescapeKey("\\x0D") == "");

    // Partial escapes forbidden
    EXPECT_TRUE(UnescapeKey("1\\x0") == "");
}

TEST(TestIniEscaping, TestRoundTrip)
{
    EXPECT_TRUE(UnescapeKey(EscapeKey("")) == "");
    EXPECT_TRUE(UnescapeKey(EscapeKey("abcd1234,!")) == "abcd1234,!");
    EXPECT_TRUE(UnescapeKey(EscapeKey("ab\ncd =12\\34\x7f")) == "ab\ncd =12\\34\x7f");
    EXPECT_TRUE(UnescapeKey(EscapeKey(" ")) == " ");
    EXPECT_TRUE(UnescapeKey(EscapeKey("===")) == "===");
    EXPECT_TRUE(UnescapeKey(EscapeKey("\r")) == "\r");

    EXPECT_TRUE(UnescapeKey(EscapeKey("\x01\x02\x03\x04\x05\x06\x07")) == "\x01\x02\x03\x04\x05\x06\x07");
    EXPECT_TRUE(UnescapeKey(EscapeKey("\x08\x09\x0a\x0b\x0c\x0d\x0e")) == "\x08\x09\x0a\x0b\x0c\x0d\x0e");
    EXPECT_TRUE(UnescapeKey(EscapeKey("\x0f\x10\x11\x12\x13\x14\x15")) == "\x0f\x10\x11\x12\x13\x14\x15");
    EXPECT_TRUE(UnescapeKey(EscapeKey("\x16\x17\x18\x19\x1a\x1b\x1c")) == "\x16\x17\x18\x19\x1a\x1b\x1c");
    EXPECT_TRUE(UnescapeKey(EscapeKey("\x1d\x1e\x1f \x7f=\\")) == "\x1d\x1e\x1f \x7f=\\");
    EXPECT_TRUE(UnescapeKey(EscapeKey("\x81\x82\xff")) == "\x81\x82\xff");

    // Make sure entire range is escapable
    for (int c = 0; c <= 255; c++)
    {
        std::string s(5, static_cast<char>(c));
        ASSERT_EQ(UnescapeKey(EscapeKey(s)), s);
    }
}
} // namespace
