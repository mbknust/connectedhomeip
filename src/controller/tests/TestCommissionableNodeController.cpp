/*
 *
 *    Copyright (c) 2020-2022 Project CHIP Authors
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

#include <controller/CHIPCommissionableNodeController.h>
#include <gtest/gtest.h>
#include <lib/support/CHIPMemString.h>

using namespace chip;
using namespace chip::Dnssd;
using namespace chip::Controller;

namespace chip {
namespace Dnssd {
namespace {

class MockResolver : public Resolver
{
public:
    CHIP_ERROR Init(chip::Inet::EndPointManager<chip::Inet::UDPEndPoint> * udpEndPointManager) override { return InitStatus; }
    bool IsInitialized() override { return true; }
    void Shutdown() override {}
    void SetOperationalDelegate(OperationalResolveDelegate * delegate) override {}
    void SetCommissioningDelegate(CommissioningResolveDelegate * delegate) override {}
    CHIP_ERROR ResolveNodeId(const PeerId & peerId) override { return ResolveNodeIdStatus; }
    void NodeIdResolutionNoLongerNeeded(const PeerId & peerId) override {}
    CHIP_ERROR DiscoverCommissioners(DiscoveryFilter filter = DiscoveryFilter()) override { return DiscoverCommissionersStatus; }
    CHIP_ERROR DiscoverCommissionableNodes(DiscoveryFilter filter = DiscoveryFilter()) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR StopDiscovery() override { return CHIP_ERROR_NOT_IMPLEMENTED; }
    CHIP_ERROR ReconfirmRecord(const char * hostname, Inet::IPAddress address, Inet::InterfaceId interfaceId) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR InitStatus                  = CHIP_NO_ERROR;
    CHIP_ERROR ResolveNodeIdStatus         = CHIP_NO_ERROR;
    CHIP_ERROR DiscoverCommissionersStatus = CHIP_NO_ERROR;
};

} // namespace
} // namespace Dnssd
} // namespace chip

namespace {

class TestCommissionableNodeController : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

#if INET_CONFIG_ENABLE_IPV4
TEST_F(TestCommissionableNodeController, TestGetDiscoveredCommissioner_HappyCase)
{
    MockResolver resolver;
    CommissionableNodeController controller(&resolver);
    chip::Dnssd::DiscoveredNodeData inNodeData;
    Platform::CopyString(inNodeData.resolutionData.hostName, "mockHostName");
    Inet::IPAddress::FromString("192.168.1.10", inNodeData.resolutionData.ipAddress[0]);
    inNodeData.resolutionData.numIPs++;
    inNodeData.resolutionData.port = 5540;

    controller.OnNodeDiscovered(inNodeData);

    EXPECT_TRUE(controller.GetDiscoveredCommissioner(0) != nullptr);
    EXPECT_TRUE(strcmp(inNodeData.resolutionData.hostName, controller.GetDiscoveredCommissioner(0)->resolutionData.hostName) == 0);
    EXPECT_TRUE(inNodeData.resolutionData.ipAddress[0] == controller.GetDiscoveredCommissioner(0)->resolutionData.ipAddress[0]);
    EXPECT_TRUE(controller.GetDiscoveredCommissioner(0)->resolutionData.port == 5540);
    EXPECT_TRUE(controller.GetDiscoveredCommissioner(0)->resolutionData.numIPs == 1);
}

TEST_F(TestCommissionableNodeController, TestGetDiscoveredCommissioner_InvalidNodeDiscovered_ReturnsNullptr)
{
    MockResolver resolver;
    CommissionableNodeController controller(&resolver);
    chip::Dnssd::DiscoveredNodeData inNodeData;
    Inet::IPAddress::FromString("192.168.1.10", inNodeData.resolutionData.ipAddress[0]);
    inNodeData.resolutionData.numIPs++;
    inNodeData.resolutionData.port = 5540;

    controller.OnNodeDiscovered(inNodeData);

    for (int i = 0; i < CHIP_DEVICE_CONFIG_MAX_DISCOVERED_NODES; i++)
    {
        EXPECT_TRUE(controller.GetDiscoveredCommissioner(i) == nullptr);
    }
}

TEST_F(TestCommissionableNodeController, TestGetDiscoveredCommissioner_HappyCase_OneValidOneInvalidNode)
{
    MockResolver resolver;
    CommissionableNodeController controller(&resolver);
    chip::Dnssd::DiscoveredNodeData invalidNodeData, validNodeData;
    Inet::IPAddress::FromString("192.168.1.10", invalidNodeData.resolutionData.ipAddress[0]);
    invalidNodeData.resolutionData.numIPs++;
    invalidNodeData.resolutionData.port = 5540;

    Platform::CopyString(validNodeData.resolutionData.hostName, "mockHostName2");
    Inet::IPAddress::FromString("192.168.1.11", validNodeData.resolutionData.ipAddress[0]);
    validNodeData.resolutionData.numIPs++;
    validNodeData.resolutionData.port = 5540;

    controller.OnNodeDiscovered(validNodeData);
    controller.OnNodeDiscovered(invalidNodeData);

    EXPECT_TRUE(controller.GetDiscoveredCommissioner(0) != nullptr);
    EXPECT_TRUE(strcmp(validNodeData.resolutionData.hostName, controller.GetDiscoveredCommissioner(0)->resolutionData.hostName) ==
                0);
    EXPECT_TRUE(validNodeData.resolutionData.ipAddress[0] == controller.GetDiscoveredCommissioner(0)->resolutionData.ipAddress[0]);
    EXPECT_TRUE(controller.GetDiscoveredCommissioner(0)->resolutionData.port == 5540);
    EXPECT_TRUE(controller.GetDiscoveredCommissioner(0)->resolutionData.numIPs == 1);

    EXPECT_TRUE(controller.GetDiscoveredCommissioner(1) == nullptr);
}

#endif // INET_CONFIG_ENABLE_IPV4

TEST_F(TestCommissionableNodeController, TestGetDiscoveredCommissioner_NoNodesDiscovered_ReturnsNullptr)
{
    MockResolver resolver;
    CommissionableNodeController controller(&resolver);

    for (int i = 0; i < CHIP_DEVICE_CONFIG_MAX_DISCOVERED_NODES; i++)
    {
        EXPECT_TRUE(controller.GetDiscoveredCommissioner(i) == nullptr);
    }
}

TEST_F(TestCommissionableNodeController, TestDiscoverCommissioners_HappyCase)
{
    MockResolver resolver;
    CommissionableNodeController controller(&resolver);
    EXPECT_TRUE(controller.DiscoverCommissioners() == CHIP_NO_ERROR);
}

TEST_F(TestCommissionableNodeController, TestDiscoverCommissioners_HappyCaseWithDiscoveryFilter)
{
    MockResolver resolver;
    CommissionableNodeController controller(&resolver);
    EXPECT_TRUE(controller.DiscoverCommissioners(Dnssd::DiscoveryFilter(Dnssd::DiscoveryFilterType::kDeviceType, 35)) ==
                CHIP_NO_ERROR);
}

TEST_F(TestCommissionableNodeController, TestDiscoverCommissioners_InitError_ReturnsError)
{
    MockResolver resolver;
    resolver.InitStatus = CHIP_ERROR_INTERNAL;
    CommissionableNodeController controller(&resolver);
    EXPECT_TRUE(controller.DiscoverCommissioners() != CHIP_NO_ERROR);
}

TEST_F(TestCommissionableNodeController, TestDiscoverCommissioners_DiscoverCommissionersError_ReturnsError)
{
    MockResolver resolver;
    resolver.DiscoverCommissionersStatus = CHIP_ERROR_INTERNAL;
    CommissionableNodeController controller(&resolver);
    EXPECT_TRUE(controller.DiscoverCommissioners() != CHIP_NO_ERROR);
} // clang-format on

} // namespace