/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include "lib/support/CHIPMem.h"
#include <access/examples/PermissiveAccessControlDelegate.h>
#include <app/AttributeAccessInterface.h>
#include <app/ConcreteAttributePath.h>
#include <app/ConcreteEventPath.h>
#include <app/InteractionModelEngine.h>
#include <app/MessageDef/AttributeReportIBs.h>
#include <app/MessageDef/EventDataIB.h>
#include <app/reporting/tests/MockReportScheduler.h>
#include <app/tests/AppTestContext.h>
#include <app/util/basic-types.h>
#include <app/util/mock/Constants.h>
#include <app/util/mock/Functions.h>
#include <gtest/gtest.h>
#include <lib/core/CHIPCore.h>
#include <lib/core/ErrorStr.h>
#include <lib/core/TLV.h>
#include <lib/core/TLVDebug.h>
#include <lib/core/TLVUtilities.h>
#include <lib/support/CHIPCounter.h>
#include <lib/support/UnitTestContext.h>

#include <messaging/ExchangeContext.h>
#include <messaging/Flags.h>
#include <protocols/interaction_model/Constants.h>

#include <type_traits>

namespace {
using namespace chip;
using namespace chip::Access;
using namespace chip::Test::Constants::TestAclAttribute;

chip::ClusterId kTestClusterId        = 1;
chip::ClusterId kTestDeniedClusterId2 = 3;
chip::EndpointId kTestEndpointId      = 4;

class TestAccessControlDelegate : public AccessControl::Delegate
{
public:
    CHIP_ERROR Check(const SubjectDescriptor & subjectDescriptor, const chip::Access::RequestPath & requestPath,
                     Privilege requestPrivilege) override
    {
        if (requestPath.cluster == kTestDeniedClusterId2)
        {
            return CHIP_ERROR_ACCESS_DENIED;
        }
        return CHIP_NO_ERROR;
    }
};

AccessControl::Delegate * GetTestAccessControlDelegate()
{
    static TestAccessControlDelegate accessControlDelegate;
    return &accessControlDelegate;
}

class TestDeviceTypeResolver : public AccessControl::DeviceTypeResolver
{
public:
    bool IsDeviceTypeOnEndpoint(DeviceTypeId deviceType, EndpointId endpoint) override { return false; }
} gDeviceTypeResolver;

class TestAccessContext : public chip::Test::AppContext
{
public:
    static int Initialize(void * context)
    {
        if (AppContext::Initialize(context) != SUCCESS)
            return FAILURE;
        Access::GetAccessControl().Finish();
        Access::GetAccessControl().Init(GetTestAccessControlDelegate(), gDeviceTypeResolver);
        return SUCCESS;
    }

    static int Finalize(void * context)
    {
        if (AppContext::Finalize(context) != SUCCESS)
            return FAILURE;
        return SUCCESS;
    }

private:
    chip::MonotonicallyIncreasingCounter<chip::EventNumber> mEventCounter;
};

class MockInteractionModelApp : public chip::app::ReadClient::Callback
{
public:
    void OnAttributeData(const chip::app::ConcreteDataAttributePath & aPath, chip::TLV::TLVReader * apData,
                         const chip::app::StatusIB & status) override
    {
        mGotReport          = true;
        mLastStatusReceived = status;
    }

    void OnError(CHIP_ERROR aError) override { mError = aError; }

    void OnDone(chip::app::ReadClient *) override {}

    void OnDeallocatePaths(chip::app::ReadPrepareParams && aReadPrepareParams) override
    {
        if (aReadPrepareParams.mpAttributePathParamsList != nullptr)
        {
            delete[] aReadPrepareParams.mpAttributePathParamsList;
        }

        if (aReadPrepareParams.mpDataVersionFilterList != nullptr)
        {
            delete[] aReadPrepareParams.mpDataVersionFilterList;
        }
    }

    bool mGotReport = false;
    chip::app::StatusIB mLastStatusReceived;
    CHIP_ERROR mError = CHIP_NO_ERROR;
};
} // namespace

namespace chip {
namespace app {

Protocols::InteractionModel::Status CheckEventSupportStatus(const ConcreteEventPath & aPath)
{
    if (aPath.mClusterId == kTestDeniedClusterId1)
    {
        return Protocols::InteractionModel::Status::UnsupportedCluster;
    }

    return Protocols::InteractionModel::Status::Success;
}

class TestAclAttribute : public ::testing::Test
{
public:
    static TestAccessContext ctx;
    static void SetUpTestSuite() { TestAccessContext::Initialize(&ctx); }
    static void TearDownTestSuite() { TestAccessContext::Finalize(&ctx); }
};

TestAccessContext TestAclAttribute::ctx;

// Read Client sends a malformed subscribe request, interaction model engine fails to parse the request and generates a status
// report to client, and client is closed.
TEST_F(TestAclAttribute, TestACLDeniedAttribute)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        chip::app::AttributePathParams attributePathParams[2];
        attributePathParams[0].mEndpointId  = kTestEndpointId;
        attributePathParams[0].mClusterId   = kTestDeniedClusterId1;
        attributePathParams[0].mAttributeId = 1;

        attributePathParams[1].mEndpointId  = kTestEndpointId;
        attributePathParams[1].mClusterId   = kTestDeniedClusterId1;
        attributePathParams[1].mAttributeId = 2;

        ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
        readPrepareParams.mpAttributePathParamsList    = attributePathParams;
        readPrepareParams.mAttributePathParamsListSize = 2;

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
        EXPECT_TRUE(!delegate.mGotReport);
        delegate.mError     = CHIP_NO_ERROR;
        delegate.mGotReport = false;
    }

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        chip::app::AttributePathParams attributePathParams[2];

        attributePathParams[0].mClusterId   = kTestDeniedClusterId2;
        attributePathParams[0].mAttributeId = 1;

        attributePathParams[1].mClusterId   = kTestDeniedClusterId2;
        attributePathParams[1].mAttributeId = 2;

        ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
        readPrepareParams.mpAttributePathParamsList    = attributePathParams;
        readPrepareParams.mAttributePathParamsListSize = 2;

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
        EXPECT_TRUE(!delegate.mGotReport);
        delegate.mError     = CHIP_NO_ERROR;
        delegate.mGotReport = false;
    }

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        chip::app::AttributePathParams attributePathParams[2];
        attributePathParams[0].mEndpointId  = kTestEndpointId;
        attributePathParams[0].mClusterId   = kTestDeniedClusterId1;
        attributePathParams[0].mAttributeId = 1;

        attributePathParams[1].mEndpointId  = kTestEndpointId;
        attributePathParams[1].mClusterId   = kTestClusterId;
        attributePathParams[1].mAttributeId = 2;

        ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
        readPrepareParams.mpAttributePathParamsList    = attributePathParams;
        readPrepareParams.mAttributePathParamsListSize = 2;

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mError == CHIP_NO_ERROR);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        delegate.mError     = CHIP_NO_ERROR;
        delegate.mGotReport = false;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}
} // namespace app
} // namespace chip
