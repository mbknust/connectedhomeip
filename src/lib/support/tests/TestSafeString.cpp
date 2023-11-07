/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

/**
 *    @file
 *      This file implements a unit test suite for CHIP SafeString functions
 *
 */

#include <lib/support/SafeString.h>

#include <gtest/gtest.h>

using namespace chip;

TEST(TestSafeString, TestMaxStringLength)
{
    constexpr size_t len = MaxStringLength("a", "bc", "def");
    EXPECT_TRUE(len == 3);

    EXPECT_TRUE(MaxStringLength("bc") == 2);

    EXPECT_TRUE(MaxStringLength("def", "bc", "a") == 3);

    EXPECT_TRUE(MaxStringLength("") == 0);
}

TEST(TestSafeString, TestTotalStringLength)
{
    EXPECT_TRUE(TotalStringLength("") == 0);
    EXPECT_TRUE(TotalStringLength("a") == 1);
    EXPECT_TRUE(TotalStringLength("def", "bc", "a") == 6);
}