/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2016-2017 Nest Labs, Inc.
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
 *      Unit tests for the Chip Persisted Storage API.
 *
 */

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <map>
#include <string>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <CHIPVersion.h>
#include <lib/support/Base64.h>
#include <lib/support/CHIPArgParser.hpp>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/DefaultStorageKeyAllocator.h>
#include <lib/support/PersistedCounter.h>
#include <lib/support/TestPersistentStorageDelegate.h>
#include <lib/support/UnitTestContext.h>

#include <platform/ConfigurationManager.h>
#include <platform/PersistedStorage.h>

namespace {

chip::TestPersistentStorageDelegate * sPersistentStore = nullptr;

} // namespace

struct TestPersistedCounterContext
{
    TestPersistedCounterContext();
    bool mVerbose;
};

TestPersistedCounterContext::TestPersistedCounterContext() : mVerbose(false) {}

class TestPersistedCounter : public ::testing::Test
{
public:
    static void TearDownTestSuite()
    {
        if (sPersistentStore != nullptr)
        {
            delete sPersistentStore;
            sPersistentStore = nullptr;
        }
    }
    static TestPersistedCounterContext ctx;
};

TestPersistedCounterContext TestPersistedCounter::ctx;

static void InitializePersistedStorage()
{
    if (sPersistentStore != nullptr)
    {
        delete sPersistentStore;
    }

    sPersistentStore = new chip::TestPersistentStorageDelegate;
}

TEST_F(TestPersistedCounter, CheckOOB)
{
    InitializePersistedStorage();

    // When initializing the first time out of the box, we should have
    // a count of 0 and a value of 0x10000 for the next starting value
    // in persistent storage.

    chip::PersistedCounter<uint64_t> counter;

    CHIP_ERROR err = counter.Init(sPersistentStore, chip::DefaultStorageKeyAllocator::IMEventNumber(), 0x10000);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    auto value = counter.GetValue();
    EXPECT_TRUE(value == 0);
}

TEST_F(TestPersistedCounter, CheckReboot)
{
    InitializePersistedStorage();

    chip::PersistedCounter<uint64_t> counter, counter2;

    // When initializing the first time out of the box, we should have
    // a count of 0.

    CHIP_ERROR err = counter.Init(sPersistentStore, chip::DefaultStorageKeyAllocator::IMEventNumber(), 0x10000);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    auto value = counter.GetValue();
    EXPECT_TRUE(value == 0);

    // Now we "reboot", and we should get a count of 0x10000.

    err = counter2.Init(sPersistentStore, chip::DefaultStorageKeyAllocator::IMEventNumber(), 0x10000);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    value = counter2.GetValue();
    EXPECT_TRUE(value == 0x10000);
}

TEST_F(TestPersistedCounter, CheckWriteNextCounterStart)
{
    InitializePersistedStorage();

    chip::PersistedCounter<uint64_t> counter;

    // When initializing the first time out of the box, we should have
    // a count of 0.

    CHIP_ERROR err = counter.Init(sPersistentStore, chip::DefaultStorageKeyAllocator::IMEventNumber(), 0x10000);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    auto value = counter.GetValue();
    EXPECT_TRUE(value == 0);

    // Verify that we write out the next starting counter value after
    // we've exhausted the counter's range.

    for (int32_t i = 0; i < 0x10000; i++)
    {
        err = counter.Advance();
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }

    value = counter.GetValue();
    EXPECT_TRUE(value == 0x10000);

    for (int32_t i = 0; i < 0x10000; i++)
    {
        err = counter.Advance();
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }

    value = counter.GetValue();
    EXPECT_TRUE(value == 0x20000);
}
