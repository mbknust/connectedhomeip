/*
 *
 *    Copyright (c) 2023 Project CHIP Authors
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
#include <app/EventManagement.h>
#include <app/icd/ICDManagementServer.h>
#include <app/icd/ICDManager.h>
#include <app/icd/ICDNotifier.h>
#include <app/icd/ICDStateObserver.h>
#include <app/tests/AppTestContext.h>
#include <gtest/gtest.h>
#include <lib/support/TestPersistentStorageDelegate.h>
#include <lib/support/TimeUtils.h>
#include <lib/support/UnitTestContext.h>
#include <lib/support/UnitTestRegistration.h>
#include <nlunit-test.h>

#include <system/SystemLayerImpl.h>

#include <crypto/DefaultSessionKeystore.h>

using namespace chip;
using namespace chip::app;
using namespace chip::System;

using TestSessionKeystoreImpl = Crypto::DefaultSessionKeystore;

namespace {

// Test Values
constexpr uint16_t kMaxTestClients      = 2;
constexpr FabricIndex kTestFabricIndex1 = 1;
constexpr FabricIndex kTestFabricIndex2 = kMaxValidFabricIndex;
constexpr uint64_t kClientNodeId11      = 0x100001;
constexpr uint64_t kClientNodeId12      = 0x100002;
constexpr uint64_t kClientNodeId21      = 0x200001;
constexpr uint64_t kClientNodeId22      = 0x200002;

constexpr uint8_t kKeyBuffer1a[] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};
constexpr uint8_t kKeyBuffer1b[] = {
    0xf1, 0xe1, 0xd1, 0xc1, 0xb1, 0xa1, 0x91, 0x81, 0x71, 0x61, 0x51, 0x14, 0x31, 0x21, 0x11, 0x01
};
constexpr uint8_t kKeyBuffer2a[] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
};
constexpr uint8_t kKeyBuffer2b[] = {
    0xf2, 0xe2, 0xd2, 0xc2, 0xb2, 0xa2, 0x92, 0x82, 0x72, 0x62, 0x52, 0x42, 0x32, 0x22, 0x12, 0x02
};

class TestICDStateObserver : public app::ICDStateObserver
{
public:
    void OnEnterActiveMode() {}
    void OnTransitionToIdle() {}
};

TestICDStateObserver mICDStateObserver;
static Clock::Internal::MockClock gMockClock;
static Clock::ClockBase * gRealClock;

class TestContext : public Test::AppContext
{
public:
    static int Initialize(void * context)
    {
        if (AppContext::Initialize(context) != SUCCESS)
            return FAILURE;

        auto * ctx = static_cast<TestContext *>(context);
        DeviceLayer::SetSystemLayerForTesting(&ctx->GetSystemLayer());

        gRealClock = &SystemClock();
        Clock::Internal::SetSystemClockForTesting(&gMockClock);

        if (ctx->mEventCounter.Init(0) != CHIP_NO_ERROR)
        {
            return FAILURE;
        }
        ctx->mICDManager.Init(&ctx->testStorage, &ctx->GetFabricTable(), &mICDStateObserver, &(ctx->mKeystore));
        return SUCCESS;
    }

    static int Finalize(void * context)
    {
        auto * ctx = static_cast<TestContext *>(context);
        ctx->mICDManager.Shutdown();
        app::EventManagement::DestroyEventManagement();
        System::Clock::Internal::SetSystemClockForTesting(gRealClock);
        DeviceLayer::SetSystemLayerForTesting(nullptr);

        if (AppContext::Finalize(context) != SUCCESS)
            return FAILURE;

        return SUCCESS;
    }

    TestSessionKeystoreImpl mKeystore;
    app::ICDManager mICDManager;
    TestPersistentStorageDelegate testStorage;

private:
    MonotonicallyIncreasingCounter<EventNumber> mEventCounter;
};

} // namespace

namespace chip {
namespace app {
class TestICDManager : public ::testing::Test
{
public:
    static TestContext ctx;
    static void SetUpTestSuite() { TestContext::Initialize(&ctx); }
    static void TearDownTestSuite() { TestContext::Finalize(&ctx); }

    /*
     * Advance the test Mock clock time by the amout passed in argument
     * and then force the SystemLayer Timer event loop. It will check for any expired timer,
     * and invoke their callbacks if there are any.
     *
     * @param time_ms: Value in milliseconds.
     */
    static void AdvanceClockAndRunEventLoop(uint32_t time_ms)
    {
        gMockClock.AdvanceMonotonic(System::Clock::Timeout(time_ms));
        ctx.GetIOContext().DriveIO();
    }

    static void TestICDModeDurations();
    static void TestKeepActivemodeRequests();
    static void TestICDMRegisterUnregisterEvents();
};

TestContext TestICDManager::ctx;

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name) { test_fixture::test_name(); }                                                                 \
    void test_fixture::test_name()

STATIC_TEST(TestICDManager, TestICDModeDurations)
{
    // After the init we should be in active mode
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeDurationMs() + 1);
    // Active mode interval expired, ICDManager transitioned to the IdleMode.
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::IdleMode);
    AdvanceClockAndRunEventLoop(secondsToMilliseconds(ICDManagementServer::GetInstance().GetIdleModeDurationSec()) + 1);
    // Idle mode interval expired, ICDManager transitioned to the ActiveMode.
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);

    // Events updating the Operation to Active mode can extend the current active mode time by 1 Active mode threshold.
    // Kick an active Threshold just before the end of the Active interval and validate that the active mode is extended.
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeDurationMs() - 1);
    ICDNotifier::GetInstance().BroadcastNetworkActivityNotification();
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeThresholdMs() / 2);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeThresholdMs());
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::IdleMode);
}

STATIC_TEST(TestICDManager, TestKeepActivemodeRequests)
{
    typedef ICDListener::KeepActiveFlags ActiveFlag;
    ICDNotifier notifier = ICDNotifier::GetInstance();

    // Setting a requirement will transition the ICD to active mode.
    notifier.BroadcastActiveRequestNotification(ActiveFlag::kCommissioningWindowOpen);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);
    // Advance time so active mode interval expires.
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeDurationMs() + 1);
    // Requirement flag still set. We stay in active mode
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);

    // Remove requirement. we should directly transition to idle mode.
    notifier.BroadcastActiveRequestWithdrawal(ActiveFlag::kCommissioningWindowOpen);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::IdleMode);

    notifier.BroadcastActiveRequestNotification(ActiveFlag::kFailSafeArmed);
    // Requirement will transition us to active mode.
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);

    // Advance time, but by less than the active mode interval and remove the requirement.
    // We should stay in active mode.
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeDurationMs() / 2);
    notifier.BroadcastActiveRequestWithdrawal(ActiveFlag::kFailSafeArmed);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);

    // Advance time again, The activemode interval is completed.
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeDurationMs() + 1);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::IdleMode);

    // Set two requirements
    notifier.BroadcastActiveRequestNotification(ActiveFlag::kFailSafeArmed);
    notifier.BroadcastActiveRequestNotification(ActiveFlag::kExchangeContextOpen);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);
    // advance time so the active mode interval expires.
    AdvanceClockAndRunEventLoop(ICDManagementServer::GetInstance().GetActiveModeDurationMs() + 1);
    // A requirement flag is still set. We stay in active mode.
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);

    // remove 1 requirement. Active mode is maintained
    notifier.BroadcastActiveRequestWithdrawal(ActiveFlag::kFailSafeArmed);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::ActiveMode);
    // remove the last requirement
    notifier.BroadcastActiveRequestWithdrawal(ActiveFlag::kExchangeContextOpen);
    EXPECT_TRUE(ctx.mICDManager.mOperationalState == ICDManager::OperationalState::IdleMode);
}

/*
 * Test that verifies that the ICDManager is the correct operating mode based on entries
 * in the ICDMonitoringTable
 */
STATIC_TEST(TestICDManager, TestICDMRegisterUnregisterEvents)
{
    typedef ICDListener::ICDManagementEvents ICDMEvent;
    ICDNotifier notifier = ICDNotifier::GetInstance();

    // Set FeatureMap
    // Configures CIP, UAT and LITS to 1
    ctx.mICDManager.SetTestFeatureMapValue(0x07);

    // Check ICDManager starts in SIT mode if no entries are present
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::SIT);

    // Trigger a "fake" register, ICDManager shoudl remain in SIT mode
    notifier.BroadcastICDManagementEvent(ICDMEvent::kTableUpdated);

    // Check ICDManager stayed in SIT mode
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::SIT);

    // Create tables with different fabrics
    ICDMonitoringTable table1(ctx.testStorage, kTestFabricIndex1, kMaxTestClients, &(ctx.mKeystore));
    ICDMonitoringTable table2(ctx.testStorage, kTestFabricIndex2, kMaxTestClients, &(ctx.mKeystore));

    // Add first entry to the first fabric
    ICDMonitoringEntry entry1(&(ctx.mKeystore));
    entry1.checkInNodeID    = kClientNodeId11;
    entry1.monitoredSubject = kClientNodeId12;
    EXPECT_TRUE(CHIP_NO_ERROR == entry1.SetKey(ByteSpan(kKeyBuffer1a)));
    EXPECT_TRUE(CHIP_NO_ERROR == table1.Set(0, entry1));

    // Trigger register event after first entry was added
    notifier.BroadcastICDManagementEvent(ICDMEvent::kTableUpdated);

    // Check ICDManager is now in the LIT operating mode
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::LIT);

    // Add second entry to the first fabric
    ICDMonitoringEntry entry2(&(ctx.mKeystore));
    entry2.checkInNodeID    = kClientNodeId12;
    entry2.monitoredSubject = kClientNodeId11;
    EXPECT_TRUE(CHIP_NO_ERROR == entry2.SetKey(ByteSpan(kKeyBuffer1b)));
    EXPECT_TRUE(CHIP_NO_ERROR == table1.Set(1, entry2));

    // Trigger register event after first entry was added
    notifier.BroadcastICDManagementEvent(ICDMEvent::kTableUpdated);

    // Check ICDManager is now in the LIT operating mode
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::LIT);

    // Add first entry to the first fabric
    ICDMonitoringEntry entry3(&(ctx.mKeystore));
    entry3.checkInNodeID    = kClientNodeId21;
    entry3.monitoredSubject = kClientNodeId22;
    EXPECT_TRUE(CHIP_NO_ERROR == entry3.SetKey(ByteSpan(kKeyBuffer2a)));
    EXPECT_TRUE(CHIP_NO_ERROR == table2.Set(0, entry3));

    // Trigger register event after first entry was added
    notifier.BroadcastICDManagementEvent(ICDMEvent::kTableUpdated);

    // Check ICDManager is now in the LIT operating mode
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::LIT);

    // Add second entry to the first fabric
    ICDMonitoringEntry entry4(&(ctx.mKeystore));
    entry4.checkInNodeID    = kClientNodeId22;
    entry4.monitoredSubject = kClientNodeId21;
    EXPECT_TRUE(CHIP_NO_ERROR == entry4.SetKey(ByteSpan(kKeyBuffer2b)));
    EXPECT_TRUE(CHIP_NO_ERROR == table2.Set(1, entry4));

    // Clear a fabric
    EXPECT_TRUE(CHIP_NO_ERROR == table2.RemoveAll());

    // Trigger register event after fabric was cleared
    notifier.BroadcastICDManagementEvent(ICDMEvent::kTableUpdated);

    // Check ICDManager is still in the LIT operating mode
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::LIT);

    // Remove single entry from remaining fabric
    EXPECT_TRUE(CHIP_NO_ERROR == table1.Remove(1));

    // Trigger register event after fabric was cleared
    notifier.BroadcastICDManagementEvent(ICDMEvent::kTableUpdated);

    // Check ICDManager is still in the LIT operating mode
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::LIT);

    // Remove last entry from remaining fabric
    EXPECT_TRUE(CHIP_NO_ERROR == table1.Remove(0));
    EXPECT_TRUE(table1.IsEmpty());
    EXPECT_TRUE(table2.IsEmpty());

    // Trigger register event after fabric was cleared
    notifier.BroadcastICDManagementEvent(ICDMEvent::kTableUpdated);

    // Check ICDManager is still in the LIT operating mode
    EXPECT_TRUE(ctx.mICDManager.GetICDMode() == ICDManager::ICDMode::SIT);
}

} // namespace app
} // namespace chip
