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

/**
 *    @file
 *      This file implements a unit test suite for CHIP SafeInt functions
 *
 */

#include <lib/support/Span.h>

#include <gtest/gtest.h>

using namespace chip;

TEST(TestSpan, TestByteSpan)
{
    uint8_t arr[] = { 1, 2, 3 };

    ByteSpan s0 = ByteSpan();
    EXPECT_TRUE(s0.size() == 0);
    EXPECT_TRUE(s0.empty());
    EXPECT_TRUE(s0.data_equal(s0));

    ByteSpan s1(arr, 2);
    EXPECT_TRUE(s1.data() == arr);
    EXPECT_TRUE(s1.size() == 2);
    EXPECT_TRUE(!s1.empty());
    EXPECT_TRUE(s1.data_equal(s1));
    EXPECT_TRUE(!s1.data_equal(s0));

    ByteSpan s2(arr);
    EXPECT_TRUE(s2.data() == arr);
    EXPECT_TRUE(s2.size() == 3);
    EXPECT_TRUE(s2.data()[2] == 3);
    EXPECT_TRUE(!s2.empty());
    EXPECT_TRUE(s2.data_equal(s2));
    EXPECT_TRUE(!s2.data_equal(s1));
    EXPECT_TRUE(s2.front() == 1);
    EXPECT_TRUE(s2.back() == 3);
    EXPECT_TRUE(s2[0] == 1);
    EXPECT_TRUE(s2[1] == 2);
    EXPECT_TRUE(s2[2] == 3);

    ByteSpan s3 = s2;
    EXPECT_TRUE(s3.data() == arr);
    EXPECT_TRUE(s3.size() == 3);
    EXPECT_TRUE(s3.data()[2] == 3);
    EXPECT_TRUE(!s3.empty());
    EXPECT_TRUE(s3.data_equal(s2));

    uint8_t arr2[] = { 3, 2, 1 };
    ByteSpan s4(arr2);
    EXPECT_TRUE(!s4.data_equal(s2));

    ByteSpan s5(arr2, 0);
    EXPECT_TRUE(s5.data() != nullptr);
    EXPECT_TRUE(!s5.data_equal(s4));
    EXPECT_TRUE(s5.data_equal(s0));
    EXPECT_TRUE(s0.data_equal(s5));

    ByteSpan s6(arr2);
    s6.reduce_size(2);
    EXPECT_TRUE(s6.size() == 2);
    ByteSpan s7(arr2, 2);
    EXPECT_TRUE(s6.data_equal(s7));
    EXPECT_TRUE(s7.data_equal(s6));
}

TEST(TestSpan, TestMutableByteSpan)
{
    uint8_t arr[] = { 1, 2, 3 };

    MutableByteSpan s0 = MutableByteSpan();
    EXPECT_TRUE(s0.size() == 0);
    EXPECT_TRUE(s0.empty());
    EXPECT_TRUE(s0.data_equal(s0));

    MutableByteSpan s1(arr, 2);
    EXPECT_TRUE(s1.data() == arr);
    EXPECT_TRUE(s1.size() == 2);
    EXPECT_TRUE(!s1.empty());
    EXPECT_TRUE(s1.data_equal(s1));
    EXPECT_TRUE(!s1.data_equal(s0));

    MutableByteSpan s2(arr);
    EXPECT_TRUE(s2.data() == arr);
    EXPECT_TRUE(s2.size() == 3);
    EXPECT_TRUE(s2.data()[2] == 3);
    EXPECT_TRUE(!s2.empty());
    EXPECT_TRUE(s2.data_equal(s2));
    EXPECT_TRUE(!s2.data_equal(s1));

    MutableByteSpan s3 = s2;
    EXPECT_TRUE(s3.data() == arr);
    EXPECT_TRUE(s3.size() == 3);
    EXPECT_TRUE(s3.data()[2] == 3);
    EXPECT_TRUE(!s3.empty());
    EXPECT_TRUE(s3.data_equal(s2));

    uint8_t arr2[] = { 3, 2, 1 };
    MutableByteSpan s4(arr2);
    EXPECT_TRUE(!s4.data_equal(s2));

    MutableByteSpan s5(arr2, 0);
    EXPECT_TRUE(s5.data() != nullptr);
    EXPECT_TRUE(!s5.data_equal(s4));
    EXPECT_TRUE(s5.data_equal(s0));
    EXPECT_TRUE(s0.data_equal(s5));

    MutableByteSpan s6(arr2);
    s6.reduce_size(2);
    EXPECT_TRUE(s6.size() == 2);
    MutableByteSpan s7(arr2, 2);
    EXPECT_TRUE(s6.data_equal(s7));
    EXPECT_TRUE(s7.data_equal(s6));

    uint8_t arr3[] = { 1, 2, 3 };
    MutableByteSpan s8(arr3);
    EXPECT_TRUE(arr3[1] == 2);
    s8.data()[1] = 3;
    EXPECT_TRUE(arr3[1] == 3);

    // Not mutable span on purpose, to test conversion.
    ByteSpan s9 = s8;
    EXPECT_TRUE(s9.data_equal(s8));
    EXPECT_TRUE(s8.data_equal(s9));

    // Not mutable span on purpose.
    ByteSpan s10(s8);
    EXPECT_TRUE(s10.data_equal(s8));
    EXPECT_TRUE(s8.data_equal(s10));
}

TEST(TestSpan, TestFixedByteSpan)
{
    uint8_t arr[] = { 1, 2, 3 };

    FixedByteSpan<3> s0 = FixedByteSpan<3>();
    EXPECT_TRUE(s0.data() != nullptr);
    EXPECT_TRUE(s0.size() == 3);
    EXPECT_TRUE(s0.data_equal(s0));
    EXPECT_TRUE(s0[0] == 0);
    EXPECT_TRUE(s0[1] == 0);
    EXPECT_TRUE(s0[2] == 0);

    FixedByteSpan<2> s1(arr);
    EXPECT_TRUE(s1.data() == arr);
    EXPECT_TRUE(s1.size() == 2);
    EXPECT_TRUE(s1.data_equal(s1));

    FixedByteSpan<3> s2(arr);
    EXPECT_TRUE(s2.data() == arr);
    EXPECT_TRUE(s2.size() == 3);
    EXPECT_TRUE(s2.data()[2] == 3);
    EXPECT_TRUE(s2.data_equal(s2));
    EXPECT_TRUE(s2.front() == 1);
    EXPECT_TRUE(s2.back() == 3);
    EXPECT_TRUE(s2[0] == 1);
    EXPECT_TRUE(s2[1] == 2);
    EXPECT_TRUE(s2[2] == 3);

    FixedByteSpan<3> s3 = s2;
    EXPECT_TRUE(s3.data() == arr);
    EXPECT_TRUE(s3.size() == 3);
    EXPECT_TRUE(s3.data()[2] == 3);
    EXPECT_TRUE(s3.data_equal(s2));

    uint8_t arr2[] = { 3, 2, 1 };
    FixedSpan<uint8_t, 3> s4(arr2);
    EXPECT_TRUE(!s4.data_equal(s2));

    size_t idx = 0;
    for (auto & entry : s4)
    {
        EXPECT_TRUE(entry == arr2[idx++]);
    }
    EXPECT_TRUE(idx == 3);

    FixedByteSpan<3> s5(arr2);
    EXPECT_TRUE(s5.data_equal(s4));
    EXPECT_TRUE(s4.data_equal(s5));

    FixedByteSpan<2> s6(s4);
    idx = 0;
    for (auto & entry : s6)
    {
        EXPECT_TRUE(entry == arr2[idx++]);
    }
    EXPECT_TRUE(idx == 2);

    // Not fixed, to test conversion.
    ByteSpan s7(s4);
    EXPECT_TRUE(s7.data_equal(s4));
    EXPECT_TRUE(s4.data_equal(s7));

    MutableByteSpan s8(s4);
    EXPECT_TRUE(s8.data_equal(s4));
    EXPECT_TRUE(s4.data_equal(s8));
}

TEST(TestSpan, TestSpanOfPointers)
{
    uint8_t x        = 5;
    uint8_t * ptrs[] = { &x, &x };
    Span<uint8_t *> s1(ptrs);
    Span<uint8_t * const> s2(s1);
    EXPECT_TRUE(s1.data_equal(s2));
    EXPECT_TRUE(s2.data_equal(s1));

    FixedSpan<uint8_t *, 2> s3(ptrs);
    FixedSpan<uint8_t * const, 2> s4(s3);
    EXPECT_TRUE(s1.data_equal(s3));
    EXPECT_TRUE(s3.data_equal(s1));

    EXPECT_TRUE(s2.data_equal(s3));
    EXPECT_TRUE(s3.data_equal(s2));

    EXPECT_TRUE(s1.data_equal(s4));
    EXPECT_TRUE(s4.data_equal(s1));

    EXPECT_TRUE(s2.data_equal(s4));
    EXPECT_TRUE(s4.data_equal(s2));

    EXPECT_TRUE(s3.data_equal(s4));
    EXPECT_TRUE(s4.data_equal(s3));

    Span<uint8_t *> s5(s3);
    EXPECT_TRUE(s5.data_equal(s3));
    EXPECT_TRUE(s3.data_equal(s5));
}

TEST(TestSpan, TestSubSpan)
{
    uint8_t array[16];
    ByteSpan span(array);

    EXPECT_TRUE(span.data() == &array[0]);
    EXPECT_TRUE(span.size() == 16);

    ByteSpan subspan = span.SubSpan(1, 14);
    EXPECT_TRUE(subspan.data() == &array[1]);
    EXPECT_TRUE(subspan.size() == 14);

    subspan = span.SubSpan(1, 0);
    EXPECT_TRUE(subspan.size() == 0);

    subspan = span.SubSpan(10);
    EXPECT_TRUE(subspan.data() == &array[10]);
    EXPECT_TRUE(subspan.size() == 6);

    subspan = span.SubSpan(16);
    EXPECT_TRUE(subspan.size() == 0);
}

TEST(TestSpan, TestFromZclString)
{
    // Purposefully larger size than data.
    constexpr uint8_t array[16] = { 3, 0x41, 0x63, 0x45 };

    constexpr char str[] = "AcE";

    ByteSpan s1 = ByteSpan::fromZclString(array);
    EXPECT_TRUE(s1.data_equal(ByteSpan(&array[1], 3)));

    CharSpan s2 = CharSpan::fromZclString(array);
    EXPECT_TRUE(s2.data_equal(CharSpan(str, 3)));
}

TEST(TestSpan, TestFromCharString)
{
    constexpr char str[] = "AcE";

    CharSpan s1 = CharSpan::fromCharString(str);
    EXPECT_TRUE(s1.data_equal(CharSpan(str, 3)));
}

TEST(TestSpan, TestConversionConstructors)
{
    struct Foo
    {
        int member = 0;
    };
    struct Bar : public Foo
    {
    };

    Bar objects[2];

    // Check that various things here compile.
    Span<Foo> span1(objects);
    Span<Foo> span2(&objects[0], 1);
    FixedSpan<Foo, 2> span3(objects);
    FixedSpan<Foo, 1> span4(objects);

    Span<Bar> testSpan1(objects);
    FixedSpan<Bar, 2> testSpan2(objects);

    Span<Foo> span5(testSpan1);
    Span<Foo> span6(testSpan2);

    FixedSpan<Foo, 2> span7(testSpan2);
}
