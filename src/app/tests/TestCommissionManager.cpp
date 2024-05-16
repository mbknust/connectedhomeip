/*
 *
 *    Copyright (c) 2021-2022 Project CHIP Authors
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

#include <app/TestEventTriggerDelegate.h>
#include <app/TimerDelegates.h>
#include <app/reporting/ReportSchedulerImpl.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <crypto/RandUtils.h>
#include <lib/dnssd/Advertiser.h>
#include <lib/support/Span.h>

#include <messaging/tests/echo/common.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/ConfigurationManager.h>
#include <platform/PlatformManager.h>
#include <platform/TestOnlyCommissionableDataProvider.h>
#include <protocols/secure_channel/PASESession.h>

#include <gtest/gtest.h>

using namespace chip::Crypto;

using chip::CommissioningWindowAdvertisement;
using chip::CommissioningWindowManager;
using chip::Server;

// Mock function for linking
void InitDataModelHandler() {}

namespace {
bool sAdminFabricIndexDirty = false;
bool sAdminVendorIdDirty    = false;
bool sWindowStatusDirty     = false;

void ResetDirtyFlags()
{
    sAdminFabricIndexDirty = false;
    sAdminVendorIdDirty    = false;
    sWindowStatusDirty     = false;
}

} // namespace

void MatterReportingAttributeChangeCallback(chip::EndpointId endpoint, chip::ClusterId clusterId, chip::AttributeId attributeId)
{
    using namespace chip::app::Clusters;
    using namespace chip::app::Clusters::AdministratorCommissioning::Attributes;
    if (endpoint != chip::kRootEndpointId || clusterId != AdministratorCommissioning::Id)
    {
        return;
    }

    switch (attributeId)
    {
    case WindowStatus::Id:
        sWindowStatusDirty = true;
        break;
    case AdminVendorId::Id:
        sAdminVendorIdDirty = true;
        break;
    case AdminFabricIndex::Id:
        sAdminFabricIndexDirty = true;
        break;
    default:
        break;
    }
}

namespace chip::app {
bool IsDeviceTypeOnEndpoint(DeviceTypeId deviceType, EndpointId endpoint)
{
    return false;
}
} // namespace chip::app

namespace {

class TestCommissionManager : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        CHIP_ERROR err = CHIP_NO_ERROR;
        err            = chip::Platform::MemoryInit();
        EXPECT_EQ(err, CHIP_NO_ERROR);
        err = chip::DeviceLayer::PlatformMgr().InitChipStack();
        EXPECT_EQ(err, CHIP_NO_ERROR);

        static chip::DeviceLayer::TestOnlyCommissionableDataProvider commissionableDataProvider;
        chip::DeviceLayer::SetCommissionableDataProvider(&commissionableDataProvider);

        static chip::CommonCaseDeviceServerInitParams initParams;
        // Report scheduler and timer delegate instance
        static chip::app::DefaultTimerDelegate sTimerDelegate;
        static chip::app::reporting::ReportSchedulerImpl sReportScheduler(&sTimerDelegate);
        initParams.reportScheduler = &sReportScheduler;
        static chip::SimpleTestEventTriggerDelegate sSimpleTestEventTriggerDelegate;
        initParams.testEventTriggerDelegate = &sSimpleTestEventTriggerDelegate;
        (void) initParams.InitializeStaticResourcesBeforeServerInit();
        // Set a randomized server port(slightly shifted from CHIP_PORT) for testing
        initParams.operationalServicePort =
            static_cast<uint16_t>(initParams.operationalServicePort + chip::Crypto::GetRandU16() % 20);

        err = chip::Server::GetInstance().Init(initParams);

        EXPECT_EQ(err, CHIP_NO_ERROR);

        Server::GetInstance().GetCommissioningWindowManager().CloseCommissioningWindow();
        chip::DeviceLayer::PlatformMgr().StartEventLoopTask();
    }

    static void TearDownTestSuite()
    {
        // TODO: The platform memory was intentionally left not deinitialized so that minimal mdns can destruct
        chip::DeviceLayer::PlatformMgr().ScheduleWork(TearDownTask, 0);
        sleep(kTestTaskWaitSeconds);

        chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
        chip::DeviceLayer::PlatformMgr().Shutdown();

        auto & mdnsAdvertiser = chip::Dnssd::ServiceAdvertiser::Instance();
        mdnsAdvertiser.RemoveServices();
        mdnsAdvertiser.Shutdown();

        // Server shudown will be called in TearDownTask

        // TODO: At this point UDP endpoits still seem leaked and the sanitizer
        // builds will attempt a memory free. As a result, we keep Memory initialized
        // so that the global UDPManager can still be destructed without a coredump.
        //
        // This is likely either a missing shutdown or an actual UDP endpoint leak
        // which I have not been able to track down yet.
        //
        // chip::Platform::MemoryShutdown();
    }

    static void TearDownTask(intptr_t context) { chip::Server::GetInstance().Shutdown(); }

    static constexpr int kTestTaskWaitSeconds = 2;
};

void CheckCommissioningWindowManagerBasicWindowOpenCloseTask(intptr_t context)
{
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    CommissioningWindowManager & commissionMgr = Server::GetInstance().GetCommissioningWindowManager();
    CHIP_ERROR err                             = commissionMgr.OpenBasicCommissioningWindow(commissionMgr.MaxCommissioningTimeout(),
                                                                                            CommissioningWindowAdvertisement::kDnssdOnly);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    EXPECT_TRUE(commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kWindowNotOpen);
    EXPECT_TRUE(commissionMgr.GetOpenerFabricIndex().IsNull());
    EXPECT_TRUE(commissionMgr.GetOpenerVendorId().IsNull());
    EXPECT_TRUE(!chip::DeviceLayer::ConnectivityMgr().IsBLEAdvertisingEnabled());
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    commissionMgr.CloseCommissioningWindow();
    EXPECT_TRUE(!commissionMgr.IsCommissioningWindowOpen());
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);
}

TEST_F(TestCommissionManager, CheckCommissioningWindowManagerBasicWindowOpenClose)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CheckCommissioningWindowManagerBasicWindowOpenCloseTask, 0);
    sleep(kTestTaskWaitSeconds);
}

void CheckCommissioningWindowManagerBasicWindowOpenCloseFromClusterTask(intptr_t context)
{
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    CommissioningWindowManager & commissionMgr = Server::GetInstance().GetCommissioningWindowManager();
    constexpr auto fabricIndex                 = static_cast<chip::FabricIndex>(1);
    constexpr auto vendorId                    = static_cast<chip::VendorId>(0xFFF3);
    CHIP_ERROR err                             = commissionMgr.OpenBasicCommissioningWindowForAdministratorCommissioningCluster(
        commissionMgr.MaxCommissioningTimeout(), fabricIndex, vendorId);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    EXPECT_TRUE(commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kBasicWindowOpen);
    ASSERT_FALSE(commissionMgr.GetOpenerFabricIndex().IsNull());
    EXPECT_EQ(commissionMgr.GetOpenerFabricIndex().Value(), fabricIndex);
    ASSERT_FALSE(commissionMgr.GetOpenerVendorId().IsNull());
    EXPECT_EQ(commissionMgr.GetOpenerVendorId().Value(), vendorId);
    EXPECT_TRUE(!chip::DeviceLayer::ConnectivityMgr().IsBLEAdvertisingEnabled());
    EXPECT_TRUE(sWindowStatusDirty);
    EXPECT_TRUE(sAdminFabricIndexDirty);
    EXPECT_TRUE(sAdminVendorIdDirty);

    ResetDirtyFlags();
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    commissionMgr.CloseCommissioningWindow();
    EXPECT_TRUE(!commissionMgr.IsCommissioningWindowOpen());
    EXPECT_TRUE(commissionMgr.GetOpenerFabricIndex().IsNull());
    EXPECT_TRUE(commissionMgr.GetOpenerVendorId().IsNull());
    EXPECT_TRUE(sWindowStatusDirty);
    EXPECT_TRUE(sAdminFabricIndexDirty);
    EXPECT_TRUE(sAdminVendorIdDirty);

    ResetDirtyFlags();
}

TEST_F(TestCommissionManager, CheckCommissioningWindowManagerBasicWindowOpenCloseFromCluster)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CheckCommissioningWindowManagerBasicWindowOpenCloseFromClusterTask, 0);
    sleep(kTestTaskWaitSeconds);
}

void CheckCommissioningWindowManagerWindowClosedTask(chip::System::Layer *, void * context)
{
    CommissioningWindowManager & commissionMgr = Server::GetInstance().GetCommissioningWindowManager();
    EXPECT_TRUE(!commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kWindowNotOpen);
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);
}

void CheckCommissioningWindowManagerWindowTimeoutTask(intptr_t context)
{
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    CommissioningWindowManager & commissionMgr = Server::GetInstance().GetCommissioningWindowManager();
    constexpr auto kTimeoutSeconds             = chip::System::Clock::Seconds32(1);
    constexpr uint16_t kTimeoutMs              = 1000;
    constexpr unsigned kSleepPadding           = 100;
    commissionMgr.OverrideMinCommissioningTimeout(kTimeoutSeconds);
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds, CommissioningWindowAdvertisement::kDnssdOnly);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    EXPECT_TRUE(commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kWindowNotOpen);
    EXPECT_TRUE(!chip::DeviceLayer::ConnectivityMgr().IsBLEAdvertisingEnabled());
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(kTimeoutMs + kSleepPadding),
                                                CheckCommissioningWindowManagerWindowClosedTask, 0);
}

TEST_F(TestCommissionManager, CheckCommissioningWindowManagerWindowTimeout)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CheckCommissioningWindowManagerWindowTimeoutTask, 0);
    sleep(kTestTaskWaitSeconds);
}

void SimulateFailedSessionEstablishmentTask(chip::System::Layer *, void * context)
{
    CommissioningWindowManager & commissionMgr = Server::GetInstance().GetCommissioningWindowManager();
    EXPECT_TRUE(commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kWindowNotOpen);
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    commissionMgr.OnSessionEstablishmentStarted();
    commissionMgr.OnSessionEstablishmentError(CHIP_ERROR_INTERNAL);
    EXPECT_TRUE(commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kWindowNotOpen);
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);
}

void CheckCommissioningWindowManagerWindowTimeoutWithSessionEstablishmentErrorsTask(intptr_t context)
{
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    CommissioningWindowManager & commissionMgr = Server::GetInstance().GetCommissioningWindowManager();
    constexpr auto kTimeoutSeconds             = chip::System::Clock::Seconds16(1);
    constexpr uint16_t kTimeoutMs              = 1000;
    constexpr unsigned kSleepPadding           = 100;
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds, CommissioningWindowAdvertisement::kDnssdOnly);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    EXPECT_TRUE(commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kWindowNotOpen);
    EXPECT_TRUE(!chip::DeviceLayer::ConnectivityMgr().IsBLEAdvertisingEnabled());
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(kTimeoutMs + kSleepPadding),
                                                CheckCommissioningWindowManagerWindowClosedTask, 0);
    // Simulate a session establishment error during that window, such that the
    // delay for the error plus the window size exceeds our "timeout + padding" above.
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(kTimeoutMs / 4 * 3),
                                                SimulateFailedSessionEstablishmentTask, 0);
}

TEST_F(TestCommissionManager, CheckCommissioningWindowManagerWindowTimeoutWithSessionEstablishmentErrors)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CheckCommissioningWindowManagerWindowTimeoutWithSessionEstablishmentErrorsTask,
                                                  0);
    sleep(kTestTaskWaitSeconds);
}

void CheckCommissioningWindowManagerEnhancedWindowTask(intptr_t context)
{
    CommissioningWindowManager & commissionMgr = Server::GetInstance().GetCommissioningWindowManager();
    uint16_t originDiscriminator;
    CHIP_ERROR err = chip::DeviceLayer::GetCommissionableDataProvider()->GetSetupDiscriminator(originDiscriminator);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    uint16_t newDiscriminator = static_cast<uint16_t>(originDiscriminator + 1);
    Spake2pVerifier verifier;
    constexpr uint32_t kIterations = kSpake2p_Min_PBKDF_Iterations;
    uint8_t salt[kSpake2p_Min_PBKDF_Salt_Length];
    chip::ByteSpan saltData(salt);

    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    constexpr auto fabricIndex = static_cast<chip::FabricIndex>(1);
    constexpr auto vendorId    = static_cast<chip::VendorId>(0xFFF3);
    err = commissionMgr.OpenEnhancedCommissioningWindow(commissionMgr.MaxCommissioningTimeout(), newDiscriminator, verifier,
                                                        kIterations, saltData, fabricIndex, vendorId);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    EXPECT_TRUE(commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kEnhancedWindowOpen);
    EXPECT_TRUE(!chip::DeviceLayer::ConnectivityMgr().IsBLEAdvertisingEnabled());
    ASSERT_FALSE(commissionMgr.GetOpenerFabricIndex().IsNull());
    EXPECT_EQ(commissionMgr.GetOpenerFabricIndex().Value(), fabricIndex);
    ASSERT_FALSE(commissionMgr.GetOpenerVendorId().IsNull());
    EXPECT_EQ(commissionMgr.GetOpenerVendorId().Value(), vendorId);
    EXPECT_TRUE(sWindowStatusDirty);
    EXPECT_TRUE(sAdminFabricIndexDirty);
    EXPECT_TRUE(sAdminVendorIdDirty);

    ResetDirtyFlags();
    EXPECT_TRUE(!sWindowStatusDirty);
    EXPECT_TRUE(!sAdminFabricIndexDirty);
    EXPECT_TRUE(!sAdminVendorIdDirty);

    commissionMgr.CloseCommissioningWindow();
    EXPECT_TRUE(!commissionMgr.IsCommissioningWindowOpen());
    EXPECT_EQ(commissionMgr.CommissioningWindowStatusForCluster(),
              chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum::kWindowNotOpen);
    EXPECT_TRUE(commissionMgr.GetOpenerFabricIndex().IsNull());
    EXPECT_TRUE(commissionMgr.GetOpenerVendorId().IsNull());
    EXPECT_TRUE(sWindowStatusDirty);
    EXPECT_TRUE(sAdminFabricIndexDirty);
    EXPECT_TRUE(sAdminVendorIdDirty);

    ResetDirtyFlags();
}

TEST_F(TestCommissionManager, CheckCommissioningWindowManagerEnhancedWindow)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CheckCommissioningWindowManagerEnhancedWindowTask, 0);
    sleep(kTestTaskWaitSeconds);
}
} // namespace
