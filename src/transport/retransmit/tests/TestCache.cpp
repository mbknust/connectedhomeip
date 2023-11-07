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

#include <transport/retransmit/Cache.h>

#include <bitset>
#include <gtest/gtest.h>

// Helpers for simple payload management
namespace {
constexpr int kMaxPayloadValue = 100;

/**
 * Derived cache class containing some test helper methods.
 */
template <typename KeyType, typename PayloadType, size_t N>
class TestableCache : public chip::Retransmit::Cache<KeyType, PayloadType, N>
{
public:
    /**
     * Convenience add when types are trivially copyable, so no actual
     * reference needs to be created.
     */
    template <typename = std::enable_if<std::is_trivially_copyable<PayloadType>::value, int>>
    CHIP_ERROR AddValue(const KeyType & key, PayloadType payload)
    {
        return chip::Retransmit::Cache<KeyType, PayloadType, N>::Add(key, payload);
    }
};

class IntPayloadTracker
{
public:
    void Acquire(int value)
    {
        EXPECT_TRUE((value > 0) && value < kMaxPayloadValue);
        mAcquired.set(static_cast<size_t>(value));
    }

    void Release(int value)
    {
        EXPECT_TRUE((value > 0) && value < kMaxPayloadValue);
        EXPECT_TRUE(mAcquired.test(static_cast<size_t>(value)));
        mAcquired.reset(static_cast<size_t>(value));
    }

    size_t Count() const { return mAcquired.count(); }

    bool IsAcquired(int value) const { return mAcquired.test(static_cast<size_t>(value)); }

private:
    std::bitset<kMaxPayloadValue> mAcquired;
};

IntPayloadTracker gPayloadTracker;

/**
 * Helper class defining a matches method for things divisible by a
 * specific value.
 */
class DivisibleBy
{
public:
    DivisibleBy(int value) : mValue(value) {}

    bool Matches(int x) const { return (x % mValue) == 0; }

private:
    const int mValue;
};

} // namespace

template <>
int chip::Retransmit::Lifetime<int>::Acquire(int & value)
{
    gPayloadTracker.Acquire(value);
    return value;
}

template <>
void chip::Retransmit::Lifetime<int>::Release(int & value)
{
    gPayloadTracker.Release(value);
    value = 0; // make sure it is not used anymore
}

namespace {

TEST(TestCache, TestNoOp)
{
    // unused address cache should not do any Acquire/release at any time
    EXPECT_TRUE(gPayloadTracker.Count() == 0);
    {
        TestableCache<int, int, 20> test;
        EXPECT_TRUE(gPayloadTracker.Count() == 0);
    }
    EXPECT_TRUE(gPayloadTracker.Count() == 0);
}

TEST(TestCache, TestDestructorFree)
{
    {
        TestableCache<int, int, 20> test;

        EXPECT_TRUE(gPayloadTracker.Count() == 0);

        EXPECT_TRUE(test.AddValue(1, 1) == CHIP_NO_ERROR);
        EXPECT_TRUE(test.AddValue(2, 2) == CHIP_NO_ERROR);

        EXPECT_TRUE(gPayloadTracker.Count() == 2);
    }

    // destructor should release the items
    EXPECT_TRUE(gPayloadTracker.Count() == 0);
}

TEST(TestCache, OutOfSpace)
{
    {
        TestableCache<int, int, 4> test;

        EXPECT_TRUE(gPayloadTracker.Count() == 0);

        EXPECT_TRUE(test.AddValue(1, 1) == CHIP_NO_ERROR);
        EXPECT_TRUE(test.AddValue(2, 2) == CHIP_NO_ERROR);
        EXPECT_TRUE(test.AddValue(3, 4) == CHIP_NO_ERROR);
        EXPECT_TRUE(test.AddValue(4, 6) == CHIP_NO_ERROR);
        EXPECT_TRUE(gPayloadTracker.Count() == 4);

        EXPECT_TRUE(test.AddValue(5, 8) == CHIP_ERROR_NO_MEMORY);
        EXPECT_TRUE(gPayloadTracker.Count() == 4);

        EXPECT_TRUE(test.AddValue(6, 10) == CHIP_ERROR_NO_MEMORY);
        EXPECT_TRUE(gPayloadTracker.Count() == 4);
    }
    EXPECT_TRUE(gPayloadTracker.Count() == 0);
}

TEST(TestCache, AddRemove)
{
    TestableCache<int, int, 3> test;

    EXPECT_TRUE(gPayloadTracker.Count() == 0);

    EXPECT_TRUE(test.AddValue(1, 1) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(2, 2) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(3, 4) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 3);

    EXPECT_TRUE(test.AddValue(10, 8) == CHIP_ERROR_NO_MEMORY);
    EXPECT_TRUE(gPayloadTracker.Count() == 3);

    EXPECT_TRUE(test.Remove(2) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 2);

    EXPECT_TRUE(test.AddValue(10, 8) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 3);

    EXPECT_TRUE(test.Remove(14) == CHIP_ERROR_KEY_NOT_FOUND);
    EXPECT_TRUE(gPayloadTracker.Count() == 3);

    EXPECT_TRUE(test.Remove(1) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 2);

    EXPECT_TRUE(test.Remove(3) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 1);

    EXPECT_TRUE(test.Remove(3) == CHIP_ERROR_KEY_NOT_FOUND);
    EXPECT_TRUE(gPayloadTracker.Count() == 1);

    EXPECT_TRUE(test.Remove(10) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 0);

    EXPECT_TRUE(test.Remove(10) == CHIP_ERROR_KEY_NOT_FOUND);
    EXPECT_TRUE(gPayloadTracker.Count() == 0);
}

TEST(TestCache, RemoveMatching)
{
    TestableCache<int, int, 4> test;

    EXPECT_TRUE(gPayloadTracker.Count() == 0);

    EXPECT_TRUE(test.AddValue(1, 1) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(2, 2) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(3, 4) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(4, 8) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 4);

    test.RemoveMatching(DivisibleBy(2));
    EXPECT_TRUE(gPayloadTracker.Count() == 2);

    // keys 1 and 3 remain
    EXPECT_TRUE(gPayloadTracker.IsAcquired(1));
    EXPECT_TRUE(gPayloadTracker.IsAcquired(4));

    EXPECT_TRUE(test.Remove(3) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.IsAcquired(1));
    EXPECT_TRUE(!gPayloadTracker.IsAcquired(4));
}

TEST(TestCache, FindMatching)
{
    TestableCache<int, int, 4> test;

    EXPECT_TRUE(gPayloadTracker.Count() == 0);

    EXPECT_TRUE(test.AddValue(1, 1) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(2, 2) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(3, 4) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.AddValue(4, 8) == CHIP_NO_ERROR);
    EXPECT_TRUE(gPayloadTracker.Count() == 4);

    const int * key;
    const int * value;

    EXPECT_TRUE(test.Find(DivisibleBy(20), &key, &value) == false);
    EXPECT_TRUE(key == nullptr);
    EXPECT_TRUE(value == nullptr);

    // This relies on linear add. May need changing if implementation changes
    EXPECT_TRUE(test.Find(DivisibleBy(2), &key, &value) == true);
    EXPECT_TRUE(*key == 2);
    EXPECT_TRUE(*value == 2);

    EXPECT_TRUE(test.Remove(*key) == CHIP_NO_ERROR);

    EXPECT_TRUE(test.Find(DivisibleBy(2), &key, &value) == true);
    EXPECT_TRUE(*key == 4);
    EXPECT_TRUE(*value == 8);

    EXPECT_TRUE(test.Remove(*key) == CHIP_NO_ERROR);
    EXPECT_TRUE(test.Find(DivisibleBy(2), &key, &value) == false);
    EXPECT_TRUE(key == nullptr);
    EXPECT_TRUE(value == nullptr);
}

} // namespace
