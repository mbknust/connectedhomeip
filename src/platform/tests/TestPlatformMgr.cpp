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

/**
 *    @file
 *      This file implements a unit test suite for the Platform Manager
 *      code functionality.
 *
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include <gtest/gtest.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>

#include <lib/support/UnitTestUtils.h>

#include <platform/CHIPDeviceLayer.h>

using namespace chip;
using namespace chip::Logging;
using namespace chip::Inet;
using namespace chip::DeviceLayer;

class TestPlatformMgr : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

// =================================
//      Unit tests
// =================================

TEST_F(TestPlatformMgr, InitShutdown)
{
    CHIP_ERROR err = PlatformMgr().InitChipStack();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    PlatformMgr().Shutdown();
}

TEST_F(TestPlatformMgr, BasicEventLoopTask)
{
    std::atomic<int> counterRun{ 0 };

    CHIP_ERROR err = PlatformMgr().InitChipStack();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Start/stop the event loop task a few times.
    for (size_t i = 0; i < 3; i++)
    {
        err = PlatformMgr().StartEventLoopTask();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        std::atomic<int> counterSync{ 2 };

        // Verify that the event loop will not exit until we tell it to by
        // scheduling few lambdas (for the test to pass, the event loop will
        // have to process more than one event).
        DeviceLayer::SystemLayer().ScheduleLambda([&]() {
            counterRun++;
            counterSync--;
        });

        // Sleep for a short time to allow the event loop to process the
        // scheduled event and go to idle state. Without this sleep, the
        // event loop may process both scheduled lambdas during single
        // iteration of the event loop which would defeat the purpose of
        // this test on POSIX platforms where the event loop is implemented
        // using a "do { ... } while (shouldRun)" construct.
        chip::test_utils::SleepMillis(10);

        DeviceLayer::SystemLayer().ScheduleLambda([&]() {
            counterRun++;
            counterSync--;
        });

        // Wait for the event loop to process the scheduled events.
        // Note, that we can not use any synchronization primitives like
        // condition variables or barriers, because the test has to compile
        // on all platforms. Instead we use a busy loop with a timeout.
        for (size_t t = 0; counterSync != 0 && t < 1000; t++)
            chip::test_utils::SleepMillis(1);

        err = PlatformMgr().StopEventLoopTask();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        // Sleep for a short time to allow the event loop to stop.
        // Note, in some platform implementations the event loop thread
        // is self-terminating. We need time to process the stopping event
        // inside event loop.
        chip::test_utils::SleepMillis(10);
    }

    EXPECT_TRUE(counterRun == (3 * 2));

    PlatformMgr().Shutdown();
}

static bool stopRan;

static void StopTheLoop(intptr_t)
{
    // Testing the return value here would involve multi-threaded access to the
    // nlTestSuite, and it's not clear whether that's OK.
    stopRan = true;
    PlatformMgr().StopEventLoopTask();
}

TEST_F(TestPlatformMgr, BasicRunEventLoop)
{
    stopRan = false;

    CHIP_ERROR err = PlatformMgr().InitChipStack();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    PlatformMgr().ScheduleWork(StopTheLoop);

    PlatformMgr().RunEventLoop();
    EXPECT_TRUE(stopRan);

    PlatformMgr().Shutdown();
}

static bool sleepRan;

static void SleepSome(intptr_t)
{
    chip::test_utils::SleepMillis(1000);
    sleepRan = true;
}

TEST_F(TestPlatformMgr, RunEventLoopTwoTasks)
{
    stopRan  = false;
    sleepRan = false;

    CHIP_ERROR err = PlatformMgr().InitChipStack();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    PlatformMgr().ScheduleWork(SleepSome);
    PlatformMgr().ScheduleWork(StopTheLoop);

    PlatformMgr().RunEventLoop();
    EXPECT_TRUE(stopRan);
    EXPECT_TRUE(sleepRan);

    PlatformMgr().Shutdown();
}

void StopAndSleep(intptr_t arg)
{
    // Ensure that we don't proceed after stopping until the sleep is done too.
    StopTheLoop(arg);
    SleepSome(arg);
}

TEST_F(TestPlatformMgr, RunEventLoopStopBeforeSleep)
{
    stopRan  = false;
    sleepRan = false;

    CHIP_ERROR err = PlatformMgr().InitChipStack();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    PlatformMgr().ScheduleWork(StopAndSleep);

    PlatformMgr().RunEventLoop();
    EXPECT_TRUE(stopRan);
    EXPECT_TRUE(sleepRan);

    PlatformMgr().Shutdown();
}

TEST_F(TestPlatformMgr, TryLockChipStack)
{
    bool locked = PlatformMgr().TryLockChipStack();
    if (locked)
        PlatformMgr().UnlockChipStack();
}

static int sEventRecieved = 0;

void DeviceEventHandler(const ChipDeviceEvent * event, intptr_t arg)
{
    // EXPECT_TRUE(arg == 12345);
    sEventRecieved++;
}

TEST_F(TestPlatformMgr, AddEventHandler)
{
    CHIP_ERROR error;
    sEventRecieved = 0;
    error          = PlatformMgr().AddEventHandler(DeviceEventHandler, 12345);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

#if 0
    while (sEventRecieved == 0)
    {
    }

    EXPECT_TRUE(sEventRecieved > 0);
#endif
}

class MockSystemLayer : public System::LayerImpl
{
public:
    CHIP_ERROR StartTimer(System::Clock::Timeout aDelay, System::TimerCompleteCallback aComplete, void * aAppState) override
    {
        return CHIP_APPLICATION_ERROR(1);
    }
    CHIP_ERROR ScheduleWork(System::TimerCompleteCallback aComplete, void * aAppState) override
    {
        return CHIP_APPLICATION_ERROR(2);
    }
};

TEST_F(TestPlatformMgr, MockSystemLayer)
{
    MockSystemLayer systemLayer;

    DeviceLayer::SetSystemLayerForTesting(&systemLayer);
    EXPECT_TRUE(&DeviceLayer::SystemLayer() == static_cast<chip::System::Layer *>(&systemLayer));

    CHIP_ERROR err = PlatformMgr().InitChipStack();
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(&DeviceLayer::SystemLayer() == static_cast<chip::System::Layer *>(&systemLayer));

    EXPECT_TRUE(DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::kZero, nullptr, nullptr) == CHIP_APPLICATION_ERROR(1));
    EXPECT_TRUE(DeviceLayer::SystemLayer().ScheduleWork(nullptr, nullptr) == CHIP_APPLICATION_ERROR(2));

    PlatformMgr().Shutdown();

    DeviceLayer::SetSystemLayerForTesting(nullptr);
}
