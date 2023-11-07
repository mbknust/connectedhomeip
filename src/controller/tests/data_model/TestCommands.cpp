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

/**
 *    @file
 *      This file implements unit tests for CHIP Interaction Model Command Interaction
 *
 */

#include "app/data-model/NullObject.h"
#include <app-common/zap-generated/cluster-objects.h>
#include <app/AppConfig.h>
#include <app/InteractionModelEngine.h>
#include <app/tests/AppTestContext.h>
#include <controller/InvokeInteraction.h>
#include <gtest/gtest.h>
#include <lib/core/CHIPCore.h>
#include <lib/core/TLV.h>
#include <lib/core/TLVUtilities.h>
#include <lib/support/ErrorStr.h>
#include <lib/support/UnitTestContext.h>

#include <lib/support/logging/CHIPLogging.h>
#include <messaging/tests/MessagingContext.h>
#include <protocols/interaction_model/Constants.h>

using TestContext = chip::Test::AppContext;

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::Protocols;

namespace {
chip::ClusterStatus kTestSuccessClusterStatus = 1;
chip::ClusterStatus kTestFailureClusterStatus = 2;

constexpr EndpointId kTestEndpointId = 1;

enum ResponseDirective
{
    kSendDataResponse,
    kSendSuccessStatusCode,
    kSendMultipleSuccessStatusCodes,
    kSendError,
    kSendMultipleErrors,
    kSendSuccessStatusCodeWithClusterStatus,
    kSendErrorWithClusterStatus,
    kAsync,
};

ResponseDirective responseDirective;
CommandHandler::Handle asyncHandle;

} // namespace

namespace chip {
namespace app {

void DispatchSingleClusterCommand(const ConcreteCommandPath & aCommandPath, chip::TLV::TLVReader & aReader,
                                  CommandHandler * apCommandObj)
{
    ChipLogDetail(Controller, "Received Cluster Command: Endpoint=%x Cluster=" ChipLogFormatMEI " Command=" ChipLogFormatMEI,
                  aCommandPath.mEndpointId, ChipLogValueMEI(aCommandPath.mClusterId), ChipLogValueMEI(aCommandPath.mCommandId));

    if (aCommandPath.mClusterId == Clusters::UnitTesting::Id &&
        aCommandPath.mCommandId == Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type::GetCommandId())
    {
        Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::DecodableType dataRequest;

        if (DataModel::Decode(aReader, dataRequest) != CHIP_NO_ERROR)
        {
            apCommandObj->AddStatus(aCommandPath, Protocols::InteractionModel::Status::Failure, "Unable to decode the request");
            return;
        }

        if (responseDirective == kSendDataResponse)
        {
            Clusters::UnitTesting::Commands::TestStructArrayArgumentResponse::Type dataResponse;
            Clusters::UnitTesting::Structs::NestedStructList::Type nestedStructList[4];

            uint8_t i = 0;
            for (auto & item : nestedStructList)
            {
                item.a   = i;
                item.b   = false;
                item.c.a = i;
                item.c.b = true;
                i++;
            }

            dataResponse.arg1 = nestedStructList;
            dataResponse.arg6 = true;

            apCommandObj->AddResponse(aCommandPath, dataResponse);
        }
        else if (responseDirective == kSendSuccessStatusCode)
        {
            apCommandObj->AddStatus(aCommandPath, Protocols::InteractionModel::Status::Success);
        }
        else if (responseDirective == kSendMultipleSuccessStatusCodes)
        {
            apCommandObj->AddStatus(aCommandPath, Protocols::InteractionModel::Status::Success,
                                    "No error but testing status success case");

            // TODO: Right now all but the first AddStatus call fail, so this
            // test is not really testing what it should.
            for (size_t i = 0; i < 3; ++i)
            {
                (void) apCommandObj->FallibleAddStatus(aCommandPath, Protocols::InteractionModel::Status::Success,
                                                       "No error but testing status success case");
            }
            // And one failure on the end.
            (void) apCommandObj->FallibleAddStatus(aCommandPath, Protocols::InteractionModel::Status::Failure);
        }
        else if (responseDirective == kSendError)
        {
            apCommandObj->AddStatus(aCommandPath, Protocols::InteractionModel::Status::Failure);
        }
        else if (responseDirective == kSendMultipleErrors)
        {
            apCommandObj->AddStatus(aCommandPath, Protocols::InteractionModel::Status::Failure);

            // TODO: Right now all but the first AddStatus call fail, so this
            // test is not really testing what it should.
            for (size_t i = 0; i < 3; ++i)
            {
                (void) apCommandObj->FallibleAddStatus(aCommandPath, Protocols::InteractionModel::Status::Failure);
            }
        }
        else if (responseDirective == kSendSuccessStatusCodeWithClusterStatus)
        {
            apCommandObj->AddClusterSpecificSuccess(aCommandPath, kTestSuccessClusterStatus);
        }
        else if (responseDirective == kSendErrorWithClusterStatus)
        {
            apCommandObj->AddClusterSpecificFailure(aCommandPath, kTestFailureClusterStatus);
        }
        else if (responseDirective == kAsync)
        {
            asyncHandle = apCommandObj;
        }
    }
}

InteractionModel::Status ServerClusterCommandExists(const ConcreteCommandPath & aCommandPath)
{
    // Mock cluster catalog, only support commands on one cluster on one endpoint.
    using InteractionModel::Status;

    if (aCommandPath.mEndpointId != kTestEndpointId)
    {
        return Status::UnsupportedEndpoint;
    }

    if (aCommandPath.mClusterId != Clusters::UnitTesting::Id)
    {
        return Status::UnsupportedCluster;
    }

    return Status::Success;
}
} // namespace app
} // namespace chip

namespace {

class TestCommandInteraction : public ::testing::Test
{
public:
    static void SetUpTestSuite() { TestContext::Initialize(&ctx); }
    static void TearDownTestSuite() { TestContext::Finalize(&ctx); }
    static TestContext ctx;

    TestCommandInteraction() {}
    static void TestDataResponse();
    static void TestSuccessNoDataResponse();
    static void TestMultipleSuccessNoDataResponses();
    static void TestAsyncResponse();
    static void TestFailure();
    static void TestMultipleFailures();
    static void TestSuccessNoDataResponseWithClusterStatus();
    static void TestFailureWithClusterStatus();

private:
};

TestContext TestCommandInteraction::ctx;

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name) { test_fixture::test_name(); }                                                                 \
    void test_fixture::test_name()

STATIC_TEST(TestCommandInteraction, TestDataResponse)
{
    // We want to send a TestSimpleArgumentRequest::Type, but get a
    // TestStructArrayArgumentResponse in return, so need to shadow the actual
    // ResponseType that TestSimpleArgumentRequest has.
    struct FakeRequest : public Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type
    {
        using ResponseType = Clusters::UnitTesting::Commands::TestStructArrayArgumentResponse::DecodableType;
    };

    FakeRequest request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    bool onSuccessWasCalled = false;
    bool onFailureWasCalled = false;

    request.arg1 = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&onSuccessWasCalled](const app::ConcreteCommandPath & commandPath, const app::StatusIB & aStatus,
                                             const auto & dataResponse) {
        uint8_t i = 0;
        auto iter = dataResponse.arg1.begin();
        while (iter.Next())
        {
            auto & item = iter.GetValue();

            EXPECT_TRUE(item.a == i);
            EXPECT_TRUE(item.b == false);
            EXPECT_TRUE(item.c.a == i);
            EXPECT_TRUE(item.c.b == true);
            i++;
        }

        EXPECT_TRUE(iter.GetStatus() == CHIP_NO_ERROR);
        EXPECT_TRUE(dataResponse.arg6 == true);

        onSuccessWasCalled = true;
    };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&onFailureWasCalled](CHIP_ERROR aError) { onFailureWasCalled = true; };

    responseDirective = kSendDataResponse;

    chip::Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb,
                                           onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(onSuccessWasCalled && !onFailureWasCalled);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestSuccessNoDataResponse)
{
    struct FakeRequest : public Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type
    {
        using ResponseType = DataModel::NullObjectType;
    };

    FakeRequest request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    bool onSuccessWasCalled = false;
    bool onFailureWasCalled = false;
    bool statusCheck        = false;
    request.arg1            = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&onSuccessWasCalled, &statusCheck](const app::ConcreteCommandPath & commandPath,
                                                           const app::StatusIB & aStatus, const auto & dataResponse) {
        statusCheck        = (aStatus.mStatus == Protocols::InteractionModel::Status::Success);
        onSuccessWasCalled = true;
    };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&onFailureWasCalled](CHIP_ERROR aError) { onFailureWasCalled = true; };

    responseDirective = kSendSuccessStatusCode;

    chip::Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb,
                                           onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(onSuccessWasCalled && !onFailureWasCalled && statusCheck);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestMultipleSuccessNoDataResponses)
{
    struct FakeRequest : public Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type
    {
        using ResponseType = DataModel::NullObjectType;
    };

    FakeRequest request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    size_t successCalls = 0;
    size_t failureCalls = 0;
    bool statusCheck    = false;
    request.arg1        = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&successCalls, &statusCheck](const ConcreteCommandPath & commandPath, const StatusIB & aStatus,
                                                     const auto & dataResponse) {
        statusCheck = (aStatus.mStatus == InteractionModel::Status::Success);
        ++successCalls;
    };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&failureCalls](CHIP_ERROR aError) { ++failureCalls; };

    responseDirective = kSendMultipleSuccessStatusCodes;

    Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb, onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(successCalls == 1 && statusCheck);
    EXPECT_TRUE(failureCalls == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestAsyncResponse)
{
    struct FakeRequest : public Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type
    {
        using ResponseType = DataModel::NullObjectType;
    };

    FakeRequest request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    bool onSuccessWasCalled = false;
    bool onFailureWasCalled = false;
    bool statusCheck        = false;
    request.arg1            = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&onSuccessWasCalled, &statusCheck](const app::ConcreteCommandPath & commandPath,
                                                           const app::StatusIB & aStatus, const auto & dataResponse) {
        statusCheck        = (aStatus.mStatus == Protocols::InteractionModel::Status::Success);
        onSuccessWasCalled = true;
    };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&onFailureWasCalled](CHIP_ERROR aError) { onFailureWasCalled = true; };

    responseDirective = kAsync;

    chip::Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb,
                                           onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(!onSuccessWasCalled && !onFailureWasCalled && !statusCheck);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 2);

    CommandHandler * commandHandle = asyncHandle.Get();
    EXPECT_TRUE(commandHandle != nullptr);

    if (commandHandle == nullptr)
    {
        return;
    }

    commandHandle->AddStatus(ConcreteCommandPath(kTestEndpointId, request.GetClusterId(), request.GetCommandId()),
                             Protocols::InteractionModel::Status::Success);
    asyncHandle.Release();

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(onSuccessWasCalled && !onFailureWasCalled && statusCheck);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestFailure)
{
    Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    bool onSuccessWasCalled = false;
    bool onFailureWasCalled = false;
    bool statusCheck        = false;
    request.arg1            = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&onSuccessWasCalled](const app::ConcreteCommandPath & commandPath, const app::StatusIB & aStatus,
                                             const auto & dataResponse) { onSuccessWasCalled = true; };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&onFailureWasCalled, &statusCheck](CHIP_ERROR aError) {
        statusCheck        = aError.IsIMStatus() && app::StatusIB(aError).mStatus == Protocols::InteractionModel::Status::Failure;
        onFailureWasCalled = true;
    };

    responseDirective = kSendError;

    chip::Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb,
                                           onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(!onSuccessWasCalled && onFailureWasCalled && statusCheck);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestMultipleFailures)
{
    struct FakeRequest : public Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type
    {
        using ResponseType = DataModel::NullObjectType;
    };

    FakeRequest request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    size_t successCalls = 0;
    size_t failureCalls = 0;
    bool statusCheck    = false;
    request.arg1        = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&successCalls](const ConcreteCommandPath & commandPath, const StatusIB & aStatus,
                                       const auto & dataResponse) { ++successCalls; };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&failureCalls, &statusCheck](CHIP_ERROR aError) {
        statusCheck = aError.IsIMStatus() && StatusIB(aError).mStatus == InteractionModel::Status::Failure;
        ++failureCalls;
    };

    responseDirective = kSendMultipleErrors;

    Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb, onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(successCalls == 0);
    EXPECT_TRUE(failureCalls == 1 && statusCheck);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestSuccessNoDataResponseWithClusterStatus)
{
    struct FakeRequest : public Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type
    {
        using ResponseType = DataModel::NullObjectType;
    };

    FakeRequest request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    bool onSuccessWasCalled = false;
    bool onFailureWasCalled = false;
    bool statusCheck        = false;
    request.arg1            = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&onSuccessWasCalled, &statusCheck](const app::ConcreteCommandPath & commandPath,
                                                           const app::StatusIB & aStatus, const auto & dataResponse) {
        statusCheck        = (aStatus.mStatus == Protocols::InteractionModel::Status::Success &&
                       aStatus.mClusterStatus.Value() == kTestSuccessClusterStatus);
        onSuccessWasCalled = true;
    };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&onFailureWasCalled](CHIP_ERROR aError) { onFailureWasCalled = true; };

    responseDirective = kSendSuccessStatusCodeWithClusterStatus;

    chip::Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb,
                                           onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(onSuccessWasCalled && !onFailureWasCalled && statusCheck);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestFailureWithClusterStatus)
{
    Clusters::UnitTesting::Commands::TestSimpleArgumentRequest::Type request;
    auto sessionHandle = ctx.GetSessionBobToAlice();

    bool onSuccessWasCalled = false;
    bool onFailureWasCalled = false;
    bool statusCheck        = false;
    request.arg1            = true;

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onSuccessCb = [&onSuccessWasCalled](const app::ConcreteCommandPath & commandPath, const app::StatusIB & aStatus,
                                             const auto & dataResponse) { onSuccessWasCalled = true; };

    // Passing of stack variables by reference is only safe because of synchronous completion of the interaction. Otherwise, it's
    // not safe to do so.
    auto onFailureCb = [&onFailureWasCalled, &statusCheck](CHIP_ERROR aError) {
        statusCheck = aError.IsIMStatus();
        if (statusCheck)
        {
            app::StatusIB status(aError);
            statusCheck = (status.mStatus == Protocols::InteractionModel::Status::Failure &&
                           status.mClusterStatus.Value() == kTestFailureClusterStatus);
        }
        onFailureWasCalled = true;
    };

    responseDirective = kSendErrorWithClusterStatus;

    chip::Controller::InvokeCommandRequest(&ctx.GetExchangeManager(), sessionHandle, kTestEndpointId, request, onSuccessCb,
                                           onFailureCb);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(!onSuccessWasCalled && onFailureWasCalled && statusCheck);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

} // namespace
