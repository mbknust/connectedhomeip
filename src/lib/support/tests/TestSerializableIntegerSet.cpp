/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/SerializableIntegerSet.h>

#include <gtest/gtest.h>

namespace {
class TestSerializableIntegerSet : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

TEST_F(TestSerializableIntegerSet, TestSerializableIntegerSet)
{
    chip::SerializableU64Set<8> set;
    EXPECT_TRUE(!set.Contains(123));

    EXPECT_TRUE(set.Insert(123) == CHIP_NO_ERROR);
    EXPECT_TRUE(set.Contains(123));

    EXPECT_TRUE(set.Insert(123) == CHIP_NO_ERROR);
    EXPECT_TRUE(set.Contains(123));

    set.Remove(123);
    EXPECT_TRUE(!set.Contains(123));

    for (uint64_t i = 1; i <= 8; i++)
    {
        EXPECT_TRUE(set.Insert(i) == CHIP_NO_ERROR);
    }

    EXPECT_TRUE(set.Insert(9) != CHIP_NO_ERROR);

    for (uint64_t i = 1; i <= 8; i++)
    {
        EXPECT_TRUE(set.Contains(i));
    }

    size_t size = set.SerializedSize();
    EXPECT_TRUE(set.MaxSerializedSize() == size);

    for (uint64_t i = 1; i <= 7; i++)
    {
        set.Remove(i);
        EXPECT_TRUE(set.SerializedSize() == size);
    }

    set.Remove(8);
    EXPECT_TRUE(set.SerializedSize() == 0);
}

TEST_F(TestSerializableIntegerSet, TestSerializableIntegerSetNonZero)
{
    chip::SerializableU64Set<8, 2> set;
    EXPECT_TRUE(!set.Contains(123));

    EXPECT_TRUE(set.Insert(123) == CHIP_NO_ERROR);
    EXPECT_TRUE(set.Contains(123));

    EXPECT_TRUE(set.Insert(123) == CHIP_NO_ERROR);
    EXPECT_TRUE(set.Contains(123));

    set.Remove(123);
    EXPECT_TRUE(!set.Contains(123));

    for (uint64_t i = 0; i <= 1; i++)
    {
        EXPECT_TRUE(set.Insert(i) == CHIP_NO_ERROR);
    }

    // Try inserting empty value
    EXPECT_TRUE(set.Insert(2) != CHIP_NO_ERROR);

    for (uint64_t i = 3; i <= 7; i++)
    {
        EXPECT_TRUE(set.Insert(i) == CHIP_NO_ERROR);
    }

    for (uint64_t i = 0; i <= 1; i++)
    {
        EXPECT_TRUE(set.Contains(i));
    }

    for (uint64_t i = 3; i <= 7; i++)
    {
        EXPECT_TRUE(set.Contains(i));
    }

    for (uint64_t i = 0; i <= 6; i++)
    {
        set.Remove(i);
    }

    set.Remove(7);
    EXPECT_TRUE(set.SerializedSize() == 0);
}

TEST_F(TestSerializableIntegerSet, TestSerializableIntegerSetSerialize)
{
    chip::SerializableU64Set<8> set;

    for (uint64_t i = 1; i <= 6; i++)
    {
        EXPECT_TRUE(set.Insert(i) == CHIP_NO_ERROR);
    }

    EXPECT_TRUE(!set.Contains(0));
    for (uint64_t i = 1; i <= 6; i++)
    {
        EXPECT_TRUE(set.Contains(i));
    }
    EXPECT_TRUE(!set.Contains(7));

    EXPECT_TRUE(set.Serialize([&](chip::ByteSpan serialized) -> CHIP_ERROR {
        EXPECT_TRUE(serialized.size() == 48);
        return CHIP_NO_ERROR;
    }) == CHIP_NO_ERROR);

    EXPECT_TRUE(set.Serialize([&](chip::ByteSpan serialized) -> CHIP_ERROR {
        chip::SerializableU64Set<8> set2;
        EXPECT_TRUE(set2.Deserialize(serialized) == CHIP_NO_ERROR);

        EXPECT_TRUE(!set2.Contains(0));
        for (uint64_t i = 1; i <= 6; i++)
        {
            EXPECT_TRUE(set2.Contains(i));
        }
        EXPECT_TRUE(!set2.Contains(7));
        return CHIP_NO_ERROR;
    }) == CHIP_NO_ERROR);
}

} // namespace
