/*
 *
 *    Copyright (c) 2020-2021 Project CHIP Authors
 *    Copyright (c) 2016-2017 Nest Labs, Inc.
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
 *      This is a unit test suite for <tt>chip::System::Timer</tt>,
 *      the part of the CHIP System Layer that implements timers.
 *
 */

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <system/SystemConfig.h>

#include <gtest/gtest.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ErrorStr.h>
#include <lib/support/UnitTestContext.h>

#include <system/SystemError.h>
#include <system/SystemLayerImpl.h>

#if CHIP_SYSTEM_CONFIG_USE_LWIP
#include <lwip/init.h>
#include <lwip/sys.h>
#include <lwip/tcpip.h>
#endif // CHIP_SYSTEM_CONFIG_USE_LWIP

#include <errno.h>
#include <stdint.h>
#include <string.h>

using chip::ErrorStr;
using namespace chip::System;

namespace chip {
namespace System {

template <class LayerImpl, typename Enable = void>
class LayerEvents
{
public:
    static bool HasServiceEvents() { return false; }
    static void ServiceEvents(Layer & aLayer) {}
};

#if CHIP_SYSTEM_CONFIG_USE_SOCKETS || CHIP_SYSTEM_CONFIG_USE_NETWORK_FRAMEWORK

template <class LayerImpl>
class LayerEvents<LayerImpl, typename std::enable_if<std::is_base_of<LayerSocketsLoop, LayerImpl>::value>::type>
{
public:
    static bool HasServiceEvents() { return true; }
    static void ServiceEvents(Layer & aLayer)
    {
        LayerSocketsLoop & layer = static_cast<LayerSocketsLoop &>(aLayer);
        layer.PrepareEvents();
        layer.WaitForEvents();
        layer.HandleEvents();
    }
};

#endif // CHIP_SYSTEM_CONFIG_USE_SOCKETS || CHIP_SYSTEM_CONFIG_USE_NETWORK_FRAMEWORK

#if CHIP_SYSTEM_CONFIG_USE_LWIP

template <class LayerImpl>
class LayerEvents<LayerImpl, typename std::enable_if<std::is_base_of<LayerImplFreeRTOS, LayerImpl>::value>::type>
{
public:
    static bool HasServiceEvents() { return true; }
    static void ServiceEvents(Layer & aLayer)
    {
        LayerImplFreeRTOS & layer = static_cast<LayerImplFreeRTOS &>(aLayer);
        if (layer.IsInitialized())
        {
            layer.HandlePlatformTimer();
        }
    }
};

#endif // CHIP_SYSTEM_CONFIG_USE_LWIP

// Test input vector format.
static const uint32_t MAX_NUM_TIMERS = 1000;

class TestContext
{
public:
    Layer * mLayer;
    chip::Callback::Callback<> mGreedyTimer; // for greedy timer
    uint32_t mNumTimersHandled;

    void GreedyTimer()
    {
        EXPECT_TRUE(mNumTimersHandled < MAX_NUM_TIMERS);

        if (mNumTimersHandled >= MAX_NUM_TIMERS)
        {
            return;
        }

        mNumTimersHandled++;
    }
    static void GreedyTimer(void * p)
    {
        TestContext * lContext = static_cast<TestContext *>(p);
        lContext->GreedyTimer();
    }

    TestContext() : mGreedyTimer(GreedyTimer, this), mNumTimersHandled(0) {}
};

static TestContext * gCurrentTestContext = nullptr;

static int TestSetup(void * aContext);
static int TestTeardown(void * aContext);

class TestSystemTimer : public ::testing::Test
{
public:
    static void SetUpTestSuite() { TestSetup(&ctx); }
    static void TearDownTestSuite() { TestTeardown(&ctx); }
    static TestContext ctx;

    static void CheckTimerPool();
};

TestContext TestSystemTimer::ctx;

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name) { test_fixture::test_name(); }                                                                 \
    void test_fixture::test_name()

class ScopedGlobalTestContext
{
public:
    ScopedGlobalTestContext(TestContext * ctx) { gCurrentTestContext = ctx; }
    ~ScopedGlobalTestContext() { gCurrentTestContext = nullptr; }
};

// Test input data.

static volatile bool sOverflowTestDone;

void TimerFailed(void * aState)
{
    ASSERT_TRUE(false);
    sOverflowTestDone = true;
}

void HandleTimerFailed(Layer * systemLayer, void * aState)
{
    (void) systemLayer;
    TimerFailed(aState);
}

void HandleTimer10Success(Layer * systemLayer, void * aState)
{
    sOverflowTestDone = true;
}

TEST_F(TestSystemTimer, CheckOverflow)
{
    if (!LayerEvents<LayerImpl>::HasServiceEvents())
        return;

    chip::System::Clock::Milliseconds32 timeout_overflow_0ms = chip::System::Clock::Milliseconds32(652835029);
    chip::System::Clock::Milliseconds32 timeout_10ms         = chip::System::Clock::Milliseconds32(10);

    TestContext & lContext = TestSystemTimer::ctx;
    Layer & lSys           = *lContext.mLayer;

    sOverflowTestDone = false;

    lSys.StartTimer(timeout_overflow_0ms, HandleTimerFailed, &lContext);
    lSys.StartTimer(timeout_10ms, HandleTimer10Success, &lContext);

    while (!sOverflowTestDone)
    {
        LayerEvents<LayerImpl>::ServiceEvents(lSys);
    }

    lSys.CancelTimer(HandleTimerFailed, &lContext);
    // cb  timer is cancelled by destructor
    lSys.CancelTimer(HandleTimer10Success, &lContext);
}

void HandleGreedyTimer(Layer * aLayer, void * aState)
{
    static uint32_t sNumTimersHandled = 0;
    ASSERT_TRUE(sNumTimersHandled < MAX_NUM_TIMERS);

    if (sNumTimersHandled >= MAX_NUM_TIMERS)
    {
        return;
    }

    aLayer->StartTimer(chip::System::Clock::kZero, HandleGreedyTimer, aState);
    sNumTimersHandled++;
}

TEST_F(TestSystemTimer, CheckStarvation)
{
    if (!LayerEvents<LayerImpl>::HasServiceEvents())
        return;

    TestContext & lContext = TestSystemTimer::ctx;
    Layer & lSys           = *lContext.mLayer;

    lSys.StartTimer(chip::System::Clock::kZero, HandleGreedyTimer, &lContext);

    LayerEvents<LayerImpl>::ServiceEvents(lSys);
}

TEST_F(TestSystemTimer, CheckOrder)
{
    if (!LayerEvents<LayerImpl>::HasServiceEvents())
        return;

    TestContext & testContext = TestSystemTimer::ctx;
    Layer & systemLayer       = *testContext.mLayer;

    struct TestState
    {
        void Record(char c)
        {
            size_t n = strlen(record);
            if (n + 1 < sizeof(record))
            {
                record[n++] = c;
                record[n]   = 0;
            }
        }
        static void A(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('A'); }
        static void B(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('B'); }
        static void C(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('C'); }
        static void D(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('D'); }
        char record[5] = { 0 };
    };
    TestState testState;
    EXPECT_TRUE(testState.record[0] == 0);

    Clock::ClockBase * const savedClock = &SystemClock();
    Clock::Internal::MockClock mockClock;
    Clock::Internal::SetSystemClockForTesting(&mockClock);

    using namespace Clock::Literals;
    systemLayer.StartTimer(300_ms, TestState::D, &testState);
    systemLayer.StartTimer(100_ms, TestState::B, &testState);
    systemLayer.StartTimer(200_ms, TestState::C, &testState);
    systemLayer.StartTimer(0_ms, TestState::A, &testState);

    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "A") == 0);

    mockClock.AdvanceMonotonic(100_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "AB") == 0);

    mockClock.AdvanceMonotonic(200_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "ABCD") == 0);

    Clock::Internal::SetSystemClockForTesting(savedClock);
}

TEST_F(TestSystemTimer, CheckCancellation)
{
    if (!LayerEvents<LayerImpl>::HasServiceEvents())
        return;

    TestContext & testContext = TestSystemTimer::ctx;
    Layer & systemLayer       = *testContext.mLayer;

    struct TestState
    {
        TestState(Layer & aSystemLayer) : mSystemLayer(aSystemLayer) {}

        void Record(char c)
        {
            size_t n = strlen(record);
            if (n + 1 < sizeof(record))
            {
                record[n++] = c;
                record[n]   = 0;
            }
        }
        static void A(Layer * layer, void * state)
        {
            auto self = static_cast<TestState *>(state);
            self->Record('A');
            self->mSystemLayer.CancelTimer(B, state);
            self->mSystemLayer.CancelTimer(D, state);
        }
        static void B(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('B'); }
        static void C(Layer * layer, void * state)
        {
            auto self = static_cast<TestState *>(state);
            self->Record('C');
            self->mSystemLayer.CancelTimer(E, state);
        }
        static void D(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('D'); }
        static void E(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('E'); }
        char record[6] = { 0 };

        Layer & mSystemLayer;
    };
    TestState testState(systemLayer);
    EXPECT_TRUE(testState.record[0] == 0);

    Clock::ClockBase * const savedClock = &SystemClock();
    Clock::Internal::MockClock mockClock;
    Clock::Internal::SetSystemClockForTesting(&mockClock);

    using namespace Clock::Literals;
    systemLayer.StartTimer(0_ms, TestState::A, &testState);
    systemLayer.StartTimer(0_ms, TestState::B, &testState);
    systemLayer.StartTimer(20_ms, TestState::C, &testState);
    systemLayer.StartTimer(30_ms, TestState::D, &testState);
    systemLayer.StartTimer(50_ms, TestState::E, &testState);

    mockClock.AdvanceMonotonic(100_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "AC") == 0);

    Clock::Internal::SetSystemClockForTesting(savedClock);
}

// A bit lower than maximum system timers just in case, for systems that
// have some form of limit
constexpr unsigned kCancelTimerCount = CHIP_SYSTEM_CONFIG_NUM_TIMERS - 4;
int gCallbackProcessed[kCancelTimerCount];

/// Validates that gCallbackProcessed has valid values (0 or 1)
void ValidateExecutedTimerCounts()
{
    for (int processed : gCallbackProcessed)
    {
        EXPECT_TRUE((processed == 0) || (processed == 1));
    }
}

unsigned ExecutedTimerCount()
{
    unsigned count = 0;
    for (int processed : gCallbackProcessed)
    {
        if (processed != 0)
        {
            count++;
        }
    }
    return count;
}

void Callback(Layer * layer, void * state)
{
    unsigned idx = static_cast<unsigned>(reinterpret_cast<uintptr_t>(state));
    if (gCallbackProcessed[idx] != 0)
    {
        ChipLogError(Test, "UNEXPECTED EXECUTION at index %u", idx);
    }

    gCallbackProcessed[idx]++;

    if (ExecutedTimerCount() == kCancelTimerCount / 2)
    {
        ChipLogProgress(Test, "Cancelling timers");
        for (unsigned i = 0; i < kCancelTimerCount; i++)
        {
            if (gCallbackProcessed[i] != 0)
            {
                continue;
            }
            ChipLogProgress(Test, "Timer %u is being cancelled", i);
            gCurrentTestContext->mLayer->CancelTimer(Callback, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
            gCallbackProcessed[i]++; // pretend executed.
        }
    }
}

TEST_F(TestSystemTimer, Test)
{
    // Validates that timers can cancel other timers. Generally the test will
    // do the following:
    //   - schedule several timers to start at the same time
    //   - within each timers, after half of them have run, make one timer
    //     cancel all the other ones
    //   - assert that:
    //       - timers will run if scheduled
    //       - once cancelled, timers will NOT run (i.e. a timer can cancel
    //         other timers, even if they are expiring at the same time)
    memset(gCallbackProcessed, 0, sizeof(gCallbackProcessed));

    TestContext & testContext = TestSystemTimer::ctx;
    ScopedGlobalTestContext testScope(&testContext);

    Layer & systemLayer = *testContext.mLayer;

    Clock::ClockBase * const savedClock = &SystemClock();
    Clock::Internal::MockClock mockClock;
    Clock::Internal::SetSystemClockForTesting(&mockClock);
    using namespace Clock::Literals;

    for (unsigned i = 0; i < kCancelTimerCount; i++)
    {
        EXPECT_TRUE(systemLayer.StartTimer(10_ms, Callback, reinterpret_cast<void *>(static_cast<uintptr_t>(i))) == CHIP_NO_ERROR);
    }

    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    ValidateExecutedTimerCounts();
    EXPECT_TRUE(ExecutedTimerCount() == 0);

    mockClock.AdvanceMonotonic(20_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);

    ValidateExecutedTimerCounts();
    EXPECT_TRUE(ExecutedTimerCount() == kCancelTimerCount);

    Clock::Internal::SetSystemClockForTesting(savedClock);
}

// Test the implementation helper classes TimerPool, TimerList, and TimerData.
STATIC_TEST(TestSystemTimer, CheckTimerPool)
{
    TestContext & testContext = TestSystemTimer::ctx;
    Layer & systemLayer       = *testContext.mLayer;

    using Timer = TimerList::Node;
    struct TestState
    {
        int count = 0;
        static void Increment(Layer * layer, void * state) { ++static_cast<TestState *>(state)->count; }
        static void Reset(Layer * layer, void * state) { static_cast<TestState *>(state)->count = 0; }
    };
    TestState testState;

    using namespace Clock::Literals;
    struct
    {
        Clock::Timestamp awakenTime;
        TimerCompleteCallback onComplete;
        Timer * timer;
    } testTimer[] = {
        { 111_ms, TestState::Increment }, // 0
        { 100_ms, TestState::Increment }, // 1
        { 202_ms, TestState::Reset },     // 2
        { 303_ms, TestState::Increment }, // 3
    };

    TimerPool<Timer> pool;
    EXPECT_TRUE(pool.mTimerPool.Allocated() == 0);
    SYSTEM_STATS_RESET(Stats::kSystemLayer_NumTimers);
    SYSTEM_STATS_RESET_HIGH_WATER_MARK_FOR_TESTING(Stats::kSystemLayer_NumTimers);
    EXPECT_TRUE(SYSTEM_STATS_TEST_IN_USE(Stats::kSystemLayer_NumTimers, 0));
    EXPECT_TRUE(SYSTEM_STATS_TEST_HIGH_WATER_MARK(Stats::kSystemLayer_NumTimers, 0));

    // Test TimerPool::Create() and TimerData accessors.

    for (auto & timer : testTimer)
    {
        timer.timer = pool.Create(systemLayer, timer.awakenTime, timer.onComplete, &testState);
    }
    EXPECT_TRUE(SYSTEM_STATS_TEST_IN_USE(Stats::kSystemLayer_NumTimers, 4));

    for (auto & timer : testTimer)
    {
        EXPECT_TRUE(timer.timer != nullptr);
        EXPECT_TRUE(timer.timer->AwakenTime() == timer.awakenTime);
        EXPECT_TRUE(timer.timer->GetCallback().GetOnComplete() == timer.onComplete);
        EXPECT_TRUE(timer.timer->GetCallback().GetAppState() == &testState);
        EXPECT_TRUE(timer.timer->GetCallback().GetSystemLayer() == &systemLayer);
    }

    // Test TimerList operations.

    TimerList list;
    EXPECT_TRUE(list.Remove(nullptr) == nullptr);
    EXPECT_TRUE(list.Remove(nullptr, nullptr) == nullptr);
    EXPECT_TRUE(list.PopEarliest() == nullptr);
    EXPECT_TRUE(list.PopIfEarlier(500_ms) == nullptr);
    EXPECT_TRUE(list.Earliest() == nullptr);
    EXPECT_TRUE(list.Empty());

    Timer * earliest = list.Add(testTimer[0].timer); // list: () → (0) returns: 0
    EXPECT_TRUE(earliest == testTimer[0].timer);
    EXPECT_TRUE(list.PopIfEarlier(10_ms) == nullptr);
    EXPECT_TRUE(list.Earliest() == testTimer[0].timer);
    EXPECT_TRUE(!list.Empty());

    earliest = list.Add(testTimer[1].timer); // list: (0) → (1 0) returns: 1
    EXPECT_TRUE(earliest == testTimer[1].timer);
    EXPECT_TRUE(list.Earliest() == testTimer[1].timer);

    earliest = list.Add(testTimer[2].timer); // list: (1 0) → (1 0 2) returns: 1
    EXPECT_TRUE(earliest == testTimer[1].timer);
    EXPECT_TRUE(list.Earliest() == testTimer[1].timer);

    earliest = list.Add(testTimer[3].timer); // list: (1 0 2) → (1 0 2 3) returns: 1
    EXPECT_TRUE(earliest == testTimer[1].timer);
    EXPECT_TRUE(list.Earliest() == testTimer[1].timer);

    earliest = list.Remove(earliest); // list: (1 0 2 3) → (0 2 3) returns: 0
    EXPECT_TRUE(earliest == testTimer[0].timer);
    EXPECT_TRUE(list.Earliest() == testTimer[0].timer);

    earliest = list.Remove(TestState::Reset, &testState); // list: (0 2 3) → (0 3) returns: 2
    EXPECT_TRUE(earliest == testTimer[2].timer);
    EXPECT_TRUE(list.Earliest() == testTimer[0].timer);

    earliest = list.PopEarliest(); // list: (0 3) → (3) returns: 0
    EXPECT_TRUE(earliest == testTimer[0].timer);
    EXPECT_TRUE(list.Earliest() == testTimer[3].timer);

    earliest = list.PopIfEarlier(10_ms); // list: (3) → (3) returns: nullptr
    EXPECT_TRUE(earliest == nullptr);

    earliest = list.PopIfEarlier(500_ms); // list: (3) → () returns: 3
    EXPECT_TRUE(earliest == testTimer[3].timer);
    EXPECT_TRUE(list.Empty());

    earliest = list.Add(testTimer[3].timer); // list: () → (3) returns: 3
    list.Clear();                            // list: (3) → ()
    EXPECT_TRUE(earliest == testTimer[3].timer);
    EXPECT_TRUE(list.Empty());

    for (auto & timer : testTimer)
    {
        list.Add(timer.timer);
    }
    TimerList early = list.ExtractEarlier(200_ms); // list: (1 0 2 3) → (2 3) returns: (1 0)
    EXPECT_TRUE(list.PopEarliest() == testTimer[2].timer);
    EXPECT_TRUE(list.PopEarliest() == testTimer[3].timer);
    EXPECT_TRUE(list.PopEarliest() == nullptr);
    EXPECT_TRUE(early.PopEarliest() == testTimer[1].timer);
    EXPECT_TRUE(early.PopEarliest() == testTimer[0].timer);
    EXPECT_TRUE(early.PopEarliest() == nullptr);

    // Test TimerPool::Invoke()
    EXPECT_TRUE(testState.count == 0);
    pool.Invoke(testTimer[0].timer);
    testTimer[0].timer = nullptr;
    EXPECT_TRUE(testState.count == 1);
    EXPECT_TRUE(pool.mTimerPool.Allocated() == 3);
    EXPECT_TRUE(SYSTEM_STATS_TEST_IN_USE(Stats::kSystemLayer_NumTimers, 3));

    // Test TimerPool::Release()
    pool.Release(testTimer[1].timer);
    testTimer[1].timer = nullptr;
    EXPECT_TRUE(testState.count == 1);
    EXPECT_TRUE(pool.mTimerPool.Allocated() == 2);
    EXPECT_TRUE(SYSTEM_STATS_TEST_IN_USE(Stats::kSystemLayer_NumTimers, 2));

    pool.ReleaseAll();
    EXPECT_TRUE(pool.mTimerPool.Allocated() == 0);
    EXPECT_TRUE(SYSTEM_STATS_TEST_IN_USE(Stats::kSystemLayer_NumTimers, 0));
    EXPECT_TRUE(SYSTEM_STATS_TEST_HIGH_WATER_MARK(Stats::kSystemLayer_NumTimers, 4));
}

TEST_F(TestSystemTimer, ExtendTimerToTest)
{
    if (!LayerEvents<LayerImpl>::HasServiceEvents())
        return;

    TestContext & testContext = TestSystemTimer::ctx;
    Layer & systemLayer       = *testContext.mLayer;

    struct TestState
    {
        void Record(char c)
        {
            size_t n = strlen(record);
            if (n + 1 < sizeof(record))
            {
                record[n++] = c;
                record[n]   = 0;
            }
        }
        static void A(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('A'); }
        static void B(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('B'); }
        static void C(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('C'); }
        static void D(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('D'); }
        char record[5] = { 0 };
    };
    TestState testState;
    EXPECT_TRUE(testState.record[0] == 0);

    Clock::ClockBase * const savedClock = &SystemClock();
    Clock::Internal::MockClock mockClock;
    Clock::Internal::SetSystemClockForTesting(&mockClock);

    using namespace Clock::Literals;
    systemLayer.StartTimer(150_ms, TestState::B, &testState);
    systemLayer.StartTimer(200_ms, TestState::C, &testState);
    systemLayer.StartTimer(150_ms, TestState::D, &testState);

    // Timer wasn't started before. ExtendTimerTo will start it.
    systemLayer.ExtendTimerTo(100_ms, TestState::A, &testState);
    mockClock.AdvanceMonotonic(100_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "A") == 0);

    // Timer B as 50ms remaining. ExtendTimerTo 25 should have no effect
    // Timer C as 100ms remaining. ExtendTimerTo 75ms should have no effect
    // Timer D as 50ms remaining. Timer should be extend to a duration of 75ms
    systemLayer.ExtendTimerTo(25_ms, TestState::B, &testState);
    systemLayer.ExtendTimerTo(75_ms, TestState::D, &testState);
    systemLayer.ExtendTimerTo(75_ms, TestState::D, &testState);

    mockClock.AdvanceMonotonic(25_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "A") == 0);

    mockClock.AdvanceMonotonic(25_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "AB") == 0);

    // Timer D as 25ms remaining. Timer should be extend to a duration of 75ms
    systemLayer.ExtendTimerTo(75_ms, TestState::D, &testState);
    mockClock.AdvanceMonotonic(100_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(strcmp(testState.record, "ABCD") == 0);

    Clock::Internal::SetSystemClockForTesting(savedClock);

    // Extending a timer by 0 ms permitted
    EXPECT_TRUE(systemLayer.ExtendTimerTo(0_ms, TestState::A, &testState) == CHIP_ERROR_INVALID_ARGUMENT);
}

TEST_F(TestSystemTimer, IsTimerActiveTest)
{
    if (!LayerEvents<LayerImpl>::HasServiceEvents())
        return;

    TestContext & testContext = TestSystemTimer::ctx;
    Layer & systemLayer       = *testContext.mLayer;

    struct TestState
    {
        void Record(char c)
        {
            size_t n = strlen(record);
            if (n + 1 < sizeof(record))
            {
                record[n++] = c;
                record[n]   = 0;
            }
        }
        static void A(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('A'); }
        static void B(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('B'); }
        static void C(Layer * layer, void * state) { static_cast<TestState *>(state)->Record('C'); }
        char record[4] = { 0 };
    };
    TestState testState;
    EXPECT_TRUE(testState.record[0] == 0);

    Clock::ClockBase * const savedClock = &SystemClock();
    Clock::Internal::MockClock mockClock;
    Clock::Internal::SetSystemClockForTesting(&mockClock);

    using namespace Clock::Literals;
    systemLayer.StartTimer(100_ms, TestState::A, &testState);
    systemLayer.StartTimer(200_ms, TestState::B, &testState);
    systemLayer.StartTimer(300_ms, TestState::C, &testState);

    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::A, &testState));
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::B, &testState));
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::C, &testState));

    mockClock.AdvanceMonotonic(100_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::A, &testState) == false);
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::B, &testState));
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::C, &testState));

    mockClock.AdvanceMonotonic(100_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::B, &testState) == false);
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::C, &testState));

    mockClock.AdvanceMonotonic(100_ms);
    LayerEvents<LayerImpl>::ServiceEvents(systemLayer);
    EXPECT_TRUE(systemLayer.IsTimerActive(TestState::C, &testState) == false);

    Clock::Internal::SetSystemClockForTesting(savedClock);
}

static LayerImpl sLayer;

/**
 *  Set up the test suite.
 */
static int TestSetup(void * aContext)
{
    TestContext & lContext = *reinterpret_cast<TestContext *>(aContext);

    if (::chip::Platform::MemoryInit() != CHIP_NO_ERROR)
    {
        return FAILURE;
    }

#if CHIP_SYSTEM_CONFIG_USE_LWIP && (LWIP_VERSION_MAJOR == 2) && (LWIP_VERSION_MINOR == 0) && !(CHIP_SYSTEM_CONFIG_LWIP_SKIP_INIT)
    static sys_mbox_t * sLwIPEventQueue = NULL;

    sys_mbox_new(sLwIPEventQueue, 100);
    tcpip_init(NULL, NULL);
#endif // CHIP_SYSTEM_CONFIG_USE_LWIP && (LWIP_VERSION_MAJOR == 2) && (LWIP_VERSION_MINOR == 0) &&
       // !(CHIP_SYSTEM_CONFIG_LWIP_SKIP_INIT)

    sLayer.Init();

    lContext.mLayer = &sLayer;

    return (SUCCESS);
}

/**
 *  Tear down the test suite.
 *  Free memory reserved at TestSetup.
 */
static int TestTeardown(void * aContext)
{
    TestContext & lContext = *reinterpret_cast<TestContext *>(aContext);

    lContext.mLayer->Shutdown();

#if CHIP_SYSTEM_CONFIG_USE_LWIP && (LWIP_VERSION_MAJOR == 2) && (LWIP_VERSION_MINOR == 0) && !(CHIP_SYSTEM_CONFIG_LWIP_SKIP_INIT)
    tcpip_finish(NULL, NULL);
#endif // CHIP_SYSTEM_CONFIG_USE_LWIP && (LWIP_VERSION_MAJOR == 2) && (LWIP_VERSION_MINOR == 0) &&
       // !(CHIP_SYSTEM_CONFIG_LWIP_SKIP_INIT)

    ::chip::Platform::MemoryShutdown();
    return (SUCCESS);
}

} // namespace System
} // namespace chip