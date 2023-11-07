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
 *      This file implements unit tests for CHIP Interaction Model Read Interaction
 *
 */

#include "lib/support/CHIPMem.h"
#include <access/examples/PermissiveAccessControlDelegate.h>
#include <app/AttributeAccessInterface.h>
#include <app/InteractionModelEngine.h>
#include <app/InteractionModelHelper.h>
#include <app/MessageDef/AttributeReportIBs.h>
#include <app/MessageDef/EventDataIB.h>
#include <app/reporting/ReportSchedulerImpl.h>
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
using namespace chip::Test::Constants::TestReadInteraction;

uint8_t gDebugEventBuffer[128];
uint8_t gInfoEventBuffer[128];
uint8_t gCritEventBuffer[128];
chip::app::CircularEventBuffer gCircularEventBuffer[3];
chip::ClusterId kTestEventClusterId     = chip::Test::MockClusterId(1);
chip::ClusterId kInvalidTestClusterId   = 7;
chip::EndpointId kTestEventEndpointId   = chip::Test::kMockEndpoint1;
chip::EventId kTestEventIdDebug         = chip::Test::MockEventId(1);
chip::EventId kTestEventIdCritical      = chip::Test::MockEventId(2);
chip::TLV::Tag kTestEventTag            = chip::TLV::ContextTag(1);
chip::EndpointId kInvalidTestEndpointId = 3;
chip::DataVersion kTestDataVersion1     = 3;
chip::DataVersion kTestDataVersion2     = 5;

// Number of items in the list for MockAttributeId(4).
constexpr int kMockAttribute4ListLength = 6;

static chip::System::Clock::Internal::MockClock gMockClock;
static chip::System::Clock::ClockBase * gRealClock;

class TestContext : public chip::Test::AppContext
{
public:
    static int Initialize(void * context)
    {
        gRealClock = &chip::System::SystemClock();
        chip::System::Clock::Internal::SetSystemClockForTesting(&gMockClock);

        if (AppContext::Initialize(context) != SUCCESS)
            return FAILURE;

        auto * ctx = static_cast<TestContext *>(context);

        if (ctx->mEventCounter.Init(0) != CHIP_NO_ERROR)
        {
            return FAILURE;
        }

        chip::app::LogStorageResources logStorageResources[] = {
            { &gDebugEventBuffer[0], sizeof(gDebugEventBuffer), chip::app::PriorityLevel::Debug },
            { &gInfoEventBuffer[0], sizeof(gInfoEventBuffer), chip::app::PriorityLevel::Info },
            { &gCritEventBuffer[0], sizeof(gCritEventBuffer), chip::app::PriorityLevel::Critical },
        };

        chip::app::EventManagement::CreateEventManagement(&ctx->GetExchangeManager(), ArraySize(logStorageResources),
                                                          gCircularEventBuffer, logStorageResources, &ctx->mEventCounter);

        return SUCCESS;
    }

    static int Finalize(void * context)
    {
        chip::app::EventManagement::DestroyEventManagement();
        chip::System::Clock::Internal::SetSystemClockForTesting(gRealClock);

        if (AppContext::Finalize(context) != SUCCESS)
            return FAILURE;

        return SUCCESS;
    }

private:
    chip::MonotonicallyIncreasingCounter<chip::EventNumber> mEventCounter;
};

class TestEventGenerator : public chip::app::EventLoggingDelegate
{
public:
    CHIP_ERROR WriteEvent(chip::TLV::TLVWriter & aWriter)
    {
        chip::TLV::TLVType dataContainerType;
        ReturnErrorOnFailure(aWriter.StartContainer(chip::TLV::ContextTag(chip::to_underlying(chip::app::EventDataIB::Tag::kData)),
                                                    chip::TLV::kTLVType_Structure, dataContainerType));
        ReturnErrorOnFailure(aWriter.Put(kTestEventTag, mStatus));
        return aWriter.EndContainer(dataContainerType);
    }

    void SetStatus(int32_t aStatus) { mStatus = aStatus; }

private:
    int32_t mStatus;
};

void GenerateEvents()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::EventNumber eid1, eid2;
    chip::app::EventOptions options1;
    options1.mPath     = { kTestEventEndpointId, kTestEventClusterId, kTestEventIdDebug };
    options1.mPriority = chip::app::PriorityLevel::Info;

    chip::app::EventOptions options2;
    options2.mPath     = { kTestEventEndpointId, kTestEventClusterId, kTestEventIdCritical };
    options2.mPriority = chip::app::PriorityLevel::Critical;
    TestEventGenerator testEventGenerator;
    chip::app::EventManagement & logMgmt = chip::app::EventManagement::GetInstance();

    ChipLogDetail(DataManagement, "Generating Events");
    testEventGenerator.SetStatus(0);
    err = logMgmt.LogEvent(&testEventGenerator, options1, eid1);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    testEventGenerator.SetStatus(1);
    err = logMgmt.LogEvent(&testEventGenerator, options2, eid2);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

class MockInteractionModelApp : public chip::app::ReadClient::Callback
{
public:
    void OnEventData(const chip::app::EventHeader & aEventHeader, chip::TLV::TLVReader * apData,
                     const chip::app::StatusIB * apStatus) override
    {
        ++mNumDataElementIndex;
        mGotEventResponse = true;
        if (apStatus != nullptr && !apStatus->IsSuccess())
        {
            mNumReadEventFailureStatusReceived++;
            mLastStatusReceived = *apStatus;
        }
        else
        {
            mLastStatusReceived = chip::app::StatusIB();
        }
    }

    void OnAttributeData(const chip::app::ConcreteDataAttributePath & aPath, chip::TLV::TLVReader * apData,
                         const chip::app::StatusIB & status) override
    {
        if (status.mStatus == chip::Protocols::InteractionModel::Status::Success)
        {
            mReceivedAttributePaths.push_back(aPath);
            mNumAttributeResponse++;
            mGotReport = true;

            if (aPath.IsListItemOperation())
            {
                mNumArrayItems++;
            }
            else if (aPath.IsListOperation())
            {
                // This is an entire list of things; count up how many.
                chip::TLV::TLVType containerType;
                if (apData->EnterContainer(containerType) == CHIP_NO_ERROR)
                {
                    size_t count = 0;
                    if (chip::TLV::Utilities::Count(*apData, count, /* aRecurse = */ false) == CHIP_NO_ERROR)
                    {
                        mNumArrayItems += static_cast<int>(count);
                    }
                }
            }
        }
        mLastStatusReceived = status;
    }

    void OnError(CHIP_ERROR aError) override
    {
        mError     = aError;
        mReadError = true;
    }

    void OnDone(chip::app::ReadClient *) override {}

    void OnDeallocatePaths(chip::app::ReadPrepareParams && aReadPrepareParams) override
    {
        if (aReadPrepareParams.mpAttributePathParamsList != nullptr)
        {
            delete[] aReadPrepareParams.mpAttributePathParamsList;
        }

        if (aReadPrepareParams.mpEventPathParamsList != nullptr)
        {
            delete[] aReadPrepareParams.mpEventPathParamsList;
        }

        if (aReadPrepareParams.mpDataVersionFilterList != nullptr)
        {
            delete[] aReadPrepareParams.mpDataVersionFilterList;
        }
    }

    int mNumDataElementIndex               = 0;
    bool mGotEventResponse                 = false;
    int mNumReadEventFailureStatusReceived = 0;
    int mNumAttributeResponse              = 0;
    int mNumArrayItems                     = 0;
    bool mGotReport                        = false;
    bool mReadError                        = false;
    chip::app::ReadHandler * mpReadHandler = nullptr;
    chip::app::StatusIB mLastStatusReceived;
    CHIP_ERROR mError = CHIP_NO_ERROR;
    std::vector<chip::app::ConcreteAttributePath> mReceivedAttributePaths;
};

//
// This dummy callback is used with a bunch of the tests below that don't go through
// the normal call-path of having the IM engine allocate the ReadHandler object. Instead,
// the object is allocated on stack for the purposes of a very narrow, tightly-coupled test.
//
// The typical callback implementor is the engine, but that would proceed to return the object
// back to the handler pool (which we obviously don't want in this case). This just no-ops those calls.
//
class NullReadHandlerCallback : public chip::app::ReadHandler::ManagementCallback
{
public:
    void OnDone(chip::app::ReadHandler & apReadHandlerObj) override {}
    chip::app::ReadHandler::ApplicationCallback * GetAppCallback() override { return nullptr; }
};

} // namespace

using ReportScheduler     = chip::app::reporting::ReportScheduler;
using ReportSchedulerImpl = chip::app::reporting::ReportSchedulerImpl;
using ReadHandlerNode     = chip::app::reporting::ReportScheduler::ReadHandlerNode;

namespace chip {
namespace app {

// CHIP_ERROR ReadSingleClusterData(const Access::SubjectDescriptor & aSubjectDescriptor, bool aIsFabricFiltered,
//                                  const ConcreteReadAttributePath & aPath, AttributeReportIBs::Builder & aAttributeReports,
//                                  AttributeValueEncoder::AttributeEncodeState * apEncoderState)
// {
//     if (aPath.mClusterId >= chip::Test::kMockEndpointMin)
//     {
//         return chip::Test::ReadSingleMockClusterData(aSubjectDescriptor.fabricIndex, aPath, aAttributeReports, apEncoderState);
//     }

//     if (!(aPath.mClusterId == kTestClusterId && aPath.mEndpointId == kTestEndpointId))
//     {
//         AttributeReportIB::Builder & attributeReport = aAttributeReports.CreateAttributeReport();
//         ReturnErrorOnFailure(aAttributeReports.GetError());
//         ChipLogDetail(DataManagement, "TEST Cluster %" PRIx32 ", Field %" PRIx32 " is dirty", aPath.mClusterId,
//         aPath.mAttributeId);

//         AttributeStatusIB::Builder & attributeStatus = attributeReport.CreateAttributeStatus();
//         ReturnErrorOnFailure(attributeReport.GetError());
//         AttributePathIB::Builder & attributePath = attributeStatus.CreatePath();
//         ReturnErrorOnFailure(attributeStatus.GetError());

//         attributePath.Endpoint(aPath.mEndpointId).Cluster(aPath.mClusterId).Attribute(aPath.mAttributeId).EndOfAttributePathIB();
//         ReturnErrorOnFailure(attributePath.GetError());
//         StatusIB::Builder & errorStatus = attributeStatus.CreateErrorStatus();
//         ReturnErrorOnFailure(attributeStatus.GetError());
//         errorStatus.EncodeStatusIB(StatusIB(Protocols::InteractionModel::Status::UnsupportedAttribute));
//         ReturnErrorOnFailure(errorStatus.GetError());
//         ReturnErrorOnFailure(attributeStatus.EndOfAttributeStatusIB());
//         return attributeReport.EndOfAttributeReportIB();
//     }

//     return AttributeValueEncoder(aAttributeReports, 0, aPath, 0).Encode(kTestFieldValue1);
// }

bool IsClusterDataVersionEqual(const ConcreteClusterPath & aConcreteClusterPath, DataVersion aRequiredVersion)
{
    if (kTestDataVersion1 == aRequiredVersion)
    {
        return true;
    }

    return false;
}

bool IsDeviceTypeOnEndpoint(DeviceTypeId deviceType, EndpointId endpoint)
{
    return false;
}

class TestReadInteraction : public ::testing::Test
{
    using Seconds16      = System::Clock::Seconds16;
    using Milliseconds32 = System::Clock::Milliseconds32;

public:
    static void SetUpTestSuite() { TestContext::Initialize(&ctx); }
    static void TearDownTestSuite() { TestContext::Finalize(&ctx); }
    static TestContext ctx;

    static void TestReadClient();
    static void TestReadUnexpectedSubscriptionId();
    static void TestReadHandler();
    static void TestReadClientGenerateAttributePathList();
    static void TestReadClientGenerateInvalidAttributePathList();
    static void TestReadClientGenerateOneEventPaths();
    static void TestReadClientGenerateTwoEventPaths();
    static void TestReadClientInvalidReport();
    static void TestReadHandlerInvalidAttributePath();
    static void TestProcessSubscribeRequest();
#if CHIP_CONFIG_ENABLE_ICD_SERVER
    static void TestICDProcessSubscribeRequestSupMaxIntervalCeiling();
    static void TestICDProcessSubscribeRequestInfMaxIntervalCeiling();
    static void TestICDProcessSubscribeRequestSupMinInterval();
    static void TestICDProcessSubscribeRequestMaxMinInterval();
    static void TestICDProcessSubscribeRequestInvalidIdleModeDuration();
#endif // CHIP_CONFIG_ENABLE_ICD_SERVER
    static void TestReadRoundtrip();
    static void TestPostSubscribeRoundtripChunkReport();
    static void TestReadRoundtripWithDataVersionFilter();
    static void TestReadRoundtripWithNoMatchPathDataVersionFilter();
    static void TestReadRoundtripWithMultiSamePathDifferentDataVersionFilter();
    static void TestReadRoundtripWithSameDifferentPathsDataVersionFilter();
    static void TestReadWildcard();
    static void TestReadChunking();
    static void TestSetDirtyBetweenChunks();
    static void TestSubscribeRoundtrip();
    static void TestSubscribeEarlyReport();
    static void TestSubscribeUrgentWildcardEvent();
    static void TestSubscribeWildcard();
    static void TestSubscribePartialOverlap();
    static void TestSubscribeSetDirtyFullyOverlap();
    static void TestSubscribeEarlyShutdown();
    static void TestSubscribeInvalidAttributePathRoundtrip();
    static void TestReadInvalidAttributePathRoundtrip();
    static void TestSubscribeInvalidInterval();
    static void TestReadShutdown();
    static void TestResubscribeRoundtrip();
    static void TestSubscribeRoundtripStatusReportTimeout();
    static void TestPostSubscribeRoundtripStatusReportTimeout();
    static void TestReadChunkingStatusReportTimeout();
    static void TestReadReportFailure();
    static void TestSubscribeRoundtripChunkStatusReportTimeout();
    static void TestPostSubscribeRoundtripChunkStatusReportTimeout();
    static void TestPostSubscribeRoundtripChunkReportTimeout();
    static void TestReadClientReceiveInvalidMessage();
    static void TestSubscribeClientReceiveInvalidStatusResponse();
    static void TestSubscribeClientReceiveWellFormedStatusResponse();
    static void TestSubscribeClientReceiveInvalidReportMessage();
    static void TestSubscribeClientReceiveUnsolicitedInvalidReportMessage();
    static void TestSubscribeClientReceiveInvalidSubscribeResponseMessage();
    static void TestSubscribeClientReceiveUnsolicitedReportMessageWithInvalidSubscriptionId();
    static void TestReadChunkingInvalidSubscriptionId();
    static void TestReadHandlerMalformedReadRequest1();
    static void TestReadHandlerMalformedReadRequest2();
    static void TestSubscribeSendUnknownMessage();
    static void TestSubscribeSendInvalidStatusReport();
    static void TestReadHandlerInvalidSubscribeRequest();
    static void TestSubscribeInvalidateFabric();
    static void TestShutdownSubscription();
    static void TestSubscriptionReportWithDefunctSession();
    static void TestReadHandlerMalformedSubscribeRequest();

private:
    static void GenerateReportData(System::PacketBufferHandle & aPayload, bool aNeedInvalidReport, bool aSuppressResponse,
                                   bool aHasSubscriptionId);
};

TestContext TestReadInteraction::ctx;

void TestReadInteraction::GenerateReportData(System::PacketBufferHandle & aPayload, bool aNeedInvalidReport, bool aSuppressResponse,
                                             bool aHasSubscriptionId = false)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(aPayload));

    ReportDataMessage::Builder reportDataMessageBuilder;

    err = reportDataMessageBuilder.Init(&writer);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    if (aHasSubscriptionId)
    {
        reportDataMessageBuilder.SubscriptionId(1);
        EXPECT_TRUE(reportDataMessageBuilder.GetError() == CHIP_NO_ERROR);
    }

    AttributeReportIBs::Builder & attributeReportIBsBuilder = reportDataMessageBuilder.CreateAttributeReportIBs();
    EXPECT_TRUE(reportDataMessageBuilder.GetError() == CHIP_NO_ERROR);

    AttributeReportIB::Builder & attributeReportIBBuilder = attributeReportIBsBuilder.CreateAttributeReport();
    EXPECT_TRUE(attributeReportIBsBuilder.GetError() == CHIP_NO_ERROR);

    AttributeDataIB::Builder & attributeDataIBBuilder = attributeReportIBBuilder.CreateAttributeData();
    EXPECT_TRUE(attributeReportIBBuilder.GetError() == CHIP_NO_ERROR);

    attributeDataIBBuilder.DataVersion(2);
    err = attributeDataIBBuilder.GetError();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    AttributePathIB::Builder & attributePathBuilder = attributeDataIBBuilder.CreatePath();
    EXPECT_TRUE(attributeDataIBBuilder.GetError() == CHIP_NO_ERROR);

    if (aNeedInvalidReport)
    {
        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).ListIndex(5).EndOfAttributePathIB();
    }
    else
    {
        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).EndOfAttributePathIB();
    }

    err = attributePathBuilder.GetError();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Construct attribute data
    {
        chip::TLV::TLVWriter * pWriter = attributeDataIBBuilder.GetWriter();
        chip::TLV::TLVType dummyType   = chip::TLV::kTLVType_NotSpecified;
        err = pWriter->StartContainer(chip::TLV::ContextTag(chip::to_underlying(chip::app::AttributeDataIB::Tag::kData)),
                                      chip::TLV::kTLVType_Structure, dummyType);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = pWriter->PutBoolean(chip::TLV::ContextTag(1), true);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = pWriter->EndContainer(dummyType);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }

    attributeDataIBBuilder.EndOfAttributeDataIB();
    EXPECT_TRUE(attributeDataIBBuilder.GetError() == CHIP_NO_ERROR);

    attributeReportIBBuilder.EndOfAttributeReportIB();
    EXPECT_TRUE(attributeReportIBBuilder.GetError() == CHIP_NO_ERROR);

    attributeReportIBsBuilder.EndOfAttributeReportIBs();
    EXPECT_TRUE(attributeReportIBsBuilder.GetError() == CHIP_NO_ERROR);

    reportDataMessageBuilder.MoreChunkedMessages(false);
    EXPECT_TRUE(reportDataMessageBuilder.GetError() == CHIP_NO_ERROR);

    reportDataMessageBuilder.SuppressResponse(aSuppressResponse);
    EXPECT_TRUE(reportDataMessageBuilder.GetError() == CHIP_NO_ERROR);

    reportDataMessageBuilder.EndOfReportDataMessage();
    EXPECT_TRUE(reportDataMessageBuilder.GetError() == CHIP_NO_ERROR);

    err = writer.Finalize(&aPayload);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name) { test_fixture::test_name(); }                                                                 \
    void test_fixture::test_name()

STATIC_TEST(TestReadInteraction, TestReadRoundtrip)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::EventPathParams eventPathParams[1];
    eventPathParams[0].mEndpointId = kTestEventEndpointId;
    eventPathParams[0].mClusterId  = kTestEventClusterId;

    chip::app::AttributePathParams attributePathParams[2];
    attributePathParams[0].mEndpointId  = kTestEndpointId;
    attributePathParams[0].mClusterId   = kTestClusterId;
    attributePathParams[0].mAttributeId = 1;

    attributePathParams[1].mEndpointId  = kTestEndpointId;
    attributePathParams[1].mClusterId   = kTestClusterId;
    attributePathParams[1].mAttributeId = 2;
    attributePathParams[1].mListIndex   = 1;

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpEventPathParamsList        = eventPathParams;
    readPrepareParams.mEventPathParamsListSize     = 1;
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 2;
    readPrepareParams.mEventNumber.SetValue(1);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mNumDataElementIndex == 1);
        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(!delegate.mReadError);

        delegate.mGotEventResponse     = false;
        delegate.mNumAttributeResponse = 0;
        delegate.mGotReport            = false;
    }

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(!delegate.mReadError);

        // By now we should have closed all exchanges and sent all pending acks, so
        // there should be no queued-up things in the retransmit table.
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadRoundtripWithDataVersionFilter)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[2];
    attributePathParams[0].mEndpointId  = kTestEndpointId;
    attributePathParams[0].mClusterId   = kTestClusterId;
    attributePathParams[0].mAttributeId = 1;

    attributePathParams[1].mEndpointId  = kTestEndpointId;
    attributePathParams[1].mClusterId   = kTestClusterId;
    attributePathParams[1].mAttributeId = 2;
    attributePathParams[1].mListIndex   = 1;

    chip::app::DataVersionFilter dataVersionFilters[1];
    dataVersionFilters[0].mEndpointId = kTestEndpointId;
    dataVersionFilters[0].mClusterId  = kTestClusterId;
    dataVersionFilters[0].mDataVersion.SetValue(kTestDataVersion1);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 2;
    readPrepareParams.mpDataVersionFilterList      = dataVersionFilters;
    readPrepareParams.mDataVersionFilterListSize   = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);

        delegate.mNumAttributeResponse = 0;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadRoundtripWithNoMatchPathDataVersionFilter)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::app::AttributePathParams attributePathParams[2];
    attributePathParams[0].mEndpointId  = kTestEndpointId;
    attributePathParams[0].mClusterId   = kTestClusterId;
    attributePathParams[0].mAttributeId = 1;

    attributePathParams[1].mEndpointId  = kTestEndpointId;
    attributePathParams[1].mClusterId   = kTestClusterId;
    attributePathParams[1].mAttributeId = 2;
    attributePathParams[1].mListIndex   = 1;

    chip::app::DataVersionFilter dataVersionFilters[2];
    dataVersionFilters[0].mEndpointId = kTestEndpointId;
    dataVersionFilters[0].mClusterId  = kInvalidTestClusterId;
    dataVersionFilters[0].mDataVersion.SetValue(kTestDataVersion1);

    dataVersionFilters[1].mEndpointId = kInvalidTestEndpointId;
    dataVersionFilters[1].mClusterId  = kTestClusterId;
    dataVersionFilters[1].mDataVersion.SetValue(kTestDataVersion2);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 2;
    readPrepareParams.mpDataVersionFilterList      = dataVersionFilters;
    readPrepareParams.mDataVersionFilterListSize   = 2;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);
        EXPECT_TRUE(!delegate.mReadError);

        delegate.mNumAttributeResponse = 0;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadRoundtripWithMultiSamePathDifferentDataVersionFilter)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[2];
    attributePathParams[0].mEndpointId  = kTestEndpointId;
    attributePathParams[0].mClusterId   = kTestClusterId;
    attributePathParams[0].mAttributeId = 1;

    attributePathParams[1].mEndpointId  = kTestEndpointId;
    attributePathParams[1].mClusterId   = kTestClusterId;
    attributePathParams[1].mAttributeId = 2;
    attributePathParams[1].mListIndex   = 1;

    chip::app::DataVersionFilter dataVersionFilters[2];
    dataVersionFilters[0].mEndpointId = kTestEndpointId;
    dataVersionFilters[0].mClusterId  = kTestClusterId;
    dataVersionFilters[0].mDataVersion.SetValue(kTestDataVersion1);

    dataVersionFilters[1].mEndpointId = kTestEndpointId;
    dataVersionFilters[1].mClusterId  = kTestClusterId;
    dataVersionFilters[1].mDataVersion.SetValue(kTestDataVersion2);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 2;
    readPrepareParams.mpDataVersionFilterList      = dataVersionFilters;
    readPrepareParams.mDataVersionFilterListSize   = 2;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);
        EXPECT_TRUE(!delegate.mReadError);

        delegate.mNumAttributeResponse = 0;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadRoundtripWithSameDifferentPathsDataVersionFilter)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[2];
    attributePathParams[0].mEndpointId  = kTestEndpointId;
    attributePathParams[0].mClusterId   = kTestClusterId;
    attributePathParams[0].mAttributeId = 1;

    attributePathParams[1].mEndpointId  = kTestEndpointId;
    attributePathParams[1].mClusterId   = kTestClusterId;
    attributePathParams[1].mAttributeId = 2;
    attributePathParams[1].mListIndex   = 1;

    chip::app::DataVersionFilter dataVersionFilters[2];
    dataVersionFilters[0].mEndpointId = kTestEndpointId;
    dataVersionFilters[0].mClusterId  = kTestClusterId;
    dataVersionFilters[0].mDataVersion.SetValue(kTestDataVersion1);

    dataVersionFilters[1].mEndpointId = kInvalidTestEndpointId;
    dataVersionFilters[1].mClusterId  = kTestClusterId;
    dataVersionFilters[1].mDataVersion.SetValue(kTestDataVersion2);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 2;
    readPrepareParams.mpDataVersionFilterList      = dataVersionFilters;
    readPrepareParams.mDataVersionFilterListSize   = 2;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
        EXPECT_TRUE(!delegate.mReadError);

        delegate.mNumAttributeResponse = 0;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadWildcard)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[1];
    attributePathParams[0].mEndpointId = chip::Test::kMockEndpoint2;
    attributePathParams[0].mClusterId  = chip::Test::MockClusterId(3);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpEventPathParamsList        = nullptr;
    readPrepareParams.mEventPathParamsListSize     = 0;
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mNumAttributeResponse == 5);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(!delegate.mReadError);
        // By now we should have closed all exchanges and sent all pending acks, so
        // there should be no queued-up things in the retransmit table.
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// TestReadChunking will try to read a few large attributes, the report won't fit into the MTU and result in chunking.
STATIC_TEST(TestReadInteraction, TestReadChunking)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with kMockAttribute4ListLength large
    // OCTET_STRING elements.
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpEventPathParamsList        = nullptr;
    readPrepareParams.mEventPathParamsListSize     = 0;
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        // We get one chunk with 4 array elements, and then one chunk per
        // element, and the total size of the array is
        // kMockAttribute4ListLength.
        EXPECT_TRUE(delegate.mNumAttributeResponse == 1 + (kMockAttribute4ListLength - 4));
        EXPECT_TRUE(delegate.mNumArrayItems == 6);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(!delegate.mReadError);
        // By now we should have closed all exchanges and sent all pending acks, so
        // there should be no queued-up things in the retransmit table.
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestSetDirtyBetweenChunks)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::app::AttributePathParams attributePathParams[2];
    for (auto & attributePathParam : attributePathParams)
    {
        attributePathParam.mEndpointId  = chip::Test::kMockEndpoint3;
        attributePathParam.mClusterId   = chip::Test::MockClusterId(2);
        attributePathParam.mAttributeId = chip::Test::MockAttributeId(4);
    }

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpEventPathParamsList        = nullptr;
    readPrepareParams.mEventPathParamsListSize     = 0;
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 2;

    {
        int currentAttributeResponsesWhenSetDirty = 0;
        int currentArrayItemsWhenSetDirty         = 0;

        class DirtyingMockDelegate : public MockInteractionModelApp
        {
        public:
            DirtyingMockDelegate(AttributePathParams (&aReadPaths)[2], int & aNumAttributeResponsesWhenSetDirty,
                                 int & aNumArrayItemsWhenSetDirty) :
                mReadPaths(aReadPaths),
                mNumAttributeResponsesWhenSetDirty(aNumAttributeResponsesWhenSetDirty),
                mNumArrayItemsWhenSetDirty(aNumArrayItemsWhenSetDirty)
            {}

        private:
            void OnAttributeData(const ConcreteDataAttributePath & aPath, TLV::TLVReader * apData, const StatusIB & status) override
            {
                MockInteractionModelApp::OnAttributeData(aPath, apData, status);
                if (!mGotStartOfFirstReport && aPath.mEndpointId == mReadPaths[0].mEndpointId &&
                    aPath.mClusterId == mReadPaths[0].mClusterId && aPath.mAttributeId == mReadPaths[0].mAttributeId &&
                    !aPath.IsListItemOperation())
                {
                    mGotStartOfFirstReport = true;
                    return;
                }

                if (!mGotStartOfSecondReport && aPath.mEndpointId == mReadPaths[1].mEndpointId &&
                    aPath.mClusterId == mReadPaths[1].mClusterId && aPath.mAttributeId == mReadPaths[1].mAttributeId &&
                    !aPath.IsListItemOperation())
                {
                    mGotStartOfSecondReport = true;
                    // We always have data chunks, so go ahead to mark things
                    // dirty as needed.
                }

                if (!mGotStartOfSecondReport)
                {
                    // Don't do any setting dirty yet; we are waiting for a data
                    // chunk from the second path.
                    return;
                }

                if (mDidSetDirty)
                {
                    if (!aPath.IsListItemOperation())
                    {
                        mGotPostSetDirtyReport = true;
                        return;
                    }

                    if (!mGotPostSetDirtyReport)
                    {
                        // We're finishing out the message where we decided to
                        // SetDirty.
                        ++mNumAttributeResponsesWhenSetDirty;
                        ++mNumArrayItemsWhenSetDirty;
                    }
                }

                if (!mDidSetDirty)
                {
                    mDidSetDirty = true;

                    AttributePathParams dirtyPath;
                    dirtyPath.mEndpointId  = chip::Test::kMockEndpoint3;
                    dirtyPath.mClusterId   = chip::Test::MockClusterId(2);
                    dirtyPath.mAttributeId = chip::Test::MockAttributeId(4);

                    if (aPath.mEndpointId == dirtyPath.mEndpointId && aPath.mClusterId == dirtyPath.mClusterId &&
                        aPath.mAttributeId == dirtyPath.mAttributeId)
                    {
                        // At this time, we are in the middle of report for second item.
                        mNumAttributeResponsesWhenSetDirty = mNumAttributeResponse;
                        mNumArrayItemsWhenSetDirty         = mNumArrayItems;
                        InteractionModelEngine::GetInstance()->GetReportingEngine().SetDirty(dirtyPath);
                    }
                }
            }

            // Whether we got the start of the report for our first path.
            bool mGotStartOfFirstReport = false;
            // Whether we got the start of the report for our second path.
            bool mGotStartOfSecondReport = false;
            // Whether we got a new non-list-item report after we set dirty.
            bool mGotPostSetDirtyReport = false;
            bool mDidSetDirty           = false;
            AttributePathParams (&mReadPaths)[2];
            int & mNumAttributeResponsesWhenSetDirty;
            int & mNumArrayItemsWhenSetDirty;
        };

        DirtyingMockDelegate delegate(attributePathParams, currentAttributeResponsesWhenSetDirty, currentArrayItemsWhenSetDirty);
        EXPECT_TRUE(!delegate.mGotEventResponse);

        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        // Our list has length kMockAttribute4ListLength.  Since the underlying
        // path iterator should be reset to the beginning of the cluster it is
        // currently iterating, we expect to get another value for our
        // attribute.  The way the packet boundaries happen to fall, that value
        // will encode 4 items in the first IB and then one IB per item.
        const int expectedIBs = 1 + (kMockAttribute4ListLength - 4);
        ChipLogError(DataManagement, "OLD: %d\n", currentAttributeResponsesWhenSetDirty);
        ChipLogError(DataManagement, "NEW: %d\n", delegate.mNumAttributeResponse);
        EXPECT_TRUE(delegate.mNumAttributeResponse == currentAttributeResponsesWhenSetDirty + expectedIBs);
        EXPECT_TRUE(delegate.mNumArrayItems == currentArrayItemsWhenSetDirty + kMockAttribute4ListLength);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(!delegate.mReadError);
        // By now we should have closed all exchanges and sent all pending acks, so
        // there should be no queued-up things in the retransmit table.
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadClient)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    MockInteractionModelApp delegate;
    app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                               chip::app::ReadClient::InteractionType::Read);
    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    err = readClient.SendRequest(readPrepareParams);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // We don't actually want to deliver that message, because we want to
    // synthesize the read response.  But we don't want it hanging around
    // forever either.
    ctx.GetLoopback().mNumMessagesToDrop = 1;
    ctx.DrainAndServiceIO();

    GenerateReportData(buf, false /*aNeedInvalidReport*/, true /* aSuppressResponse*/);
    err = readClient.ProcessReportData(std::move(buf), ReadClient::ReportType::kContinuingTransaction);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

STATIC_TEST(TestReadInteraction, TestReadUnexpectedSubscriptionId)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    MockInteractionModelApp delegate;
    app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                               chip::app::ReadClient::InteractionType::Read);
    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    err = readClient.SendRequest(readPrepareParams);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // We don't actually want to deliver that message, because we want to
    // synthesize the read response.  But we don't want it hanging around
    // forever either.
    ctx.GetLoopback().mNumMessagesToDrop = 1;
    ctx.DrainAndServiceIO();

    // For read, we don't expect there is subscription id in report data.
    GenerateReportData(buf, false /*aNeedInvalidReport*/, true /* aSuppressResponse*/, true /*aHasSubscriptionId*/);
    err = readClient.ProcessReportData(std::move(buf), ReadClient::ReportType::kContinuingTransaction);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_ARGUMENT);
}

STATIC_TEST(TestReadInteraction, TestReadHandler)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle reportDatabuf  = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    System::PacketBufferHandle readRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    ReadRequestMessage::Builder readRequestBuilder;
    MockInteractionModelApp delegate;
    NullReadHandlerCallback nullCallback;

    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    {
        Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);
        ReadHandler readHandler(nullCallback, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        GenerateReportData(reportDatabuf, false /*aNeedInvalidReport*/, false /* aSuppressResponse*/);
        err = readHandler.SendReportData(std::move(reportDatabuf), false);
        EXPECT_TRUE(err == CHIP_ERROR_INCORRECT_STATE);

        writer.Init(std::move(readRequestbuf));
        err = readRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = readRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        err = attributePathListBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        EXPECT_TRUE(readRequestBuilder.GetError() == CHIP_NO_ERROR);
        readRequestBuilder.IsFabricFiltered(false).EndOfReadRequestMessage();
        EXPECT_TRUE(readRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&readRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        // Call ProcessReadRequest directly, because OnInitialRequest sends status
        // messages on the wire instead of returning an error.
        err = readHandler.ProcessReadRequest(std::move(readRequestbuf));
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }

    engine->Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadClientGenerateAttributePathList)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    MockInteractionModelApp delegate;
    System::PacketBufferHandle msgBuf;
    System::PacketBufferTLVWriter writer;
    ReadRequestMessage::Builder request;
    msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    EXPECT_TRUE(!msgBuf.IsNull());
    writer.Init(std::move(msgBuf));
    err = request.Init(&writer);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                               chip::app::ReadClient::InteractionType::Read);

    AttributePathParams attributePathParams[2];
    attributePathParams[0].mAttributeId = 0;
    attributePathParams[1].mAttributeId = 0;
    attributePathParams[1].mListIndex   = 0;

    Span<AttributePathParams> attributePaths(attributePathParams, 2 /*aAttributePathParamsListSize*/);

    AttributePathIBs::Builder & attributePathListBuilder = request.CreateAttributeRequests();
    err = readClient.GenerateAttributePaths(attributePathListBuilder, attributePaths);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

STATIC_TEST(TestReadInteraction, TestReadClientGenerateInvalidAttributePathList)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    MockInteractionModelApp delegate;
    System::PacketBufferHandle msgBuf;
    System::PacketBufferTLVWriter writer;
    ReadRequestMessage::Builder request;
    msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    EXPECT_TRUE(!msgBuf.IsNull());
    writer.Init(std::move(msgBuf));

    app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                               chip::app::ReadClient::InteractionType::Read);

    err = request.Init(&writer);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    AttributePathParams attributePathParams[2];
    attributePathParams[0].mAttributeId = 0;
    attributePathParams[1].mListIndex   = 0;

    Span<AttributePathParams> attributePaths(attributePathParams, 2 /*aAttributePathParamsListSize*/);

    AttributePathIBs::Builder & attributePathListBuilder = request.CreateAttributeRequests();
    err = readClient.GenerateAttributePaths(attributePathListBuilder, attributePaths);
    EXPECT_TRUE(err == CHIP_ERROR_IM_MALFORMED_ATTRIBUTE_PATH_IB);
}

STATIC_TEST(TestReadInteraction, TestReadClientGenerateOneEventPaths)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    MockInteractionModelApp delegate;
    System::PacketBufferHandle msgBuf;
    System::PacketBufferTLVWriter writer;
    ReadRequestMessage::Builder request;
    msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    EXPECT_TRUE(!msgBuf.IsNull());
    writer.Init(std::move(msgBuf));
    err = request.Init(&writer);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                               chip::app::ReadClient::InteractionType::Read);

    chip::app::EventPathParams eventPathParams[1];
    eventPathParams[0].mEndpointId = 2;
    eventPathParams[0].mClusterId  = 3;
    eventPathParams[0].mEventId    = 4;

    EventPathIBs::Builder & eventPathListBuilder = request.CreateEventRequests();
    Span<EventPathParams> eventPaths(eventPathParams, 1 /*aEventPathParamsListSize*/);
    err = readClient.GenerateEventPaths(eventPathListBuilder, eventPaths);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    request.IsFabricFiltered(false).EndOfReadRequestMessage();
    EXPECT_TRUE(CHIP_NO_ERROR == request.GetError());

    err = writer.Finalize(&msgBuf);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::System::PacketBufferTLVReader reader;
    ReadRequestMessage::Parser readRequestParser;

    reader.Init(msgBuf.Retain());
    err = readRequestParser.Init(reader);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

#if CHIP_CONFIG_IM_PRETTY_PRINT
    readRequestParser.PrettyPrint();
#endif

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadClientGenerateTwoEventPaths)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    MockInteractionModelApp delegate;
    System::PacketBufferHandle msgBuf;
    System::PacketBufferTLVWriter writer;
    ReadRequestMessage::Builder request;
    msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    EXPECT_TRUE(!msgBuf.IsNull());
    writer.Init(std::move(msgBuf));
    err = request.Init(&writer);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                               chip::app::ReadClient::InteractionType::Read);

    chip::app::EventPathParams eventPathParams[2];
    eventPathParams[0].mEndpointId = 2;
    eventPathParams[0].mClusterId  = 3;
    eventPathParams[0].mEventId    = 4;

    eventPathParams[1].mEndpointId = 2;
    eventPathParams[1].mClusterId  = 3;
    eventPathParams[1].mEventId    = 5;

    EventPathIBs::Builder & eventPathListBuilder = request.CreateEventRequests();
    Span<EventPathParams> eventPaths(eventPathParams, 2 /*aEventPathParamsListSize*/);
    err = readClient.GenerateEventPaths(eventPathListBuilder, eventPaths);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    request.IsFabricFiltered(false).EndOfReadRequestMessage();
    EXPECT_TRUE(CHIP_NO_ERROR == request.GetError());

    err = writer.Finalize(&msgBuf);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::System::PacketBufferTLVReader reader;
    ReadRequestMessage::Parser readRequestParser;

    reader.Init(msgBuf.Retain());
    err = readRequestParser.Init(reader);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

#if CHIP_CONFIG_IM_PRETTY_PRINT
    readRequestParser.PrettyPrint();
#endif

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadClientInvalidReport)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    MockInteractionModelApp delegate;

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                               chip::app::ReadClient::InteractionType::Read);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    err = readClient.SendRequest(readPrepareParams);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // We don't actually want to deliver that message, because we want to
    // synthesize the read response.  But we don't want it hanging around
    // forever either.
    ctx.GetLoopback().mNumMessagesToDrop = 1;
    ctx.DrainAndServiceIO();

    GenerateReportData(buf, true /*aNeedInvalidReport*/, true /* aSuppressResponse*/);

    err = readClient.ProcessReportData(std::move(buf), ReadClient::ReportType::kContinuingTransaction);
    EXPECT_TRUE(err == CHIP_ERROR_IM_MALFORMED_ATTRIBUTE_PATH_IB);
}

STATIC_TEST(TestReadInteraction, TestReadHandlerInvalidAttributePath)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle reportDatabuf  = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    System::PacketBufferHandle readRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    ReadRequestMessage::Builder readRequestBuilder;
    MockInteractionModelApp delegate;
    NullReadHandlerCallback nullCallback;

    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    {
        Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);
        ReadHandler readHandler(nullCallback, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        GenerateReportData(reportDatabuf, false /*aNeedInvalidReport*/, false /* aSuppressResponse*/);
        err = readHandler.SendReportData(std::move(reportDatabuf), false);
        EXPECT_TRUE(err == CHIP_ERROR_INCORRECT_STATE);

        writer.Init(std::move(readRequestbuf));
        err = readRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = readRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        readRequestBuilder.EndOfReadRequestMessage();
        EXPECT_TRUE(readRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&readRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = readHandler.ProcessReadRequest(std::move(readRequestbuf));
        ChipLogError(DataManagement, "The error is %s", ErrorStr(err));
        EXPECT_TRUE(err == CHIP_ERROR_END_OF_TLV);

        //
        // In the call above to ProcessReadRequest, the handler will not actually close out the EC since
        // it expects the ExchangeManager to do so automatically given it's not calling WillSend() on the EC,
        // and is not sending a response back.
        //
        // Consequently, we have to manually close out the EC here in this test since we're not actually calling
        // methods on these objects in a manner similar to how it would happen in normal use.
        //
        exchangeCtx->Close();
    }

    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestProcessSubscribeRequest)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle subscribeRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    SubscribeRequestMessage::Builder subscribeRequestBuilder;
    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);

    {
        ReadHandler readHandler(*engine, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        writer.Init(std::move(subscribeRequestbuf));
        err = subscribeRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.KeepSubscriptions(true);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MinIntervalFloorSeconds(2);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MaxIntervalCeilingSeconds(3);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = subscribeRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).ListIndex(5).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        err = attributePathListBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.IsFabricFiltered(false).EndOfSubscribeRequestMessage();
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&subscribeRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = readHandler.ProcessSubscribeRequest(std::move(subscribeRequestbuf));
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }

    engine->Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

#if CHIP_CONFIG_ENABLE_ICD_SERVER
/**
 * @brief Test validates that an ICD will choose its IdleModeDuration (GetPublisherSelectedIntervalLimit)
 *        as MaxInterval when the MaxIntervalCeiling is superior.
 */
STATIC_TEST(TestReadInteraction, TestICDProcessSubscribeRequestSupMaxIntervalCeiling)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle subscribeRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    SubscribeRequestMessage::Builder subscribeRequestBuilder;
    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    uint16_t kMinInterval        = 0;
    uint16_t kMaxIntervalCeiling = 1;

    Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);

    {
        ReadHandler readHandler(*engine, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        writer.Init(std::move(subscribeRequestbuf));
        err = subscribeRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.KeepSubscriptions(true);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MinIntervalFloorSeconds(kMinInterval);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MaxIntervalCeilingSeconds(kMaxIntervalCeiling);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = subscribeRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).ListIndex(5).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        err = attributePathListBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.IsFabricFiltered(false).EndOfSubscribeRequestMessage();
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&subscribeRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = readHandler.ProcessSubscribeRequest(std::move(subscribeRequestbuf));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        uint16_t idleModeDuration = readHandler.GetPublisherSelectedIntervalLimit();

        uint16_t minInterval;
        uint16_t maxInterval;
        readHandler.GetReportingIntervals(minInterval, maxInterval);

        EXPECT_TRUE(minInterval == kMinInterval);
        EXPECT_TRUE(maxInterval == idleModeDuration);
    }
    engine->Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

/**
 * @brief Test validates that an ICD will choose its IdleModeDuration (GetPublisherSelectedIntervalLimit)
 *        as MaxInterval when the MaxIntervalCeiling is inferior.
 */
STATIC_TEST(TestReadInteraction, TestICDProcessSubscribeRequestInfMaxIntervalCeiling)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle subscribeRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    SubscribeRequestMessage::Builder subscribeRequestBuilder;
    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    uint16_t kMinInterval        = 0;
    uint16_t kMaxIntervalCeiling = 1;

    Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);

    {
        ReadHandler readHandler(*engine, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        writer.Init(std::move(subscribeRequestbuf));
        err = subscribeRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.KeepSubscriptions(true);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MinIntervalFloorSeconds(kMinInterval);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MaxIntervalCeilingSeconds(kMaxIntervalCeiling);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = subscribeRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).ListIndex(5).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        err = attributePathListBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.IsFabricFiltered(false).EndOfSubscribeRequestMessage();
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&subscribeRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = readHandler.ProcessSubscribeRequest(std::move(subscribeRequestbuf));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        uint16_t idleModeDuration = readHandler.GetPublisherSelectedIntervalLimit();

        uint16_t minInterval;
        uint16_t maxInterval;
        readHandler.GetReportingIntervals(minInterval, maxInterval);

        EXPECT_TRUE(minInterval == kMinInterval);
        EXPECT_TRUE(maxInterval == idleModeDuration);
    }
    engine->Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

/**
 * @brief Test validates that an ICD will choose a multiple of its IdleModeDuration (GetPublisherSelectedIntervalLimit)
 *        as MaxInterval when the MinInterval > IdleModeDuration.
 */
STATIC_TEST(TestReadInteraction, TestICDProcessSubscribeRequestSupMinInterval)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle subscribeRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    SubscribeRequestMessage::Builder subscribeRequestBuilder;
    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    uint16_t kMinInterval        = 305; // Default IdleModeDuration is 300
    uint16_t kMaxIntervalCeiling = 605;

    Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);

    {
        ReadHandler readHandler(*engine, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        writer.Init(std::move(subscribeRequestbuf));
        err = subscribeRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.KeepSubscriptions(true);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MinIntervalFloorSeconds(kMinInterval);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MaxIntervalCeilingSeconds(kMaxIntervalCeiling);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = subscribeRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).ListIndex(5).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        err = attributePathListBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.IsFabricFiltered(false).EndOfSubscribeRequestMessage();
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&subscribeRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = readHandler.ProcessSubscribeRequest(std::move(subscribeRequestbuf));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        uint16_t idleModeDuration = readHandler.GetPublisherSelectedIntervalLimit();

        uint16_t minInterval;
        uint16_t maxInterval;
        readHandler.GetReportingIntervals(minInterval, maxInterval);

        EXPECT_TRUE(minInterval == kMinInterval);
        EXPECT_TRUE(maxInterval == (2 * idleModeDuration));
    }
    engine->Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

/**
 * @brief Test validates that an ICD will choose a maximal value for an uint16 if the multiple of the IdleModeDuration
 *        is greater than variable size.
 */
STATIC_TEST(TestReadInteraction, TestICDProcessSubscribeRequestMaxMinInterval)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle subscribeRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    SubscribeRequestMessage::Builder subscribeRequestBuilder;
    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    uint16_t kMinInterval        = System::Clock::Seconds16::max().count();
    uint16_t kMaxIntervalCeiling = System::Clock::Seconds16::max().count();

    Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);

    {
        ReadHandler readHandler(*engine, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        writer.Init(std::move(subscribeRequestbuf));
        err = subscribeRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.KeepSubscriptions(true);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MinIntervalFloorSeconds(kMinInterval);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MaxIntervalCeilingSeconds(kMaxIntervalCeiling);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = subscribeRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).ListIndex(5).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        err = attributePathListBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.IsFabricFiltered(false).EndOfSubscribeRequestMessage();
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&subscribeRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = readHandler.ProcessSubscribeRequest(std::move(subscribeRequestbuf));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        uint16_t minInterval;
        uint16_t maxInterval;
        readHandler.GetReportingIntervals(minInterval, maxInterval);

        EXPECT_TRUE(minInterval == kMinInterval);
        EXPECT_TRUE(maxInterval == kMaxIntervalCeiling);
    }
    engine->Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

/**
 * @brief Test validates that an ICD will choose the MaxIntervalCeiling as MaxInterval if the next multiple after the MinInterval
 *        is greater than the IdleModeDuration and MaxIntervalCeiling
 */
void TestReadInteraction::TestICDProcessSubscribeRequestInvalidIdleModeDuration()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    System::PacketBufferHandle subscribeRequestbuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    SubscribeRequestMessage::Builder subscribeRequestBuilder;
    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    uint16_t kMinInterval        = 400;
    uint16_t kMaxIntervalCeiling = 400;

    Messaging::ExchangeContext * exchangeCtx = ctx.NewExchangeToAlice(nullptr, false);

    {
        ReadHandler readHandler(*engine, exchangeCtx, chip::app::ReadHandler::InteractionType::Read,
                                app::reporting::GetDefaultReportScheduler());

        writer.Init(std::move(subscribeRequestbuf));
        err = subscribeRequestBuilder.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.KeepSubscriptions(true);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MinIntervalFloorSeconds(kMinInterval);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        subscribeRequestBuilder.MaxIntervalCeilingSeconds(kMaxIntervalCeiling);
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIBs::Builder & attributePathListBuilder = subscribeRequestBuilder.CreateAttributeRequests();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        AttributePathIB::Builder & attributePathBuilder = attributePathListBuilder.CreatePath();
        EXPECT_TRUE(attributePathListBuilder.GetError() == CHIP_NO_ERROR);

        attributePathBuilder.Node(1).Endpoint(2).Cluster(3).Attribute(4).ListIndex(5).EndOfAttributePathIB();
        err = attributePathBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        attributePathListBuilder.EndOfAttributePathIBs();
        err = attributePathListBuilder.GetError();
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        subscribeRequestBuilder.IsFabricFiltered(false).EndOfSubscribeRequestMessage();
        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);

        EXPECT_TRUE(subscribeRequestBuilder.GetError() == CHIP_NO_ERROR);
        err = writer.Finalize(&subscribeRequestbuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = readHandler.ProcessSubscribeRequest(std::move(subscribeRequestbuf));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        uint16_t minInterval;
        uint16_t maxInterval;
        readHandler.GetReportingIntervals(minInterval, maxInterval);

        EXPECT_TRUE(minInterval == kMinInterval);
        EXPECT_TRUE(maxInterval == kMaxIntervalCeiling);
    }
    engine->Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

#endif // CHIP_CONFIG_ENABLE_ICD_SERVER

STATIC_TEST(TestReadInteraction, TestSubscribeRoundtrip)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestEventIdDebug;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 1;
    readPrepareParams.mMaxIntervalCeilingSeconds = 2;
    printf("\nSend first subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
    }

    delegate.mNumAttributeResponse       = 0;
    readPrepareParams.mKeepSubscriptions = false;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        delegate.mGotReport = false;
        err                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);

        GenerateEvents();
        chip::app::AttributePathParams dirtyPath1;
        dirtyPath1.mClusterId   = kTestClusterId;
        dirtyPath1.mEndpointId  = kTestEndpointId;
        dirtyPath1.mAttributeId = 1;

        chip::app::AttributePathParams dirtyPath2;
        dirtyPath2.mClusterId   = kTestClusterId;
        dirtyPath2.mEndpointId  = kTestEndpointId;
        dirtyPath2.mAttributeId = 2;

        chip::app::AttributePathParams dirtyPath3;
        dirtyPath3.mClusterId   = kTestClusterId;
        dirtyPath3.mEndpointId  = kTestEndpointId;
        dirtyPath3.mAttributeId = 2;
        dirtyPath3.mListIndex   = 1;

        chip::app::AttributePathParams dirtyPath4;
        dirtyPath4.mClusterId   = kTestClusterId;
        dirtyPath4.mEndpointId  = kTestEndpointId;
        dirtyPath4.mAttributeId = 3;

        chip::app::AttributePathParams dirtyPath5;
        dirtyPath5.mClusterId   = kTestClusterId;
        dirtyPath5.mEndpointId  = kTestEndpointId;
        dirtyPath5.mAttributeId = 4;

        // Test report with 2 different path

        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(readPrepareParams.mMinIntervalFloorSeconds));
        ctx.GetIOContext().DriveIO();

        delegate.mGotReport            = false;
        delegate.mGotEventResponse     = false;
        delegate.mNumAttributeResponse = 0;

        err = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath2);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mGotEventResponse == true);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);

        // Test report with 2 different path, and 1 same path
        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(readPrepareParams.mMinIntervalFloorSeconds));
        ctx.GetIOContext().DriveIO();

        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;
        err                            = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath2);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath2);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);

        // Test report with 3 different path, and one path is overlapped with another
        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(readPrepareParams.mMinIntervalFloorSeconds));
        ctx.GetIOContext().DriveIO();

        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;
        err                            = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath2);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath3);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);

        // Test report with 3 different path, all are not overlapped, one path is not interested for current subscription
        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(readPrepareParams.mMinIntervalFloorSeconds));
        ctx.GetIOContext().DriveIO();

        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;
        err                            = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath2);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath4);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);

        uint16_t minInterval;
        uint16_t maxInterval;
        delegate.mpReadHandler->GetReportingIntervals(minInterval, maxInterval);

        // Test empty report
        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(maxInterval));
        ctx.GetIOContext().DriveIO();

        EXPECT_TRUE(engine->GetReportingEngine().IsRunScheduled());
        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
    }

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

void TestReadInteraction::TestSubscribeEarlyReport()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[1];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;

    readPrepareParams.mEventPathParamsListSize = 1;

    readPrepareParams.mpAttributePathParamsList    = nullptr;
    readPrepareParams.mAttributePathParamsListSize = 0;

    readPrepareParams.mMinIntervalFloorSeconds   = 1;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    readPrepareParams.mKeepSubscriptions = true;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        readPrepareParams.mpEventPathParamsList[0].mIsUrgentEvent = true;
        delegate.mGotEventResponse                                = false;
        err                                                       = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        System::Clock::Timestamp startTime = gMockClock.GetMonotonicTimestamp();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        uint16_t minInterval;
        uint16_t maxInterval;
        delegate.mpReadHandler->GetReportingIntervals(minInterval, maxInterval);

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);

        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) ==
                           gMockClock.GetMonotonicTimestamp() + Seconds16(readPrepareParams.mMinIntervalFloorSeconds));
        EXPECT_TRUE(reportScheduler->GetMaxTimestampForHandler(delegate.mpReadHandler) ==
                           gMockClock.GetMonotonicTimestamp() + Seconds16(maxInterval));

        // Confirm that the node is scheduled to run
        EXPECT_TRUE(reportScheduler->IsReportScheduled(delegate.mpReadHandler));
        ReportScheduler::ReadHandlerNode * node = reportScheduler->GetReadHandlerNode(delegate.mpReadHandler);
        EXPECT_TRUE(node != nullptr);

        GenerateEvents();

        // modify the node's min timestamp to be 50ms later than the timer expiration time
        node->SetIntervalTimeStamps(delegate.mpReadHandler, startTime + Milliseconds32(50));
        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) ==
                    gMockClock.GetMonotonicTimestamp() + Seconds16(readPrepareParams.mMinIntervalFloorSeconds) +
                        Milliseconds32(50));

        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) > startTime);
        EXPECT_TRUE(delegate.mpReadHandler->IsDirty());

        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(Seconds16(readPrepareParams.mMinIntervalFloorSeconds));
        EXPECT_TRUE(!InteractionModelEngine::GetInstance()->GetReportingEngine().IsRunScheduled());
        // Service Timer expired event
        ctx.GetIOContext().DriveIO();

        // Verify the ReadHandler is considered as reportable even if its node's min timestamp has not expired
        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) > gMockClock.GetMonotonicTimestamp());
        EXPECT_TRUE(reportScheduler->IsReportableNow(delegate.mpReadHandler));
        EXPECT_TRUE(InteractionModelEngine::GetInstance()->GetReportingEngine().IsRunScheduled());

        // Service Engine Run
        ctx.GetIOContext().DriveIO();
        // Service EventManagement event
        ctx.GetIOContext().DriveIO();
        ctx.GetIOContext().DriveIO();
        EXPECT_TRUE(delegate.mGotEventResponse);

        // Check the logic works for timer expiring at maximum as well
        EXPECT_TRUE(!delegate.mpReadHandler->IsDirty());
        delegate.mGotEventResponse = false;
        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) ==
                           gMockClock.GetMonotonicTimestamp() + Seconds16(readPrepareParams.mMinIntervalFloorSeconds));
        EXPECT_TRUE(reportScheduler->GetMaxTimestampForHandler(delegate.mpReadHandler) ==
                           gMockClock.GetMonotonicTimestamp() + Seconds16(maxInterval));

        // Confirm that the node is scheduled to run
        EXPECT_TRUE(reportScheduler->IsReportScheduled(delegate.mpReadHandler));
        EXPECT_TRUE(node != nullptr);

        // modify the node's max timestamp to be 50ms later than the timer expiration time
        node->SetIntervalTimeStamps(delegate.mpReadHandler, gMockClock.GetMonotonicTimestamp() + Milliseconds32(50));
        EXPECT_TRUE(reportScheduler->GetMaxTimestampForHandler(delegate.mpReadHandler) ==
                           gMockClock.GetMonotonicTimestamp() + Seconds16(maxInterval) + Milliseconds32(50));

        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(Seconds16(maxInterval));

        EXPECT_TRUE(!InteractionModelEngine::GetInstance()->GetReportingEngine().IsRunScheduled());
        // Service Timer expired event
        ctx.GetIOContext().DriveIO();

        // Verify the ReadHandler is considered as reportable even if its node's min timestamp has not expired
        EXPECT_TRUE(reportScheduler->GetMaxTimestampForHandler(delegate.mpReadHandler) > gMockClock.GetMonotonicTimestamp());
        EXPECT_TRUE(reportScheduler->IsReportableNow(delegate.mpReadHandler));
        EXPECT_TRUE(!reportScheduler->IsReportScheduled(delegate.mpReadHandler));
        EXPECT_TRUE(!delegate.mpReadHandler->IsDirty());
        EXPECT_TRUE(InteractionModelEngine::GetInstance()->GetReportingEngine().IsRunScheduled());
        // Service Engine Run
        ctx.GetIOContext().DriveIO();
        // Service EventManagement event
        ctx.GetIOContext().DriveIO();
        ctx.GetIOContext().DriveIO();
        EXPECT_TRUE(reportScheduler->IsReportScheduled(delegate.mpReadHandler));
        EXPECT_TRUE(!InteractionModelEngine::GetInstance()->GetReportingEngine().IsRunScheduled());
    }
}

STATIC_TEST(TestReadInteraction, TestPostSubscribeRoundtripChunkReport)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestEventIdDebug;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 1;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    delegate.mNumAttributeResponse       = 0;
    readPrepareParams.mKeepSubscriptions = false;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        printf("\nSend first subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);
        delegate.mGotReport = false;
        err                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);

        GenerateEvents();
        chip::app::AttributePathParams dirtyPath1;
        dirtyPath1.mClusterId   = chip::Test::MockClusterId(2);
        dirtyPath1.mEndpointId  = chip::Test::kMockEndpoint3;
        dirtyPath1.mAttributeId = chip::Test::MockAttributeId(4);

        err                            = engine->GetReportingEngine().SetDirty(dirtyPath1);
        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;
        delegate.mNumArrayItems        = 0;

        // wait for min interval 1 seconds(in test, we use 0.9second considering the time variation), expect no event is received,
        // then wait for 0.5 seconds, then all chunked dirty reports are sent out, which would not honor minInterval
        gMockClock.AdvanceMonotonic(System::Clock::Milliseconds32(900));
        ctx.GetIOContext().DriveIO();

        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
        System::Clock::Timestamp startTime = gMockClock.GetMonotonicTimestamp();

        // Increment in time is done by steps here to allow for multiple IO processing at the right time and allow the timer to be
        // rescheduled accordingly
        while (true)
        {
            ctx.GetIOContext().DriveIO();
            if ((gMockClock.GetMonotonicTimestamp() - startTime) >= System::Clock::Milliseconds32(500))
            {
                break;
            }
            gMockClock.AdvanceMonotonic(System::Clock::Milliseconds32(10));
        }
    }
    // We get one chunk with 4 array elements, and then one chunk per
    // element, and the total size of the array is
    // kMockAttribute4ListLength.
    EXPECT_TRUE(delegate.mNumAttributeResponse == 1 + (kMockAttribute4ListLength - 4));
    EXPECT_TRUE(delegate.mNumArrayItems == 6);

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
}

namespace {

void CheckForInvalidAction(chip::Test::MessageCapturer & messageLog)
{
    EXPECT_TRUE(messageLog.MessageCount() == 1);
    EXPECT_TRUE(messageLog.IsMessageType(0, Protocols::InteractionModel::MsgType::StatusResponse));
    CHIP_ERROR status;
    EXPECT_TRUE(StatusResponse::ProcessStatusResponse(std::move(messageLog.MessagePayload(0)), status) == CHIP_NO_ERROR);
    EXPECT_TRUE(status == CHIP_IM_GLOBAL_STATUS(InvalidAction));
}

} // anonymous namespace

/**
 * Helper macro we can use to pretend we got a reply from the server in cases
 * when the reply was actually dropped due to us not wanting the client's state
 * machine to advance.
 *
 * When this macro is used, the client has sent a message and is waiting for an
 * ack+response, and the server has sent a response that got dropped and is
 * waiting for an ack (and maybe a response).
 *
 * What this macro then needs to do is:
 *
 * 1. Pretend that the client got an ack (and clear out the corresponding ack
 *    state).
 * 2. Pretend that the client got a message from the server, with the id of the
 *    message that was dropped, which requires an ack, so the client will send
 *    that ack in its next message.
 *
 * This is a macro so we get useful line numbers on assertion failures
 */
#define PretendWeGotReplyFromServer(aContext, aClientExchange)                                                                     \
    {                                                                                                                              \
        Messaging::ReliableMessageMgr * localRm    = (aContext).GetExchangeManager().GetReliableMessageMgr();                      \
        Messaging::ExchangeContext * localExchange = aClientExchange;                                                              \
        EXPECT_TRUE(localRm->TestGetCountRetransTable() == 2);                                                                     \
                                                                                                                                   \
        localRm->ClearRetransTable(localExchange);                                                                                 \
        EXPECT_TRUE(localRm->TestGetCountRetransTable() == 1);                                                                     \
                                                                                                                                   \
        localRm->EnumerateRetransTable([localExchange](auto * entry) {                                                             \
            localExchange->SetPendingPeerAckMessageCounter(entry->retainedBuf.GetMessageCounter());                                \
            return Loop::Break;                                                                                                    \
        });                                                                                                                        \
    }

// Read Client sends the read request, Read Handler drops the response, then test injects unknown status reponse message for Read
// Client.
STATIC_TEST(TestReadInteraction, TestReadClientReceiveInvalidMessage)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    {
        app::ReadClient readClient(engine, &ctx.GetExchangeManager(), delegate, chip::app::ReadClient::InteractionType::Read);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        err                                                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        StatusResponseMessage::Builder response;
        response.Init(&writer);
        response.Status(Protocols::InteractionModel::Status::Busy);
        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);
        PayloadHeader payloadHeader;
        payloadHeader.SetExchangeID(0);
        payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::StatusResponse);

        chip::Test::MessageCapturer messageLog(ctx);
        messageLog.mCaptureStandaloneAcks = false;

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        readClient.OnMessageReceived(readClient.mExchange.Get(), payloadHeader, std::move(msgBuf));
        ctx.DrainAndServiceIO();

        // The ReadHandler closed its exchange when it sent the Report Data (which we dropped).
        // Since we synthesized the StatusResponse to the ReadClient, instead of sending it from the ReadHandler,
        // the only messages here are the ReadClient's StatusResponse to the unexpected message and an MRP ack.
        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(Busy));

        CheckForInvalidAction(messageLog);
    }

    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client sends the subscribe request, Read Handler drops the response, then test injects unknown status response message for
// Read Client.
STATIC_TEST(TestReadInteraction, TestSubscribeClientReceiveInvalidStatusResponse)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        err                                                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        StatusResponseMessage::Builder response;
        response.Init(&writer);
        response.Status(Protocols::InteractionModel::Status::Busy);
        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);
        PayloadHeader payloadHeader;
        payloadHeader.SetExchangeID(0);
        payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::StatusResponse);

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        readClient.OnMessageReceived(readClient.mExchange.Get(), payloadHeader, std::move(msgBuf));
        ctx.DrainAndServiceIO();

        // TODO: Need to validate what status is being sent to the ReadHandler
        // The ReadHandler's exchange is closed when we synthesize the subscribe response, since it sent the
        // Subscribe Response as the last message in the transaction.
        // Since we synthesized the subscribe response to the ReadClient, instead of sending it from the ReadHandler,
        // the only messages here are the ReadClient's StatusResponse to the unexpected message and an MRP ack.
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);

        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(Busy));
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client sends the subscribe request, Read Handler drops the response, then test injects well-formed status response message
// with Success for Read Client, we expect the error with CHIP_ERROR_INVALID_MESSAGE_TYPE
STATIC_TEST(TestReadInteraction, TestSubscribeClientReceiveWellFormedStatusResponse)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        err                                                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        StatusResponseMessage::Builder response;
        response.Init(&writer);
        response.Status(Protocols::InteractionModel::Status::Success);
        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);
        PayloadHeader payloadHeader;
        payloadHeader.SetExchangeID(0);
        payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::StatusResponse);

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        readClient.OnMessageReceived(readClient.mExchange.Get(), payloadHeader, std::move(msgBuf));
        ctx.DrainAndServiceIO();

        // TODO: Need to validate what status is being sent to the ReadHandler
        // The ReadHandler's exchange is still open when we synthesize the StatusResponse.
        // Since we synthesized the StatusResponse to the ReadClient, instead of sending it from the ReadHandler,
        // the only messages here are the ReadClient's StatusResponse to the unexpected message and an MRP ack.
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);

        EXPECT_TRUE(delegate.mError == CHIP_ERROR_INVALID_MESSAGE_TYPE);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client sends the subscribe request, Read Handler drops the response, then test injects invalid report message for Read
// Client.
STATIC_TEST(TestReadInteraction, TestSubscribeClientReceiveInvalidReportMessage)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        err                                                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        ReportDataMessage::Builder response;
        response.Init(&writer);
        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);
        PayloadHeader payloadHeader;
        payloadHeader.SetExchangeID(0);
        payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::ReportData);

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        readClient.OnMessageReceived(readClient.mExchange.Get(), payloadHeader, std::move(msgBuf));
        ctx.DrainAndServiceIO();

        // TODO: Need to validate what status is being sent to the ReadHandler
        // The ReadHandler's exchange is still open when we synthesize the ReportData.
        // Since we synthesized the ReportData to the ReadClient, instead of sending it from the ReadHandler,
        // the only messages here are the ReadClient's StatusResponse to the unexpected message and an MRP ack.
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);

        EXPECT_TRUE(delegate.mError == CHIP_ERROR_END_OF_TLV);

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client create the subscription, handler sends unsolicited malformed report to client,
// InteractionModelEngine::OnUnsolicitedReportData would process this malformed report and sends out status report
STATIC_TEST(TestReadInteraction, TestSubscribeClientReceiveUnsolicitedInvalidReportMessage)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    {
        ctx.GetLoopback().mSentMessageCount = 0;
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 5);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        ReportDataMessage::Builder response;
        response.Init(&writer);
        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);

        ctx.GetLoopback().mSentMessageCount = 0;
        auto exchange                       = InteractionModelEngine::GetInstance()->GetExchangeManager()->NewContext(
            delegate.mpReadHandler->mSessionHandle.Get().Value(), delegate.mpReadHandler);
        delegate.mpReadHandler->mExchangeCtx.Grab(exchange);
        err = delegate.mpReadHandler->mExchangeCtx->SendMessage(Protocols::InteractionModel::MsgType::ReportData, std::move(msgBuf),
                                                                Messaging::SendMessageFlags::kExpectResponse);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        // The server sends a data report.
        // The client receives the data report data and sends out status report with invalid action.
        // The server acks the status report.
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 3);
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
}

// Read Client sends the subscribe request, Read Handler drops the subscribe response, then test injects invalid subscribe response
// message
STATIC_TEST(TestReadInteraction, TestSubscribeClientReceiveInvalidSubscribeResponseMessage)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 3;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        err                                                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        SubscribeResponseMessage::Builder response;
        response.Init(&writer);
        response.SubscriptionId(readClient.mSubscriptionId + 1);
        response.MaxInterval(1);
        response.EndOfSubscribeResponseMessage();
        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);
        PayloadHeader payloadHeader;
        payloadHeader.SetExchangeID(0);
        payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::SubscribeResponse);

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 4);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        readClient.OnMessageReceived(readClient.mExchange.Get(), payloadHeader, std::move(msgBuf));
        ctx.DrainAndServiceIO();

        // TODO: Need to validate what status is being sent to the ReadHandler
        // The ReadHandler's exchange is still open when we synthesize the subscribe response.
        // Since we synthesized the subscribe response to the ReadClient, instead of sending it from the ReadHandler,
        // the only messages here are the ReadClient's StatusResponse to the unexpected message and an MRP ack.
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);

        EXPECT_TRUE(delegate.mError == CHIP_ERROR_INVALID_SUBSCRIPTION);
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client create the subscription, handler sends unsolicited malformed report with invalid subscription id to client,
// InteractionModelEngine::OnUnsolicitedReportData would process this malformed report and sends out status report
STATIC_TEST(TestReadInteraction, TestSubscribeClientReceiveUnsolicitedReportMessageWithInvalidSubscriptionId)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    {
        ctx.GetLoopback().mSentMessageCount = 0;
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 5);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        ReportDataMessage::Builder response;
        response.Init(&writer);
        response.SubscriptionId(readClient.mSubscriptionId + 1);
        response.EndOfReportDataMessage();

        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);

        ctx.GetLoopback().mSentMessageCount = 0;
        auto exchange                       = InteractionModelEngine::GetInstance()->GetExchangeManager()->NewContext(
            delegate.mpReadHandler->mSessionHandle.Get().Value(), delegate.mpReadHandler);
        delegate.mpReadHandler->mExchangeCtx.Grab(exchange);
        err = delegate.mpReadHandler->mExchangeCtx->SendMessage(Protocols::InteractionModel::MsgType::ReportData, std::move(msgBuf),
                                                                Messaging::SendMessageFlags::kExpectResponse);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        // The server sends a data report.
        // The client receives the data report data and sends out status report with invalid subsciption.
        // The server should respond with a status report of its own, leading to 4 messages (because
        // the client would ack the server's status report), just sends an ack to the status report it got.
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 3);
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
}

// TestReadChunkingInvalidSubscriptionId will try to read a few large attributes, the report won't fit into the MTU and result in
// chunking, second report has different subscription id from the first one, read client sends out the status report with invalid
// subscription
STATIC_TEST(TestReadInteraction, TestReadChunkingInvalidSubscriptionId)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpEventPathParamsList        = nullptr;
    readPrepareParams.mEventPathParamsListSize     = 0;
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 3;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        err                                                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
        EXPECT_TRUE(!msgBuf.IsNull());
        System::PacketBufferTLVWriter writer;
        writer.Init(std::move(msgBuf));
        ReportDataMessage::Builder response;
        response.Init(&writer);
        response.SubscriptionId(readClient.mSubscriptionId + 1);
        response.EndOfReportDataMessage();
        PayloadHeader payloadHeader;
        payloadHeader.SetExchangeID(0);
        payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::ReportData);

        EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 4);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        readClient.OnMessageReceived(readClient.mExchange.Get(), payloadHeader, std::move(msgBuf));
        ctx.DrainAndServiceIO();

        // TODO: Need to validate what status is being sent to the ReadHandler
        // The ReadHandler's exchange is still open when we synthesize the report data message.
        // Since we synthesized the second report data message to the ReadClient with invalid subscription id, instead of sending it
        // from the ReadHandler, the only messages here are the ReadClient's StatusResponse to the unexpected message and an MRP
        // ack.
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);

        EXPECT_TRUE(delegate.mError == CHIP_ERROR_INVALID_SUBSCRIPTION);
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client sends a malformed read request, interaction model engine fails to parse the request and generates a status report to
// client, and client is closed.
STATIC_TEST(TestReadInteraction, TestReadHandlerMalformedReadRequest1)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);
        System::PacketBufferHandle msgBuf;
        ReadRequestMessage::Builder request;
        System::PacketBufferTLVWriter writer;

        chip::app::InitWriterWithSpaceReserved(writer, 0);
        err = request.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = writer.Finalize(&msgBuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        auto exchange = readClient.mpExchangeMgr->NewContext(readPrepareParams.mSessionHolder.Get().Value(), &readClient);
        EXPECT_TRUE(exchange != nullptr);
        readClient.mExchange.Grab(exchange);
        readClient.MoveToState(app::ReadClient::ClientState::AwaitingInitialReport);
        err = readClient.mExchange->SendMessage(Protocols::InteractionModel::MsgType::ReadRequest, std::move(msgBuf),
                                                Messaging::SendFlags(Messaging::SendMessageFlags::kExpectResponse));
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Read Client sends a malformed read request, read handler fails to parse the request and generates a status report to client, and
// client is closed.
STATIC_TEST(TestReadInteraction, TestReadHandlerMalformedReadRequest2)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);
        System::PacketBufferHandle msgBuf;
        ReadRequestMessage::Builder request;
        System::PacketBufferTLVWriter writer;

        chip::app::InitWriterWithSpaceReserved(writer, 0);
        err = request.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        EXPECT_TRUE(request.EndOfReadRequestMessage() == CHIP_NO_ERROR);
        err = writer.Finalize(&msgBuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        auto exchange = readClient.mpExchangeMgr->NewContext(readPrepareParams.mSessionHolder.Get().Value(), &readClient);
        EXPECT_TRUE(exchange != nullptr);
        readClient.mExchange.Grab(exchange);
        readClient.MoveToState(app::ReadClient::ClientState::AwaitingInitialReport);
        err = readClient.mExchange->SendMessage(Protocols::InteractionModel::MsgType::ReadRequest, std::move(msgBuf),
                                                Messaging::SendFlags(Messaging::SendMessageFlags::kExpectResponse));
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();
        ChipLogError(DataManagement, "The error is %s", ErrorStr(delegate.mError));
        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Read Client sends a malformed subscribe request, interaction model engine fails to parse the request and generates a status
// report to client, and client is closed.
STATIC_TEST(TestReadInteraction, TestReadHandlerMalformedSubscribeRequest)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        System::PacketBufferHandle msgBuf;
        ReadRequestMessage::Builder request;
        System::PacketBufferTLVWriter writer;

        chip::app::InitWriterWithSpaceReserved(writer, 0);
        err = request.Init(&writer);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = writer.Finalize(&msgBuf);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        auto exchange = readClient.mpExchangeMgr->NewContext(readPrepareParams.mSessionHolder.Get().Value(), &readClient);
        EXPECT_TRUE(exchange != nullptr);
        readClient.mExchange.Grab(exchange);
        readClient.MoveToState(app::ReadClient::ClientState::AwaitingInitialReport);
        err = readClient.mExchange->SendMessage(Protocols::InteractionModel::MsgType::ReadRequest, std::move(msgBuf),
                                                Messaging::SendFlags(Messaging::SendMessageFlags::kExpectResponse));
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Read Client creates a subscription with the server, server sends chunked reports, after the handler sends out the first chunked
// report, client sends out invalid write request message, handler sends status report with invalid action and closes
STATIC_TEST(TestReadInteraction, TestSubscribeSendUnknownMessage)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
        err                                                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);

        ctx.GetLoopback().mSentMessageCount = 0;

        // Server sends out status report, client should send status report along with Piggybacking ack, but we don't do that
        // Instead, we send out unknown message to server

        System::PacketBufferHandle msgBuf;
        ReadRequestMessage::Builder request;
        System::PacketBufferTLVWriter writer;
        chip::app::InitWriterWithSpaceReserved(writer, 0);
        request.Init(&writer);
        writer.Finalize(&msgBuf);

        err = readClient.mExchange->SendMessage(Protocols::InteractionModel::MsgType::WriteRequest, std::move(msgBuf));
        ctx.DrainAndServiceIO();
        // client sends invalid write request, server sends out status report with invalid action and closes, client replies with
        // status report server replies with MRP Ack
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 4);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client creates a subscription with the server, server sends chunked reports, after the handler sends out invalid status
// report, client sends out invalid status report message, handler sends status report with invalid action and close
STATIC_TEST(TestReadInteraction, TestSubscribeSendInvalidStatusReport)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        // Since we are dropping packets, things are not getting acked.  Set up
        // our MRP state to look like what it would have looked like if the
        // packet had not gotten dropped.
        PretendWeGotReplyFromServer(ctx, readClient.mExchange.Get());

        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        ctx.GetLoopback().mSentMessageCount = 0;

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);

        System::PacketBufferHandle msgBuf;
        StatusResponseMessage::Builder request;
        System::PacketBufferTLVWriter writer;
        chip::app::InitWriterWithSpaceReserved(writer, 0);
        request.Init(&writer);
        writer.Finalize(&msgBuf);

        err = readClient.mExchange->SendMessage(Protocols::InteractionModel::MsgType::StatusResponse, std::move(msgBuf));
        ctx.DrainAndServiceIO();

        // client sends malformed status response, server sends out status report with invalid action and close, client replies with
        // status report server replies with MRP Ack
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 4);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Read Client sends a malformed subscribe request, the server fails to parse the request and generates a status report to the
// client, and client closes itself.
STATIC_TEST(TestReadInteraction, TestReadHandlerInvalidSubscribeRequest)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        System::PacketBufferHandle msgBuf;
        ReadRequestMessage::Builder request;
        System::PacketBufferTLVWriter writer;

        chip::app::InitWriterWithSpaceReserved(writer, 0);
        err = request.Init(&writer);
        err = writer.Finalize(&msgBuf);

        auto exchange = readClient.mpExchangeMgr->NewContext(readPrepareParams.mSessionHolder.Get().Value(), &readClient);
        EXPECT_TRUE(exchange != nullptr);
        readClient.mExchange.Grab(exchange);
        readClient.MoveToState(app::ReadClient::ClientState::AwaitingInitialReport);
        err = readClient.mExchange->SendMessage(Protocols::InteractionModel::MsgType::SubscribeRequest, std::move(msgBuf),
                                                Messaging::SendFlags(Messaging::SendMessageFlags::kExpectResponse));
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();
        EXPECT_TRUE(delegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Create the subscription, then remove the corresponding fabric in client and handler, the corresponding
// client and handler would be released as well.
STATIC_TEST(TestReadInteraction, TestSubscribeInvalidateFabric)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = new chip::app::AttributePathParams[1];
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = chip::Test::kMockEndpoint3;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = chip::Test::MockClusterId(2);
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = chip::Test::MockAttributeId(1);

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 0;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        delegate.mGotReport = false;

        err = readClient.SendAutoResubscribeRequest(std::move(readPrepareParams));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        ctx.GetFabricTable().Delete(ctx.GetAliceFabricIndex());
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 0);
        ctx.GetFabricTable().Delete(ctx.GetBobFabricIndex());
        EXPECT_TRUE(delegate.mError == CHIP_ERROR_IM_FABRIC_DELETED);
        ctx.ExpireSessionAliceToBob();
        ctx.ExpireSessionBobToAlice();
        ctx.CreateAliceFabric();
        ctx.CreateBobFabric();
        ctx.CreateSessionAliceToBob();
        ctx.CreateSessionBobToAlice();
    }
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestShutdownSubscription)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = new chip::app::AttributePathParams[1];
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = chip::Test::kMockEndpoint3;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = chip::Test::MockClusterId(2);
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = chip::Test::MockAttributeId(1);

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 0;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        delegate.mGotReport = false;

        err = readClient.SendAutoResubscribeRequest(std::move(readPrepareParams));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);

        engine->ShutdownSubscription(chip::ScopedNodeId(readClient.GetPeerNodeId(), readClient.GetFabricIndex()),
                                     readClient.GetSubscriptionId().Value());
        EXPECT_TRUE(readClient.IsIdle());
    }
    engine->Shutdown();
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestSubscribeUrgentWildcardEvent)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    MockInteractionModelApp nonUrgentDelegate;
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);
    EXPECT_TRUE(!nonUrgentDelegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    readPrepareParams.mpAttributePathParamsList    = nullptr;
    readPrepareParams.mAttributePathParamsListSize = 0;

    readPrepareParams.mMinIntervalFloorSeconds   = 1;
    readPrepareParams.mMaxIntervalCeilingSeconds = 3600;
    printf("\nSend first subscribe request message with wildcard urgent event to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    readPrepareParams.mKeepSubscriptions = true;

    {
        app::ReadClient nonUrgentReadClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(),
                                            nonUrgentDelegate, chip::app::ReadClient::InteractionType::Subscribe);
        nonUrgentDelegate.mGotReport = false;
        err                          = nonUrgentReadClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        readPrepareParams.mpEventPathParamsList[0].mIsUrgentEvent = true;
        delegate.mGotReport                                       = false;
        err                                                       = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        System::Clock::Timestamp startTime = gMockClock.GetMonotonicTimestamp();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 2);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        nonUrgentDelegate.mpReadHandler = engine->ActiveHandlerAt(0);
        EXPECT_TRUE(engine->ActiveHandlerAt(1) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(1);

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(nonUrgentDelegate.mGotEventResponse);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 2);

        GenerateEvents();

        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) > startTime);
        EXPECT_TRUE(delegate.mpReadHandler->IsDirty());
        delegate.mGotEventResponse = false;
        delegate.mGotReport        = false;

        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(nonUrgentDelegate.mpReadHandler) > startTime);
        EXPECT_TRUE(!nonUrgentDelegate.mpReadHandler->IsDirty());
        nonUrgentDelegate.mGotEventResponse = false;
        nonUrgentDelegate.mGotReport        = false;

        // wait for min interval 1 seconds (in test, we use 0.6 seconds considering the time variation), expect no event is
        // received, then wait for 0.8 seconds, then the urgent event would be sent out
        //  currently DriveIOUntil will call `DriveIO` at least once, which means that if there is any CPU scheduling issues,
        // there's a chance 1.9s will already have elapsed by the time we get there, which will result in DriveIO being called when
        // it shouldn't. Better fix could happen inside DriveIOUntil, not sure the sideeffect there.

        // Advance monotonic looping to allow events to trigger
        gMockClock.AdvanceMonotonic(System::Clock::Milliseconds32(600));
        ctx.GetIOContext().DriveIO();

        EXPECT_TRUE(!delegate.mGotEventResponse);
        EXPECT_TRUE(!nonUrgentDelegate.mGotEventResponse);

        // Advance monotonic timestamp for min interval to elapse
        startTime = gMockClock.GetMonotonicTimestamp();
        gMockClock.AdvanceMonotonic(System::Clock::Milliseconds32(800));

        // Service Timer expired event
        ctx.GetIOContext().DriveIO();

        // Service Engine Run
        ctx.GetIOContext().DriveIO();

        // Service EventManagement event
        ctx.GetIOContext().DriveIO();

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(!nonUrgentDelegate.mGotEventResponse);

        // Since we just sent a report for our urgent subscription, the min interval of the urgent subcription should have been
        // updated
        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) > gMockClock.GetMonotonicTimestamp());
        EXPECT_TRUE(!delegate.mpReadHandler->IsDirty());
        delegate.mGotEventResponse = false;

        // For our non-urgent subscription, we did not send anything, so the min interval should of the non urgent subcription
        // should be in the past
        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(nonUrgentDelegate.mpReadHandler) <
                    gMockClock.GetMonotonicTimestamp());
        EXPECT_TRUE(!nonUrgentDelegate.mpReadHandler->IsDirty());

        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Milliseconds32(2100));
        ctx.GetIOContext().DriveIO();

        // No reporting should have happened.
        EXPECT_TRUE(!delegate.mGotEventResponse);
        EXPECT_TRUE(!nonUrgentDelegate.mGotEventResponse);

        // The min-interval should have elapsed for the urgent subscription, and our handler should still
        // not be dirty or reportable.
        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(delegate.mpReadHandler) <
                    System::SystemClock().GetMonotonicTimestamp());
        EXPECT_TRUE(!delegate.mpReadHandler->IsDirty());
        EXPECT_TRUE(!delegate.mpReadHandler->ShouldStartReporting());

        // And the non-urgent one should not have changed state either, since
        // it's waiting for the max-interval.
        EXPECT_TRUE(reportScheduler->GetMinTimestampForHandler(nonUrgentDelegate.mpReadHandler) <
                    System::SystemClock().GetMonotonicTimestamp());
        EXPECT_TRUE(reportScheduler->GetMaxTimestampForHandler(nonUrgentDelegate.mpReadHandler) >
                    System::SystemClock().GetMonotonicTimestamp());
        EXPECT_TRUE(!nonUrgentDelegate.mpReadHandler->IsDirty());
        EXPECT_TRUE(!nonUrgentDelegate.mpReadHandler->ShouldStartReporting());

        // There should be no reporting run scheduled.  This is very important;
        // otherwise we can get a false-positive pass below because the run was
        // already scheduled by here.
        EXPECT_TRUE(!InteractionModelEngine::GetInstance()->GetReportingEngine().IsRunScheduled());

        // Generate some events, which should get reported.
        GenerateEvents();

        // Urgent read handler should now be dirty, and reportable.
        EXPECT_TRUE(delegate.mpReadHandler->IsDirty());
        EXPECT_TRUE(delegate.mpReadHandler->ShouldStartReporting());
        EXPECT_TRUE(reportScheduler->IsReadHandlerReportable(delegate.mpReadHandler));

        // Non-urgent read handler should not be reportable.
        EXPECT_TRUE(!nonUrgentDelegate.mpReadHandler->IsDirty());
        EXPECT_TRUE(!nonUrgentDelegate.mpReadHandler->ShouldStartReporting());

        // Still no reporting should have happened.
        EXPECT_TRUE(!delegate.mGotEventResponse);
        EXPECT_TRUE(!nonUrgentDelegate.mGotEventResponse);

        ctx.DrainAndServiceIO();

        // Should get those urgent events reported.
        EXPECT_TRUE(delegate.mGotEventResponse);

        // Should get nothing reported on the non-urgent handler.
        EXPECT_TRUE(!nonUrgentDelegate.mGotEventResponse);
    }

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestSubscribeWildcard)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mEventPathParamsListSize = 0;

    std::unique_ptr<chip::app::AttributePathParams[]> attributePathParams(new chip::app::AttributePathParams[2]);
    // Subscribe to full wildcard paths, repeat twice to ensure chunking.
    readPrepareParams.mpAttributePathParamsList    = attributePathParams.get();
    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 1;
    printf("\nSend subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        delegate.mGotReport = false;

        attributePathParams.release();
        err = readClient.SendAutoResubscribeRequest(std::move(readPrepareParams));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);

#if CHIP_CONFIG_ENABLE_EVENTLIST_ATTRIBUTE
        // Mock attribute storage in src/app/util/mock/attribute-storage.cpp
        // has the following items
        // - Endpoint 0xFFFE
        //    - cluster 0xFFF1'FC01 (2 attributes)
        //    - cluster 0xFFF1'FC02 (3 attributes)
        // - Endpoint 0xFFFD
        //    - cluster 0xFFF1'FC01 (2 attributes)
        //    - cluster 0xFFF1'FC02 (4 attributes)
        //    - cluster 0xFFF1'FC03 (5 attributes)
        // - Endpoint 0xFFFC
        //    - cluster 0xFFF1'FC01 (3 attributes)
        //    - cluster 0xFFF1'FC02 (6 attributes)
        //    - cluster 0xFFF1'FC03 (2 attributes)
        //    - cluster 0xFFF1'FC04 (2 attributes)
        //
        // For at total of 29 attributes. There are two wildcard subscription
        // paths, for a total of 58 attributes.
        //
        // Attribute 0xFFFC::0xFFF1'FC02::0xFFF1'0004 (kMockEndpoint3::MockClusterId(2)::MockAttributeId(4))
        // is a list of kMockAttribute4ListLength elements of size 256 bytes each, which cannot fit in a single
        // packet, so gets list chunking applied to it.
        //
        // Because delegate.mNumAttributeResponse counts AttributeDataIB instances, not attributes,
        // the count will depend on exactly how the list for attribute
        // 0xFFFC::0xFFF1'FC02::0xFFF1'0004 is chunked.  For each of the two instances of that attribute
        // in the response, there will be one AttributeDataIB for the start of the list (which will include
        // some number of 256-byte elements), then one AttributeDataIB for each of the remaining elements.
        //
        // When EventList is enabled, for the first report for the list attribute we receive three
        // of its items in the initial list, then the remaining items.  For the second report we
        // receive 2 items in the initial list followed by the remaining items.
        constexpr size_t kExpectedAttributeResponse = 29 * 2 + (kMockAttribute4ListLength - 3) + (kMockAttribute4ListLength - 2);
#else
        // When EventList is not enabled, the packet boundaries shift and for the first
        // report for the list attribute we receive four of its items in the initial list,
        // then additional items.  For the second report we receive 4 items in
        // the initial list followed by additional items.
        constexpr size_t kExpectedAttributeResponse = 29 * 2 + (kMockAttribute4ListLength - 4) + (kMockAttribute4ListLength - 4);
#endif
        EXPECT_TRUE(delegate.mNumAttributeResponse == kExpectedAttributeResponse);
        EXPECT_TRUE(delegate.mNumArrayItems == 12);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        // Set a concrete path dirty
        {
            delegate.mGotReport            = false;
            delegate.mNumAttributeResponse = 0;

            AttributePathParams dirtyPath;
            dirtyPath.mEndpointId  = chip::Test::kMockEndpoint2;
            dirtyPath.mClusterId   = chip::Test::MockClusterId(3);
            dirtyPath.mAttributeId = chip::Test::MockAttributeId(1);

            err = engine->GetReportingEngine().SetDirty(dirtyPath);
            EXPECT_TRUE(err == CHIP_NO_ERROR);

            ctx.DrainAndServiceIO();

            EXPECT_TRUE(delegate.mGotReport);
            // We subscribed wildcard path twice, so we will receive two reports here.
            EXPECT_TRUE(delegate.mNumAttributeResponse == 2);
        }

        // Set a endpoint dirty
        {
            delegate.mGotReport            = false;
            delegate.mNumAttributeResponse = 0;
            delegate.mNumArrayItems        = 0;

            AttributePathParams dirtyPath;
            dirtyPath.mEndpointId = chip::Test::kMockEndpoint3;

            err = engine->GetReportingEngine().SetDirty(dirtyPath);
            EXPECT_TRUE(err == CHIP_NO_ERROR);

            //
            // We need to DrainAndServiceIO() until attribute callback will be called.
            // This is not correct behavior and is tracked in Issue #17528.
            //
            int last;
            do
            {
                last = delegate.mNumAttributeResponse;
                ctx.DrainAndServiceIO();
            } while (last != delegate.mNumAttributeResponse);

            // Mock endpoint3 has 13 attributes in total, and we subscribed twice.
            // And attribute 3/2/4 is a list with 6 elements and list chunking
            // is applied to it, but the way the packet boundaries fall we get two of
            // its items as a single list, followed by 4 more items for one
            // of our subscriptions, and 3 items as a single list followed by 3
            // more items for the other.
            //
            // Thus we should receive 13*2 + 4 + 3 = 33 attribute data in total.
            ChipLogError(DataManagement, "RESPO: %d\n", delegate.mNumAttributeResponse);
            EXPECT_TRUE(delegate.mNumAttributeResponse == 33);
            EXPECT_TRUE(delegate.mNumArrayItems == 12);
        }
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Subscribe (wildcard, C3, A1), then setDirty (E2, C3, wildcard), receive one attribute after setDirty
STATIC_TEST(TestReadInteraction, TestSubscribePartialOverlap)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mEventPathParamsListSize = 0;

    std::unique_ptr<chip::app::AttributePathParams[]> attributePathParams(new chip::app::AttributePathParams[2]);
    attributePathParams[0].mClusterId              = chip::Test::MockClusterId(3);
    attributePathParams[0].mAttributeId            = chip::Test::MockAttributeId(1);
    readPrepareParams.mpAttributePathParamsList    = attributePathParams.get();
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 1;
    printf("\nSend subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        delegate.mGotReport = false;

        attributePathParams.release();
        err = readClient.SendAutoResubscribeRequest(std::move(readPrepareParams));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);

        EXPECT_TRUE(delegate.mNumAttributeResponse == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        // Set a partial overlapped path dirty
        {
            delegate.mGotReport            = false;
            delegate.mNumAttributeResponse = 0;

            AttributePathParams dirtyPath;
            dirtyPath.mEndpointId = chip::Test::kMockEndpoint2;
            dirtyPath.mClusterId  = chip::Test::MockClusterId(3);

            err = engine->GetReportingEngine().SetDirty(dirtyPath);
            EXPECT_TRUE(err == CHIP_NO_ERROR);

            ctx.DrainAndServiceIO();

            EXPECT_TRUE(delegate.mGotReport);
            EXPECT_TRUE(delegate.mNumAttributeResponse == 1);
            EXPECT_TRUE(delegate.mReceivedAttributePaths[0].mEndpointId == chip::Test::kMockEndpoint2);
            EXPECT_TRUE(delegate.mReceivedAttributePaths[0].mClusterId == chip::Test::MockClusterId(3));
            EXPECT_TRUE(delegate.mReceivedAttributePaths[0].mAttributeId == chip::Test::MockAttributeId(1));
        }
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Subscribe (E2, C3, A1), then setDirty (wildcard, wildcard, wildcard), receive one attribute after setDirty
STATIC_TEST(TestReadInteraction, TestSubscribeSetDirtyFullyOverlap)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mEventPathParamsListSize = 0;

    std::unique_ptr<chip::app::AttributePathParams[]> attributePathParams(new chip::app::AttributePathParams[1]);
    attributePathParams[0].mClusterId              = chip::Test::kMockEndpoint2;
    attributePathParams[0].mClusterId              = chip::Test::MockClusterId(3);
    attributePathParams[0].mAttributeId            = chip::Test::MockAttributeId(1);
    readPrepareParams.mpAttributePathParamsList    = attributePathParams.get();
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 1;
    printf("\nSend subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        delegate.mGotReport = false;

        attributePathParams.release();
        err = readClient.SendAutoResubscribeRequest(std::move(readPrepareParams));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);

        EXPECT_TRUE(delegate.mNumAttributeResponse == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        // Set a full overlapped path dirty and expect to receive one E2C3A1
        {
            delegate.mGotReport            = false;
            delegate.mNumAttributeResponse = 0;

            AttributePathParams dirtyPath;
            err = engine->GetReportingEngine().SetDirty(dirtyPath);
            EXPECT_TRUE(err == CHIP_NO_ERROR);

            ctx.DrainAndServiceIO();

            EXPECT_TRUE(delegate.mGotReport);
            EXPECT_TRUE(delegate.mNumAttributeResponse == 1);
            EXPECT_TRUE(delegate.mReceivedAttributePaths[0].mEndpointId == chip::Test::kMockEndpoint2);
            EXPECT_TRUE(delegate.mReceivedAttributePaths[0].mClusterId == chip::Test::MockClusterId(3));
            EXPECT_TRUE(delegate.mReceivedAttributePaths[0].mAttributeId == chip::Test::MockAttributeId(1));
        }
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Verify that subscription can be shut down just after receiving SUBSCRIBE RESPONSE,
// before receiving any subsequent REPORT DATA.
STATIC_TEST(TestReadInteraction, TestSubscribeEarlyShutdown)
{
    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    InteractionModelEngine & engine    = *InteractionModelEngine::GetInstance();
    MockInteractionModelApp delegate;

    // Initialize Interaction Model Engine
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    EXPECT_TRUE(engine.Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler()) ==
                CHIP_NO_ERROR);

    // Subscribe to the attribute
    AttributePathParams attributePathParams;
    attributePathParams.mEndpointId  = kTestEndpointId;
    attributePathParams.mClusterId   = kTestClusterId;
    attributePathParams.mAttributeId = 1;

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = &attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;
    readPrepareParams.mMinIntervalFloorSeconds     = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds   = 5;
    readPrepareParams.mKeepSubscriptions           = false;

    printf("Send subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        EXPECT_TRUE(readClient.SendRequest(readPrepareParams) == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 1);
        EXPECT_TRUE(engine.GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        EXPECT_TRUE(engine.ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine.ActiveHandlerAt(0);
        EXPECT_TRUE(delegate.mpReadHandler != nullptr);
    }

    // Cleanup
    EXPECT_TRUE(engine.GetNumActiveReadClients() == 0);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    engine.Shutdown();

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestSubscribeInvalidAttributePathRoundtrip)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::AttributePathParams attributePathParams[1];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kInvalidTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mSessionHolder.Grab(ctx.GetSessionBobToAlice());
    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 1;
    printf("\nSend subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        EXPECT_TRUE(readClient.SendRequest(readPrepareParams) == CHIP_NO_ERROR);

        delegate.mNumAttributeResponse = 0;

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);

        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        uint16_t minInterval;
        uint16_t maxInterval;
        delegate.mpReadHandler->GetReportingIntervals(minInterval, maxInterval);

        // Advance monotonic timestamp for min interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(maxInterval));
        ctx.GetIOContext().DriveIO();

        EXPECT_TRUE(engine->GetReportingEngine().IsRunScheduled());
        EXPECT_TRUE(engine->GetReportingEngine().IsRunScheduled());

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestReadInvalidAttributePathRoundtrip)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[2];
    attributePathParams[0].mEndpointId  = kTestEndpointId;
    attributePathParams[0].mClusterId   = kInvalidTestClusterId;
    attributePathParams[0].mAttributeId = 1;

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
        // By now we should have closed all exchanges and sent all pending acks, so
        // there should be no queued-up things in the retransmit table.
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    }

    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestSubscribeInvalidInterval)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::AttributePathParams attributePathParams[1];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mSessionHolder.Grab(ctx.GetSessionBobToAlice());
    readPrepareParams.mMinIntervalFloorSeconds   = 6;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        EXPECT_TRUE(readClient.SendRequest(readPrepareParams) == CHIP_ERROR_INVALID_ARGUMENT);

        printf("\nSend subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

        ctx.DrainAndServiceIO();
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);

    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestSubscribeRoundtripStatusReportTimeout)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestEventIdDebug;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    delegate.mNumAttributeResponse       = 0;
    readPrepareParams.mKeepSubscriptions = false;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        printf("\nSend first subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);
        delegate.mGotReport = false;
        err                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.ExpireSessionAliceToBob();

        ctx.DrainAndServiceIO();

        ctx.ExpireSessionBobToAlice();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
    }

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

STATIC_TEST(TestReadInteraction, TestPostSubscribeRoundtripStatusReportTimeout)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestEventIdDebug;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    chip::app::AttributePathParams attributePathParams[2];
    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mpAttributePathParamsList[1].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[1].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[1].mAttributeId = 2;

    readPrepareParams.mAttributePathParamsListSize = 2;

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 1;

    delegate.mNumAttributeResponse       = 0;
    readPrepareParams.mKeepSubscriptions = false;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        printf("\nSend first subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);
        delegate.mGotReport = false;
        err                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);

        GenerateEvents();
        chip::app::AttributePathParams dirtyPath1;
        dirtyPath1.mClusterId   = kTestClusterId;
        dirtyPath1.mEndpointId  = kTestEndpointId;
        dirtyPath1.mAttributeId = 1;

        chip::app::AttributePathParams dirtyPath2;
        dirtyPath2.mClusterId   = kTestClusterId;
        dirtyPath2.mEndpointId  = kTestEndpointId;
        dirtyPath2.mAttributeId = 2;

        // Test report with 2 different path
        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;

        err = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath2);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 2);

        // Wait for max interval to elapse
        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(readPrepareParams.mMaxIntervalCeilingSeconds));
        ctx.GetIOContext().DriveIO();

        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;
        ctx.ExpireSessionBobToAlice();

        err = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        err = engine->GetReportingEngine().SetDirty(dirtyPath2);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        EXPECT_TRUE(engine->GetReportingEngine().IsRunScheduled());

        ctx.DrainAndServiceIO();

        ctx.ExpireSessionAliceToBob();
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
    }

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

STATIC_TEST(TestReadInteraction, TestReadChunkingStatusReportTimeout)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    GenerateEvents();

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpEventPathParamsList        = nullptr;
    readPrepareParams.mEventPathParamsListSize     = 0;
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        err = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.ExpireSessionAliceToBob();
        ctx.DrainAndServiceIO();
        ctx.ExpireSessionBobToAlice();

        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);
        // By now we should have closed all exchanges and sent all pending acks, so
        // there should be no queued-up things in the retransmit table.
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// ReadClient sends the read request, but handler fails to send the one report (SendMessage returns an error).
// Since this is an un-chunked read, we are not in the AwaitingReportResponse state, so the "reports in flight"
// counter should not increase.
STATIC_TEST(TestReadInteraction, TestReadReportFailure)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    chip::app::AttributePathParams attributePathParams[1];
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint2;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(3);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(1);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpEventPathParamsList        = nullptr;
    readPrepareParams.mEventPathParamsListSize     = 0;
    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Read);

        ctx.GetLoopback().mNumMessagesToAllowBeforeError = 1;
        ctx.GetLoopback().mMessageSendError              = CHIP_ERROR_INCORRECT_STATE;
        err                                              = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);

        ctx.GetLoopback().mNumMessagesToAllowBeforeError = 0;
        ctx.GetLoopback().mMessageSendError              = CHIP_NO_ERROR;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestReadInteraction, TestSubscribeRoundtripChunkStatusReportTimeout)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestEventIdDebug;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 2;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;

    delegate.mNumAttributeResponse       = 0;
    readPrepareParams.mKeepSubscriptions = false;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        printf("\nSend first subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);
        delegate.mGotReport = false;
        err                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.ExpireSessionAliceToBob();
        ctx.DrainAndServiceIO();
        ctx.ExpireSessionBobToAlice();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
    }

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

STATIC_TEST(TestReadInteraction, TestPostSubscribeRoundtripChunkStatusReportTimeout)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestEventIdDebug;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 1;

    delegate.mNumAttributeResponse       = 0;
    readPrepareParams.mKeepSubscriptions = false;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        printf("\nSend first subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);
        delegate.mGotReport = false;
        err                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);

        GenerateEvents();
        chip::app::AttributePathParams dirtyPath1;
        dirtyPath1.mClusterId   = chip::Test::MockClusterId(2);
        dirtyPath1.mEndpointId  = chip::Test::kMockEndpoint3;
        dirtyPath1.mAttributeId = chip::Test::MockAttributeId(4);

        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(readPrepareParams.mMaxIntervalCeilingSeconds));
        ctx.GetIOContext().DriveIO();

        err = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        ctx.DrainAndServiceIO();
        // Drop status report for the first chunked report, then expire session, handler would be timeout
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 1);
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);

        ctx.ExpireSessionAliceToBob();
        ctx.ExpireSessionBobToAlice();
        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 0);
        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

STATIC_TEST(TestReadInteraction, TestPostSubscribeRoundtripChunkReportTimeout)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!delegate.mGotEventResponse);

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    chip::app::EventPathParams eventPathParams[2];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestEventIdDebug;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEventEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestEventClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestEventIdCritical;

    readPrepareParams.mEventPathParamsListSize = 2;

    chip::app::AttributePathParams attributePathParams[1];
    // Mock Attribute 4 is a big attribute, with 6 large OCTET_STRING
    attributePathParams[0].mEndpointId  = chip::Test::kMockEndpoint3;
    attributePathParams[0].mClusterId   = chip::Test::MockClusterId(2);
    attributePathParams[0].mAttributeId = chip::Test::MockAttributeId(4);

    readPrepareParams.mpAttributePathParamsList    = attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 1;

    delegate.mNumAttributeResponse       = 0;
    readPrepareParams.mKeepSubscriptions = false;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);
        printf("\nSend first subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);
        delegate.mGotReport = false;
        err                 = readClient.SendRequest(readPrepareParams);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(engine->GetNumActiveReadHandlers() == 1);
        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        delegate.mpReadHandler = engine->ActiveHandlerAt(0);

        EXPECT_TRUE(delegate.mGotEventResponse);
        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);

        GenerateEvents();
        chip::app::AttributePathParams dirtyPath1;
        dirtyPath1.mClusterId   = chip::Test::MockClusterId(2);
        dirtyPath1.mEndpointId  = chip::Test::kMockEndpoint3;
        dirtyPath1.mAttributeId = chip::Test::MockAttributeId(4);

        gMockClock.AdvanceMonotonic(System::Clock::Seconds16(readPrepareParams.mMaxIntervalCeilingSeconds));
        ctx.GetIOContext().DriveIO();

        err = engine->GetReportingEngine().SetDirty(dirtyPath1);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;

        // Drop second chunked report then expire session, client would be timeout
        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 1;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 2;
        ctx.GetLoopback().mDroppedMessageCount              = 0;

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 1);
        EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 3);
        EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);

        ctx.ExpireSessionAliceToBob();
        ctx.ExpireSessionBobToAlice();
        EXPECT_TRUE(delegate.mError == CHIP_ERROR_TIMEOUT);

        ctx.GetLoopback().mSentMessageCount                 = 0;
        ctx.GetLoopback().mNumMessagesToDrop                = 0;
        ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
        ctx.GetLoopback().mDroppedMessageCount              = 0;
    }

    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    engine->Shutdown();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

STATIC_TEST(TestReadInteraction, TestReadShutdown)
{
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    app::ReadClient * pClients[4];
    MockInteractionModelApp delegate;

    //
    // Allocate a number of clients
    //
    for (auto & client : pClients)
    {
        client = Platform::New<app::ReadClient>(engine, &ctx.GetExchangeManager(), delegate,
                                                chip::app::ReadClient::InteractionType::Subscribe);
    }

    //
    // Delete every other client to ensure we test out
    // deleting clients from the list of clients tracked by the IM
    //
    Platform::Delete(pClients[1]);
    Platform::Delete(pClients[3]);

    //
    // Shutdown the engine first so that we can
    // de-activate the internal list.
    //
    engine->Shutdown();

    //
    // Shutdown the read clients. These should
    // safely destruct without causing any egregious
    // harm
    //
    Platform::Delete(pClients[0]);
    Platform::Delete(pClients[2]);
}

/**
 * Tests what happens when a subscription tries to deliver reports but the
 * session it has is defunct.  Makes sure we correctly tear down the ReadHandler
 * and don't increment the "reports in flight" count.
 */
STATIC_TEST(TestReadInteraction, TestSubscriptionReportWithDefunctSession)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    MockInteractionModelApp delegate;
    ReportSchedulerImpl * reportScheduler = app::reporting::GetDefaultReportScheduler();
    auto * engine                         = chip::app::InteractionModelEngine::GetInstance();
    err                                   = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), reportScheduler);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    AttributePathParams subscribePath(chip::Test::kMockEndpoint3, chip::Test::MockClusterId(2), chip::Test::MockAttributeId(1));

    ReadPrepareParams readPrepareParams(ctx.GetSessionBobToAlice());
    readPrepareParams.mpAttributePathParamsList    = &subscribePath;
    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 0;
    readPrepareParams.mMaxIntervalCeilingSeconds = 0;

    {
        app::ReadClient readClient(chip::app::InteractionModelEngine::GetInstance(), &ctx.GetExchangeManager(), delegate,
                                   chip::app::ReadClient::InteractionType::Subscribe);

        delegate.mGotReport = false;

        err = readClient.SendSubscribeRequest(std::move(readPrepareParams));
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Read) == 0);
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);

        EXPECT_TRUE(engine->ActiveHandlerAt(0) != nullptr);
        auto * readHandler = engine->ActiveHandlerAt(0);

        // Verify that the session we will reset later is the one we will mess
        // with now.
        EXPECT_TRUE(SessionHandle(*readHandler->GetSession()) == ctx.GetSessionAliceToBob());

        // Test that we send reports as needed.
        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;
        engine->GetReportingEngine().SetDirty(subscribePath);
        ctx.DrainAndServiceIO();

        EXPECT_TRUE(delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 1);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Read) == 0);
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);

        // Test that if the session is defunct we don't send reports and clean
        // up properly.
        readHandler->GetSession()->MarkAsDefunct();
        delegate.mGotReport            = false;
        delegate.mNumAttributeResponse = 0;
        engine->GetReportingEngine().SetDirty(subscribePath);

        ctx.DrainAndServiceIO();

        EXPECT_TRUE(!delegate.mGotReport);
        EXPECT_TRUE(delegate.mNumAttributeResponse == 0);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Subscribe) == 0);
        EXPECT_TRUE(engine->GetNumActiveReadHandlers(ReadHandler::InteractionType::Read) == 0);
        EXPECT_TRUE(engine->GetReportingEngine().GetNumReportsInFlight() == 0);
    }
    engine->Shutdown();
    EXPECT_TRUE(engine->GetNumActiveReadClients() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);

    // Get rid of our defunct session.
    ctx.ExpireSessionAliceToBob();
    ctx.CreateSessionAliceToBob();
}

} // namespace app
} // namespace chip
