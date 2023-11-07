/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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
 *      This file implements a unit test suite for the Key Value Store Manager
 *      code functionality.
 *
 */

#include <gtest/gtest.h>

#include <lib/support/CHIPMem.h>

#include <platform/CHIPDeviceLayer.h>
#include <platform/KeyValueStoreManager.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::DeviceLayer::PersistedStorage;

class TestKeyValueStoreMgr : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

TEST_F(TestKeyValueStoreMgr, EmptyString)
{
    constexpr const char * kTestKey   = "str_key";
    constexpr const char kTestValue[] = "";
    constexpr size_t kTestValueLen    = 0;

    char readValue[sizeof(kTestValue)];
    size_t readSize;

    CHIP_ERROR err = KeyValueStoreMgr().Put(kTestKey, kTestValue, kTestValueLen);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Verify if read value is the same as wrote one
    err = KeyValueStoreMgr().Get(kTestKey, readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(readSize == kTestValueLen);

    // Verify that read succeeds even if 0-length buffer is provided
    err = KeyValueStoreMgr().Get(kTestKey, readValue, 0, &readSize);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(readSize == kTestValueLen);

    err = KeyValueStoreMgr().Get(kTestKey, nullptr, 0, &readSize);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(readSize == kTestValueLen);

    // Verify deletion
    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Try to get deleted key and verify if CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND is returned
    err = KeyValueStoreMgr().Get(kTestKey, readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
}

TEST_F(TestKeyValueStoreMgr, String)
{
    constexpr const char * kTestKey   = "str_key";
    constexpr const char kTestValue[] = "test_value";

    char readValue[sizeof(kTestValue)];
    size_t readSize;

    CHIP_ERROR err = KeyValueStoreMgr().Put(kTestKey, kTestValue);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Verify if read value is the same as wrote one
    err = KeyValueStoreMgr().Get(kTestKey, readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(strcmp(kTestValue, readValue) == 0);
    EXPECT_TRUE(readSize == sizeof(kTestValue));

    // Verify deletion
    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Try to get deleted key and verify if CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND is returned
    err = KeyValueStoreMgr().Get(kTestKey, readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
}

TEST_F(TestKeyValueStoreMgr, Uint32)
{
    constexpr const char * kTestKey     = "uint32_key";
    constexpr const uint32_t kTestValue = 5;

    uint32_t readValue = UINT32_MAX;

    CHIP_ERROR err = KeyValueStoreMgr().Put(kTestKey, kTestValue);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Verify if read value is the same as wrote one
    err = KeyValueStoreMgr().Get(kTestKey, &readValue);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(kTestValue == readValue);

    // Verify deletion
    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Try to get deleted key and verify if CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND is returned
    err = KeyValueStoreMgr().Get(kTestKey, &readValue);
    EXPECT_TRUE(err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
}

TEST_F(TestKeyValueStoreMgr, Array)
{
    constexpr const char * kTestKey  = "array_key";
    constexpr uint32_t kTestValue[5] = { 1, 2, 3, 4, 5 };

    uint32_t readValue[5];
    size_t readSize;

    CHIP_ERROR err = KeyValueStoreMgr().Put(kTestKey, kTestValue);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Verify if read value is the same as wrote one
    err = KeyValueStoreMgr().Get(kTestKey, readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(memcmp(kTestValue, readValue, sizeof(kTestValue)) == 0);
    EXPECT_TRUE(readSize == sizeof(kTestValue));

    // Verify deletion
    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Try to get deleted key and verify if CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND is returned
    err = KeyValueStoreMgr().Get(kTestKey, readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
}

TEST_F(TestKeyValueStoreMgr, Struct)
{
    struct TestStruct
    {
        uint8_t value1;
        uint32_t value2;
    };

    constexpr const char * kTestKey = "struct_key";
    constexpr TestStruct kTestValue{ 1, 2 };

    TestStruct readValue;
    size_t readSize;

    CHIP_ERROR err = KeyValueStoreMgr().Put(kTestKey, kTestValue);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Verify if read value is the same as wrote one
    err = KeyValueStoreMgr().Get(kTestKey, &readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(kTestValue.value1 == readValue.value1);
    EXPECT_TRUE(kTestValue.value2 == readValue.value2);
    EXPECT_TRUE(readSize == sizeof(kTestValue));

    // Verify deletion
    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Try to get deleted key and verify if CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND is returned
    err = KeyValueStoreMgr().Get(kTestKey, &readValue, sizeof(readValue), &readSize);
    EXPECT_TRUE(err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
}

TEST_F(TestKeyValueStoreMgr, UpdateValue)
{
    constexpr const char * kTestKey = "update_key";

    CHIP_ERROR err;
    uint32_t readValue;

    for (uint32_t i = 0; i < 10; i++)
    {
        err = KeyValueStoreMgr().Put(kTestKey, i);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = KeyValueStoreMgr().Get(kTestKey, &readValue);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        EXPECT_TRUE(i == readValue);
    }

    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

TEST_F(TestKeyValueStoreMgr, TooSmallBufferRead)
{
    constexpr const char * kTestKey = "too_small_buffer_read_key";
    constexpr uint8_t kTestValue[]  = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    uint8_t readValue[9];
    size_t readSize;

    CHIP_ERROR err = KeyValueStoreMgr().Put(kTestKey, kTestValue);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Returns buffer too small and should read as many bytes as possible
    err = KeyValueStoreMgr().Get(kTestKey, &readValue, sizeof(readValue), &readSize, 0);
    EXPECT_TRUE(err == CHIP_ERROR_BUFFER_TOO_SMALL);
    EXPECT_TRUE(readSize == sizeof(readValue));
    EXPECT_TRUE(memcmp(kTestValue, readValue, readSize) == 0);

    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

TEST_F(TestKeyValueStoreMgr, AllCharactersKey)
{
    // Test that all printable characters [0x20 - 0x7f) can be part of the key
    constexpr size_t kKeyLength   = 32;
    constexpr char kCharBegin     = 0x20;
    constexpr char kCharEnd       = 0x7f;
    constexpr uint32_t kTestValue = 5;

    char allChars[kCharEnd - kCharBegin];

    for (char character = kCharBegin; character < kCharEnd; character++)
    {
        allChars[character - kCharBegin] = character;
    }

    for (size_t charId = 0; charId < sizeof(allChars); charId += kKeyLength)
    {
        char testKey[kKeyLength + 1] = {};
        memcpy(testKey, &allChars[charId], chip::min(sizeof(allChars) - charId, kKeyLength));

        CHIP_ERROR err = KeyValueStoreMgr().Put(testKey, kTestValue);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        uint32_t readValue = UINT32_MAX;
        err                = KeyValueStoreMgr().Get(testKey, &readValue);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = KeyValueStoreMgr().Delete(testKey);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }
}

TEST_F(TestKeyValueStoreMgr, NonExistentDelete)
{
    constexpr const char * kTestKey = "non_existent";

    CHIP_ERROR err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
}

#if !defined(__ZEPHYR__) && !defined(__MBED__)
TEST_F(TestKeyValueStoreMgr, MultiRead)
{
    constexpr const char * kTestKey  = "multi_key";
    constexpr uint32_t kTestValue[5] = { 1, 2, 3, 4, 5 };

    CHIP_ERROR err = KeyValueStoreMgr().Put(kTestKey, kTestValue);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    for (uint32_t i = 0; i < 5; i++)
    {
        uint32_t readValue;
        size_t readSize;

        // Returns buffer too small for all but the last read.
        err = KeyValueStoreMgr().Get(kTestKey, &readValue, sizeof(readValue), &readSize, i * sizeof(uint32_t));
        EXPECT_TRUE(err == (i < 4 ? CHIP_ERROR_BUFFER_TOO_SMALL : CHIP_NO_ERROR));
        EXPECT_TRUE(readSize == sizeof(readValue));
        EXPECT_TRUE(kTestValue[i] == readValue);
    }

    err = KeyValueStoreMgr().Delete(kTestKey);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}
#endif

#ifdef __ZEPHYR__
TEST_F(TestKeyValueStoreMgr, DoFactoryReset)
{
    constexpr const char * kStrKey  = "string_with_weird_chars\\=_key";
    constexpr const char * kUintKey = "some_uint_key";

    EXPECT_TRUE(KeyValueStoreMgr().Put(kStrKey, "some_string") == CHIP_NO_ERROR);
    EXPECT_TRUE(KeyValueStoreMgr().Put(kUintKey, uint32_t(1234)) == CHIP_NO_ERROR);

    char readString[16];
    uint32_t readValue;

    EXPECT_TRUE(KeyValueStoreMgr().Get(kStrKey, readString, sizeof(readString)) == CHIP_NO_ERROR);
    EXPECT_TRUE(KeyValueStoreMgr().Get(kUintKey, &readValue) == CHIP_NO_ERROR);

    EXPECT_TRUE(KeyValueStoreMgrImpl().DoFactoryReset() == CHIP_NO_ERROR);
    EXPECT_TRUE(KeyValueStoreMgr().Get(kStrKey, readString, sizeof(readString)) == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
    EXPECT_TRUE(KeyValueStoreMgr().Get(kUintKey, &readValue) == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
}
#endif
