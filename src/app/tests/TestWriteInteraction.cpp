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

#include <gtest/gtest.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app/InteractionModelEngine.h>
#include <app/reporting/tests/MockReportScheduler.h>
#include <app/tests/AppTestContext.h>
#include <credentials/GroupDataProviderImpl.h>
#include <crypto/DefaultSessionKeystore.h>
#include <lib/core/CHIPCore.h>
#include <lib/core/ErrorStr.h>
#include <lib/core/TLV.h>
#include <lib/core/TLVDebug.h>
#include <lib/core/TLVUtilities.h>
#include <lib/support/TestGroupData.h>
#include <lib/support/TestPersistentStorageDelegate.h>
#include <lib/support/UnitTestContext.h>

#include <messaging/ExchangeContext.h>
#include <messaging/Flags.h>

#include <memory>
#include <utility>

using TestContext = chip::Test::AppContext;

namespace {

uint8_t attributeDataTLV[CHIP_CONFIG_DEFAULT_UDP_MTU_SIZE];
size_t attributeDataTLVLen                       = 0;
constexpr chip::DataVersion kRejectedDataVersion = 1;
constexpr chip::DataVersion kAcceptedDataVersion = 5;
constexpr uint16_t kMaxGroupsPerFabric           = 5;
constexpr uint16_t kMaxGroupKeysPerFabric        = 8;

chip::TestPersistentStorageDelegate gTestStorage;
chip::Crypto::DefaultSessionKeystore gSessionKeystore;
chip::Credentials::GroupDataProviderImpl gGroupsProvider(kMaxGroupsPerFabric, kMaxGroupKeysPerFabric);

} // namespace

namespace chip {
namespace app {
class TestWriteInteraction : public ::testing::Test
{
public:
    // Performs setup for each individual test in the test suite
    void SetUp() override
    {
        ctx.SetUp();

        gTestStorage.ClearStorage();
        gGroupsProvider.SetStorageDelegate(&gTestStorage);
        gGroupsProvider.SetSessionKeystore(&gSessionKeystore);
        // TODO: use ASSERT_EQ, once transition to pw_unit_test is complete
        ASSERT_EQ(gGroupsProvider.Init(), CHIP_NO_ERROR);
        chip::Credentials::SetGroupDataProvider(&gGroupsProvider);

        uint8_t buf[sizeof(chip::CompressedFabricId)];
        chip::MutableByteSpan span(buf);
        ASSERT_EQ(ctx.GetBobFabric()->GetCompressedFabricIdBytes(span), CHIP_NO_ERROR);
        ASSERT_EQ(chip::GroupTesting::InitData(&gGroupsProvider, ctx.GetBobFabricIndex(), span), CHIP_NO_ERROR);
    }

    // Performs teardown for each individual test in the test suite
    void TearDown() override
    {
        chip::Credentials::GroupDataProvider * provider = chip::Credentials::GetGroupDataProvider();
        if (provider != nullptr)
            provider->Finish();
        ctx.TearDown();
    }

    static void TestWriteClient();
    static void TestWriteClientGroup();
    static void TestWriteHandler();
    static void TestWriteRoundtrip();
    static void TestWriteInvalidMessage1();
    static void TestWriteInvalidMessage2();
    static void TestWriteInvalidMessage3();
    static void TestWriteInvalidMessage4();
    static void TestWriteRoundtripWithClusterObjects();
    static void TestWriteRoundtripWithClusterObjectsVersionMatch();
    static void TestWriteRoundtripWithClusterObjectsVersionMismatch();
#if CONFIG_BUILD_FOR_HOST_UNIT_TEST
    static void TestWriteHandlerReceiveInvalidMessage();
    static void TestWriteHandlerInvalidateFabric();
#endif
private:
    static void AddAttributeDataIB(WriteClient & aWriteClient);
    static void AddAttributeStatus(WriteHandler & aWriteHandler);
    static void GenerateWriteRequest(bool aIsTimedWrite, System::PacketBufferHandle & aPayload);
    static void GenerateWriteResponse(System::PacketBufferHandle & aPayload);

    static TestContext ctx;
};

TestContext TestWriteInteraction::ctx;

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name)                                                                                                \
    {                                                                                                                              \
        test_fixture::test_name();                                                                                                 \
    }                                                                                                                              \
    void test_fixture::test_name()

class TestExchangeDelegate : public Messaging::ExchangeDelegate
{
    CHIP_ERROR OnMessageReceived(Messaging::ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && payload) override
    {
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(Messaging::ExchangeContext * ec) override {}
};

class TestWriteClientCallback : public chip::app::WriteClient::Callback
{
public:
    void ResetCounter() { mOnSuccessCalled = mOnErrorCalled = mOnDoneCalled = 0; }
    void OnResponse(const WriteClient * apWriteClient, const chip::app::ConcreteDataAttributePath & path, StatusIB status) override
    {
        mStatus = status;
        mOnSuccessCalled++;
    }
    void OnError(const WriteClient * apWriteClient, CHIP_ERROR chipError) override
    {
        mOnErrorCalled++;
        mLastErrorReason = app::StatusIB(chipError);
        mError           = chipError;
    }
    void OnDone(WriteClient * apWriteClient) override { mOnDoneCalled++; }

    int mOnSuccessCalled = 0;
    int mOnErrorCalled   = 0;
    int mOnDoneCalled    = 0;
    StatusIB mStatus;
    StatusIB mLastErrorReason;
    CHIP_ERROR mError = CHIP_NO_ERROR;
};

void TestWriteInteraction::AddAttributeDataIB(WriteClient & aWriteClient)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    AttributePathParams attributePathParams;
    bool attributeValue              = true;
    attributePathParams.mEndpointId  = 2;
    attributePathParams.mClusterId   = 3;
    attributePathParams.mAttributeId = 4;

    err = aWriteClient.EncodeAttribute(attributePathParams, attributeValue);
    EXPECT_EQ(err, CHIP_NO_ERROR);
}

void TestWriteInteraction::AddAttributeStatus(WriteHandler & aWriteHandler)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    ConcreteAttributePath attributePath(2, 3, 4);

    err = aWriteHandler.AddStatus(attributePath, Protocols::InteractionModel::Status::Success);
    EXPECT_EQ(err, CHIP_NO_ERROR);
}

void TestWriteInteraction::GenerateWriteRequest(bool aIsTimedWrite, System::PacketBufferHandle & aPayload)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(aPayload));

    WriteRequestMessage::Builder writeRequestBuilder;
    err = writeRequestBuilder.Init(&writer);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    writeRequestBuilder.TimedRequest(aIsTimedWrite);
    EXPECT_EQ(writeRequestBuilder.GetError(), CHIP_NO_ERROR);
    AttributeDataIBs::Builder & attributeDataIBsBuilder = writeRequestBuilder.CreateWriteRequests();
    EXPECT_EQ(writeRequestBuilder.GetError(), CHIP_NO_ERROR);
    AttributeDataIB::Builder & attributeDataIBBuilder = attributeDataIBsBuilder.CreateAttributeDataIBBuilder();
    EXPECT_EQ(attributeDataIBsBuilder.GetError(), CHIP_NO_ERROR);

    attributeDataIBBuilder.DataVersion(0);
    EXPECT_EQ(attributeDataIBBuilder.GetError(), CHIP_NO_ERROR);
    AttributePathIB::Builder & attributePathBuilder = attributeDataIBBuilder.CreatePath();
    EXPECT_EQ(attributePathBuilder.GetError(), CHIP_NO_ERROR);
    err = attributePathBuilder.Node(1)
              .Endpoint(2)
              .Cluster(3)
              .Attribute(4)
              .ListIndex(DataModel::Nullable<ListIndex>())
              .EndOfAttributePathIB();
    EXPECT_EQ(err, CHIP_NO_ERROR);

    // Construct attribute data
    {
        chip::TLV::TLVWriter * pWriter = attributeDataIBBuilder.GetWriter();
        chip::TLV::TLVType dummyType   = chip::TLV::kTLVType_NotSpecified;
        err = pWriter->StartContainer(chip::TLV::ContextTag(AttributeDataIB::Tag::kData), chip::TLV::kTLVType_Structure, dummyType);
        EXPECT_EQ(err, CHIP_NO_ERROR);

        err = pWriter->PutBoolean(chip::TLV::ContextTag(1), true);
        EXPECT_EQ(err, CHIP_NO_ERROR);

        err = pWriter->EndContainer(dummyType);
        EXPECT_EQ(err, CHIP_NO_ERROR);
    }

    attributeDataIBBuilder.EndOfAttributeDataIB();
    EXPECT_EQ(attributeDataIBBuilder.GetError(), CHIP_NO_ERROR);

    attributeDataIBsBuilder.EndOfAttributeDataIBs();
    EXPECT_EQ(attributeDataIBsBuilder.GetError(), CHIP_NO_ERROR);
    writeRequestBuilder.EndOfWriteRequestMessage();
    EXPECT_EQ(writeRequestBuilder.GetError(), CHIP_NO_ERROR);

    err = writer.Finalize(&aPayload);
    EXPECT_EQ(err, CHIP_NO_ERROR);
}

void TestWriteInteraction::GenerateWriteResponse(System::PacketBufferHandle & aPayload)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(aPayload));

    WriteResponseMessage::Builder writeResponseBuilder;
    err = writeResponseBuilder.Init(&writer);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    AttributeStatusIBs::Builder & attributeStatusesBuilder = writeResponseBuilder.CreateWriteResponses();
    EXPECT_EQ(attributeStatusesBuilder.GetError(), CHIP_NO_ERROR);
    AttributeStatusIB::Builder & attributeStatusIBBuilder = attributeStatusesBuilder.CreateAttributeStatus();
    EXPECT_EQ(attributeStatusIBBuilder.GetError(), CHIP_NO_ERROR);

    AttributePathIB::Builder & attributePathBuilder = attributeStatusIBBuilder.CreatePath();
    EXPECT_EQ(attributePathBuilder.GetError(), CHIP_NO_ERROR);
    err = attributePathBuilder.Node(1)
              .Endpoint(2)
              .Cluster(3)
              .Attribute(4)
              .ListIndex(DataModel::Nullable<ListIndex>())
              .EndOfAttributePathIB();
    EXPECT_EQ(err, CHIP_NO_ERROR);

    StatusIB::Builder & statusIBBuilder = attributeStatusIBBuilder.CreateErrorStatus();
    StatusIB statusIB;
    statusIB.mStatus = chip::Protocols::InteractionModel::Status::InvalidSubscription;
    EXPECT_EQ(statusIBBuilder.GetError(), CHIP_NO_ERROR);
    statusIBBuilder.EncodeStatusIB(statusIB);
    err = statusIBBuilder.GetError();
    EXPECT_EQ(err, CHIP_NO_ERROR);

    attributeStatusIBBuilder.EndOfAttributeStatusIB();
    EXPECT_EQ(attributeStatusIBBuilder.GetError(), CHIP_NO_ERROR);

    attributeStatusesBuilder.EndOfAttributeStatuses();
    EXPECT_EQ(attributeStatusesBuilder.GetError(), CHIP_NO_ERROR);
    writeResponseBuilder.EndOfWriteResponseMessage();
    EXPECT_EQ(writeResponseBuilder.GetError(), CHIP_NO_ERROR);

    err = writer.Finalize(&aPayload);
    EXPECT_EQ(err, CHIP_NO_ERROR);
}

STATIC_TEST(TestWriteInteraction, TestWriteClient)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    TestWriteClientCallback callback;
    app::WriteClient writeClient(&ctx.GetExchangeManager(), &callback, /* aTimedWriteTimeoutMs = */ NullOptional);

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    AddAttributeDataIB(writeClient);

    err = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    GenerateWriteResponse(buf);

    err = writeClient.ProcessWriteResponseMessage(std::move(buf));
    EXPECT_EQ(err, CHIP_NO_ERROR);

    writeClient.Close();

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);
}

STATIC_TEST(TestWriteInteraction, TestWriteClientGroup)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    TestWriteClientCallback callback;
    app::WriteClient writeClient(&ctx.GetExchangeManager(), &callback, /* aTimedWriteTimeoutMs = */ NullOptional);

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    AddAttributeDataIB(writeClient);

    SessionHandle groupSession = ctx.GetSessionBobToFriends();
    EXPECT_TRUE(groupSession->IsGroupSession());

    err = writeClient.SendWriteRequest(groupSession);

    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    // The WriteClient should be shutdown once we SendWriteRequest for group.
    EXPECT_EQ(writeClient.mState, WriteClient::State::AwaitingDestruction);
}

STATIC_TEST(TestWriteInteraction, TestWriteHandler)
{
    using namespace Protocols::InteractionModel;

    constexpr bool allBooleans[] = { true, false };
    for (auto messageIsTimed : allBooleans)
    {
        for (auto transactionIsTimed : allBooleans)
        {
            CHIP_ERROR err = CHIP_NO_ERROR;

            app::WriteHandler writeHandler;

            System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
            err                            = writeHandler.Init();

            GenerateWriteRequest(messageIsTimed, buf);

            TestExchangeDelegate delegate;
            Messaging::ExchangeContext * exchange = ctx.NewExchangeToBob(&delegate);

            Status status = writeHandler.OnWriteRequest(exchange, std::move(buf), transactionIsTimed);
            if (messageIsTimed == transactionIsTimed)
            {
                EXPECT_EQ(status, Status::Success);
            }
            else
            {
                EXPECT_EQ(status, Status::UnsupportedAccess);
            }

            ctx.DrainAndServiceIO();

            Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
            EXPECT_EQ(rm->TestGetCountRetransTable(), 0);
        }
    }
}

const EmberAfAttributeMetadata * GetAttributeMetadata(const ConcreteAttributePath & aConcreteClusterPath)
{
    // Note: This test does not make use of the real attribute metadata.
    static EmberAfAttributeMetadata stub = { .defaultValue = EmberAfDefaultOrMinMaxAttributeValue(uint32_t(0)) };
    return &stub;
}

CHIP_ERROR WriteSingleClusterData(const Access::SubjectDescriptor & aSubjectDescriptor, const ConcreteDataAttributePath & aPath,
                                  TLV::TLVReader & aReader, WriteHandler * aWriteHandler)
{
    if (aPath.mDataVersion.HasValue() && aPath.mDataVersion.Value() == kRejectedDataVersion)
    {
        return aWriteHandler->AddStatus(aPath, Protocols::InteractionModel::Status::DataVersionMismatch);
    }

    TLV::TLVWriter writer;
    writer.Init(attributeDataTLV);
    writer.CopyElement(TLV::AnonymousTag(), aReader);
    attributeDataTLVLen = writer.GetLengthWritten();
    return aWriteHandler->AddStatus(aPath, Protocols::InteractionModel::Status::Success);
}

STATIC_TEST(TestWriteInteraction, TestWriteRoundtripWithClusterObjects)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    AttributePathParams attributePathParams;
    attributePathParams.mEndpointId  = 2;
    attributePathParams.mClusterId   = 3;
    attributePathParams.mAttributeId = 4;

    const uint8_t byteSpanData[]     = { 0xde, 0xad, 0xbe, 0xef };
    static const char charSpanData[] = "a simple test string";

    app::Clusters::UnitTesting::Structs::SimpleStruct::Type dataTx;
    dataTx.a = 12;
    dataTx.b = true;
    dataTx.d = chip::ByteSpan(byteSpanData);
    // Spec A.11.2 strings SHALL NOT include a terminating null character to mark the end of a string.
    dataTx.e = chip::Span<const char>(charSpanData, strlen(charSpanData));

    writeClient.EncodeAttribute(attributePathParams, dataTx);
    EXPECT_EQ(err, CHIP_NO_ERROR);

    EXPECT_EQ(callback.mOnSuccessCalled, 0);

    err = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_EQ(callback.mOnSuccessCalled, 1);

    {
        app::Clusters::UnitTesting::Structs::SimpleStruct::Type dataRx;
        TLV::TLVReader reader;
        reader.Init(attributeDataTLV, attributeDataTLVLen);
        reader.Next();
        EXPECT_EQ(CHIP_NO_ERROR, DataModel::Decode(reader, dataRx));
        EXPECT_EQ(dataRx.a, dataTx.a);
        EXPECT_EQ(dataRx.b, dataTx.b);
        EXPECT_TRUE(dataRx.d.data_equal(dataTx.d));
        // Equals to dataRx.e.size() == dataTx.e.size() && memncmp(dataRx.e.data(), dataTx.e.data(), dataTx.e.size()) == 0
        EXPECT_TRUE(dataRx.e.data_equal(dataTx.e));
    }

    EXPECT_TRUE(callback.mOnSuccessCalled == 1 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 1);

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    engine->Shutdown();
}

STATIC_TEST(TestWriteInteraction, TestWriteRoundtripWithClusterObjectsVersionMatch)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    AttributePathParams attributePathParams;
    attributePathParams.mEndpointId  = 2;
    attributePathParams.mClusterId   = 3;
    attributePathParams.mAttributeId = 4;

    DataModel::Nullable<app::Clusters::UnitTesting::Structs::SimpleStruct::Type> dataTx;

    Optional<DataVersion> version(kAcceptedDataVersion);

    writeClient.EncodeAttribute(attributePathParams, dataTx, version);
    EXPECT_EQ(err, CHIP_NO_ERROR);

    EXPECT_EQ(callback.mOnSuccessCalled, 0);

    err = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(callback.mOnSuccessCalled == 1 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 1 &&
                callback.mStatus.mStatus == Protocols::InteractionModel::Status::Success);

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    engine->Shutdown();
}

STATIC_TEST(TestWriteInteraction, TestWriteRoundtripWithClusterObjectsVersionMismatch)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    AttributePathParams attributePathParams;
    attributePathParams.mEndpointId  = 2;
    attributePathParams.mClusterId   = 3;
    attributePathParams.mAttributeId = 4;

    app::Clusters::UnitTesting::Structs::SimpleStruct::Type dataTxValue;
    dataTxValue.a = 12;
    dataTxValue.b = true;
    DataModel::Nullable<app::Clusters::UnitTesting::Structs::SimpleStruct::Type> dataTx;
    dataTx.SetNonNull(dataTxValue);
    Optional<DataVersion> version(kRejectedDataVersion);
    writeClient.EncodeAttribute(attributePathParams, dataTx, version);
    EXPECT_EQ(err, CHIP_NO_ERROR);

    EXPECT_EQ(callback.mOnSuccessCalled, 0);

    err = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(callback.mOnSuccessCalled == 1 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 1 &&
                callback.mStatus.mStatus == Protocols::InteractionModel::Status::DataVersionMismatch);

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    engine->Shutdown();
}

STATIC_TEST(TestWriteInteraction, TestWriteRoundtrip)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    AddAttributeDataIB(writeClient);

    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 0);

    err = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(callback.mOnSuccessCalled == 1 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 1);

    // By now we should have closed all exchanges and sent all pending acks, so
    // there should be no queued-up things in the retransmit table.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    engine->Shutdown();
}

// This test creates a chunked write request, we drop the second write chunk message, then write handler receives unknown
// report message and sends out a status report with invalid action.
#if CONFIG_BUILD_FOR_HOST_UNIT_TEST
STATIC_TEST(TestWriteInteraction, TestWriteHandlerReceiveInvalidMessage)
{
    auto sessionHandle = ctx.GetSessionBobToAlice();

    app::AttributePathParams attributePath(2, 3, 4);

    CHIP_ERROR err                     = CHIP_NO_ERROR;
    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback writeCallback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    // Reserve all except the last 128 bytes, so that we make sure to chunk.
    app::WriteClient writeClient(&ctx.GetExchangeManager(), &writeCallback, Optional<uint16_t>::Missing(),
                                 static_cast<uint16_t>(kMaxSecureSduLengthBytes - 128) /* reserved buffer size */);

    ByteSpan list[5];

    err = writeClient.EncodeAttribute(attributePath, app::DataModel::List<ByteSpan>(list, 5));
    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 2;
    err                                                 = writeClient.SendWriteRequest(sessionHandle);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_EQ(InteractionModelEngine::GetInstance()->GetNumActiveWriteHandlers(), 1u);
    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 3u);
    EXPECT_EQ(ctx.GetLoopback().mDroppedMessageCount, 1u);

    System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    ASSERT_FALSE(msgBuf.IsNull());
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(msgBuf));

    ReportDataMessage::Builder response;
    response.Init(&writer);
    EXPECT_EQ(writer.Finalize(&msgBuf), CHIP_NO_ERROR);

    PayloadHeader payloadHeader;
    payloadHeader.SetExchangeID(0);
    payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::ReportData);

    auto * writeHandler = InteractionModelEngine::GetInstance()->ActiveWriteHandlerAt(0);
    rm->ClearRetransTable(writeClient.mExchangeCtx.Get());
    rm->ClearRetransTable(writeHandler->mExchangeCtx.Get());
    ctx.GetLoopback().mSentMessageCount  = 0;
    ctx.GetLoopback().mNumMessagesToDrop = 0;
    writeHandler->OnMessageReceived(writeHandler->mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    ctx.DrainAndServiceIO();

    EXPECT_EQ(writeCallback.mLastErrorReason.mStatus, Protocols::InteractionModel::Status::InvalidAction);
    EXPECT_EQ(InteractionModelEngine::GetInstance()->GetNumActiveWriteHandlers(), 0u);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// This test is to create Chunked write requests, we drop the message since the 3rd message, then remove fabrics for client and
// handler, the corresponding client and handler would be released as well.
STATIC_TEST(TestWriteInteraction, TestWriteHandlerInvalidateFabric)
{
    auto sessionHandle = ctx.GetSessionBobToAlice();

    app::AttributePathParams attributePath(2, 3, 4);

    CHIP_ERROR err                     = CHIP_NO_ERROR;
    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback writeCallback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    // Reserve all except the last 128 bytes, so that we make sure to chunk.
    app::WriteClient writeClient(&ctx.GetExchangeManager(), &writeCallback, Optional<uint16_t>::Missing(),
                                 static_cast<uint16_t>(kMaxSecureSduLengthBytes - 128) /* reserved buffer size */);

    ByteSpan list[5];

    err = writeClient.EncodeAttribute(attributePath, app::DataModel::List<ByteSpan>(list, 5));
    EXPECT_EQ(err, CHIP_NO_ERROR);

    ctx.GetLoopback().mDroppedMessageCount              = 0;
    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 2;
    err                                                 = writeClient.SendWriteRequest(sessionHandle);
    EXPECT_EQ(err, CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_EQ(InteractionModelEngine::GetInstance()->GetNumActiveWriteHandlers(), 1u);
    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 3u);
    EXPECT_EQ(ctx.GetLoopback().mDroppedMessageCount, 1u);

    ctx.GetFabricTable().Delete(ctx.GetAliceFabricIndex());
    EXPECT_EQ(InteractionModelEngine::GetInstance()->GetNumActiveWriteHandlers(), 0u);
    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateAliceFabric();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

#endif

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
#define PretendWeGotReplyFromServer(aClientExchange)                                                                               \
    {                                                                                                                              \
        Messaging::ReliableMessageMgr * localRm    = ctx.GetExchangeManager().GetReliableMessageMgr();                                 \
        Messaging::ExchangeContext * localExchange = aClientExchange;                                                              \
        EXPECT_EQ(localRm->TestGetCountRetransTable(), 2);                                                          \
                                                                                                                                   \
        localRm->ClearRetransTable(localExchange);                                                                                 \
        EXPECT_EQ(localRm->TestGetCountRetransTable(), 1);                                                          \
                                                                                                                                   \
        localRm->EnumerateRetransTable([localExchange](auto * entry) {                                                             \
            localExchange->SetPendingPeerAckMessageCounter(entry->retainedBuf.GetMessageCounter());                                \
            return Loop::Break;                                                                                                    \
        });                                                                                                                        \
    }

// Write Client sends a write request, receives an unexpected message type, sends a status response to that.
STATIC_TEST(TestWriteInteraction, TestWriteInvalidMessage1)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    AddAttributeDataIB(writeClient);

    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 0);

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err                                                 = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);
    EXPECT_EQ(ctx.GetLoopback().mDroppedMessageCount, 1u);

    System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    ASSERT_FALSE(msgBuf.IsNull());
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(msgBuf));
    ReportDataMessage::Builder response;
    response.Init(&writer);
    EXPECT_EQ(writer.Finalize(&msgBuf), CHIP_NO_ERROR);
    PayloadHeader payloadHeader;
    payloadHeader.SetExchangeID(0);
    payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::ReportData);

    // Since we are dropping packets, things are not getting acked.  Set up
    // our MRP state to look like what it would have looked like if the
    // packet had not gotten dropped.
    PretendWeGotReplyFromServer(writeClient.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err = writeClient.OnMessageReceived(writeClient.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_EQ(err, CHIP_ERROR_INVALID_MESSAGE_TYPE);
    ctx.DrainAndServiceIO();
    EXPECT_EQ(callback.mError, CHIP_ERROR_INVALID_MESSAGE_TYPE);
    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 1 && callback.mOnDoneCalled == 1);

    // TODO: Check that the server gets the right status.
    // Client sents status report with invalid action, server's exchange has been closed, so all it sends is an MRP Ack
    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);

    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Write Client sends a write request, receives a malformed write response message, sends a Status Report.
STATIC_TEST(TestWriteInteraction, TestWriteInvalidMessage2)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    AddAttributeDataIB(writeClient);

    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 0);

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err                                                 = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);
    EXPECT_EQ(ctx.GetLoopback().mDroppedMessageCount, 1u);

    System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    ASSERT_FALSE(msgBuf.IsNull());
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(msgBuf));
    WriteResponseMessage::Builder response;
    response.Init(&writer);
    EXPECT_EQ(writer.Finalize(&msgBuf), CHIP_NO_ERROR);
    PayloadHeader payloadHeader;
    payloadHeader.SetExchangeID(0);
    payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::WriteResponse);

    // Since we are dropping packets, things are not getting acked.  Set up
    // our MRP state to look like what it would have looked like if the
    // packet had not gotten dropped.
    PretendWeGotReplyFromServer(writeClient.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err = writeClient.OnMessageReceived(writeClient.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_EQ(err, CHIP_ERROR_END_OF_TLV);
    ctx.DrainAndServiceIO();
    EXPECT_EQ(callback.mError, CHIP_ERROR_END_OF_TLV);
    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 1 && callback.mOnDoneCalled == 1);

    // Client sents status report with invalid action, server's exchange has been closed, so all it sends is an MRP Ack
    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);

    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Write Client sends a write request, receives a malformed status response message.
STATIC_TEST(TestWriteInteraction, TestWriteInvalidMessage3)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    AddAttributeDataIB(writeClient);

    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 0);

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err                                                 = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);
    EXPECT_EQ(ctx.GetLoopback().mDroppedMessageCount, 1u);

    System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    ASSERT_FALSE(msgBuf.IsNull());
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(msgBuf));
    StatusResponseMessage::Builder response;
    response.Init(&writer);
    EXPECT_EQ(writer.Finalize(&msgBuf), CHIP_NO_ERROR);
    PayloadHeader payloadHeader;
    payloadHeader.SetExchangeID(0);
    payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::StatusResponse);

    // Since we are dropping packets, things are not getting acked.  Set up
    // our MRP state to look like what it would have looked like if the
    // packet had not gotten dropped.
    PretendWeGotReplyFromServer(writeClient.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err = writeClient.OnMessageReceived(writeClient.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_EQ(err, CHIP_ERROR_END_OF_TLV);
    ctx.DrainAndServiceIO();
    EXPECT_EQ(callback.mError, CHIP_ERROR_END_OF_TLV);
    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 1 && callback.mOnDoneCalled == 1);

    // TODO: Check that the server gets the right status
    // Client sents status report with invalid action, server's exchange has been closed, so all it sends is an MRP ack.
    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);

    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Write Client sends a write request, receives a busy status response message.
STATIC_TEST(TestWriteInteraction, TestWriteInvalidMessage4)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    Messaging::ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    // Shouldn't have anything in the retransmit table when starting the test.
    EXPECT_EQ(rm->TestGetCountRetransTable(), 0);

    TestWriteClientCallback callback;
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    err           = engine->Init(&ctx.GetExchangeManager(), &ctx.GetFabricTable(), app::reporting::GetDefaultReportScheduler());
    EXPECT_EQ(err, CHIP_NO_ERROR);

    app::WriteClient writeClient(engine->GetExchangeManager(), &callback, Optional<uint16_t>::Missing());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    AddAttributeDataIB(writeClient);

    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 0 && callback.mOnDoneCalled == 0);

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err                                                 = writeClient.SendWriteRequest(ctx.GetSessionBobToAlice());
    EXPECT_EQ(err, CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);
    EXPECT_EQ(ctx.GetLoopback().mDroppedMessageCount, 1u);

    System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    ASSERT_FALSE(msgBuf.IsNull());
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(msgBuf));
    StatusResponseMessage::Builder response;
    response.Init(&writer);
    response.Status(Protocols::InteractionModel::Status::Busy);
    EXPECT_EQ(writer.Finalize(&msgBuf), CHIP_NO_ERROR);
    PayloadHeader payloadHeader;
    payloadHeader.SetExchangeID(0);
    payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::StatusResponse);

    // Since we are dropping packets, things are not getting acked.  Set up
    // our MRP state to look like what it would have looked like if the
    // packet had not gotten dropped.
    PretendWeGotReplyFromServer(writeClient.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;
    err = writeClient.OnMessageReceived(writeClient.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_EQ(err, CHIP_IM_GLOBAL_STATUS(Busy));
    ctx.DrainAndServiceIO();
    EXPECT_EQ(callback.mError, CHIP_IM_GLOBAL_STATUS(Busy));
    EXPECT_TRUE(callback.mOnSuccessCalled == 0 && callback.mOnErrorCalled == 1 && callback.mOnDoneCalled == 1);

    // TODO: Check that the server gets the right status..
    // Client sents status report with invalid action, server's exchange has been closed, so it just sends an MRP ack.
    EXPECT_EQ(ctx.GetLoopback().mSentMessageCount, 2u);

    engine->Shutdown();
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

} // namespace app
} // namespace chip
