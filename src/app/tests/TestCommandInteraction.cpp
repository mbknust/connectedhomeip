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

#include <cinttypes>

#include <app/AppConfig.h>
#include <app/InteractionModelEngine.h>
#include <app/data-model/Encode.h>
#include <app/tests/AppTestContext.h>
#include <lib/core/CHIPCore.h>
#include <lib/core/ErrorStr.h>
#include <lib/core/Optional.h>
#include <lib/core/TLV.h>
#include <lib/core/TLVDebug.h>
#include <lib/core/TLVUtilities.h>
#include <lib/support/UnitTestContext.h>

#include <lib/support/logging/CHIPLogging.h>
#include <messaging/ExchangeContext.h>
#include <messaging/ExchangeMgr.h>
#include <messaging/Flags.h>
#include <platform/CHIPDeviceLayer.h>
#include <protocols/interaction_model/Constants.h>
#include <system/SystemPacketBuffer.h>
#include <system/TLVPacketBufferBackingStore.h>

#include <gtest/gtest.h>

using TestContext = chip::Test::AppContext;
using namespace chip::Protocols;

namespace {

void CheckForInvalidAction(chip::Test::MessageCapturer & messageLog)
{
    EXPECT_TRUE(messageLog.MessageCount() == 1);
    EXPECT_TRUE(messageLog.IsMessageType(0, chip::Protocols::InteractionModel::MsgType::StatusResponse));
    CHIP_ERROR status;
    EXPECT_TRUE(chip::app::StatusResponse::ProcessStatusResponse(std::move(messageLog.MessagePayload(0)), status) == CHIP_NO_ERROR);
    EXPECT_TRUE(status == CHIP_IM_GLOBAL_STATUS(InvalidAction));
}

} // anonymous namespace

namespace chip {

namespace {
bool isCommandDispatched = false;

bool sendResponse = true;
bool asyncCommand = false;

constexpr EndpointId kTestEndpointId                      = 1;
constexpr ClusterId kTestClusterId                        = 3;
constexpr CommandId kTestCommandIdWithData                = 4;
constexpr CommandId kTestCommandIdNoData                  = 5;
constexpr CommandId kTestCommandIdCommandSpecificResponse = 6;
constexpr CommandId kTestNonExistCommandId                = 0;
} // namespace

namespace app {

CommandHandler::Handle asyncCommandHandle;

InteractionModel::Status ServerClusterCommandExists(const ConcreteCommandPath & aCommandPath)
{
    // Mock cluster catalog, only support commands on one cluster on one endpoint.
    using InteractionModel::Status;

    if (aCommandPath.mEndpointId != kTestEndpointId)
    {
        return Status::UnsupportedEndpoint;
    }

    if (aCommandPath.mClusterId != kTestClusterId)
    {
        return Status::UnsupportedCluster;
    }

    if (aCommandPath.mCommandId == kTestNonExistCommandId)
    {
        return Status::UnsupportedCommand;
    }

    return Status::Success;
}

void DispatchSingleClusterCommand(const ConcreteCommandPath & aCommandPath, chip::TLV::TLVReader & aReader,
                                  CommandHandler * apCommandObj)
{
    ChipLogDetail(Controller, "Received Cluster Command: Endpoint=%x Cluster=" ChipLogFormatMEI " Command=" ChipLogFormatMEI,
                  aCommandPath.mEndpointId, ChipLogValueMEI(aCommandPath.mClusterId), ChipLogValueMEI(aCommandPath.mCommandId));

    // Duplicate what our normal command-field-decode code does, in terms of
    // checking for a struct and then entering it before getting the fields.
    if (aReader.GetType() != TLV::kTLVType_Structure)
    {
        apCommandObj->AddStatus(aCommandPath, Protocols::InteractionModel::Status::InvalidAction);
        return;
    }

    TLV::TLVType outerContainerType;
    CHIP_ERROR err = aReader.EnterContainer(outerContainerType);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = aReader.Next();
    if (aCommandPath.mCommandId == kTestCommandIdNoData)
    {
        EXPECT_TRUE(err == CHIP_ERROR_END_OF_TLV);
    }
    else
    {
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        EXPECT_TRUE(aReader.GetTag() == TLV::ContextTag(1));
        bool val;
        err = aReader.Get(val);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        EXPECT_TRUE(val);
    }

    err = aReader.ExitContainer(outerContainerType);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    if (asyncCommand)
    {
        asyncCommandHandle = apCommandObj;
        asyncCommand       = false;
    }

    if (sendResponse)
    {
        if (aCommandPath.mCommandId == kTestCommandIdNoData || aCommandPath.mCommandId == kTestCommandIdWithData)
        {
            apCommandObj->AddStatus(aCommandPath, Protocols::InteractionModel::Status::Success);
        }
        else
        {
            apCommandObj->PrepareCommand(aCommandPath);
            chip::TLV::TLVWriter * writer = apCommandObj->GetCommandDataIBTLVWriter();
            writer->PutBoolean(chip::TLV::ContextTag(1), true);
            apCommandObj->FinishCommand();
        }
    }

    chip::isCommandDispatched = true;
}

class MockCommandSenderCallback : public CommandSender::Callback
{
public:
    void OnResponse(chip::app::CommandSender * apCommandSender, const chip::app::ConcreteCommandPath & aPath,
                    const chip::app::StatusIB & aStatus, chip::TLV::TLVReader * aData) override
    {
        IgnoreUnusedVariable(apCommandSender);
        IgnoreUnusedVariable(aData);
        ChipLogDetail(Controller, "Received Cluster Command: Cluster=%" PRIx32 " Command=%" PRIx32 " Endpoint=%x", aPath.mClusterId,
                      aPath.mCommandId, aPath.mEndpointId);
        onResponseCalledTimes++;
    }
    void OnError(const chip::app::CommandSender * apCommandSender, CHIP_ERROR aError) override
    {
        ChipLogError(Controller, "OnError happens with %" CHIP_ERROR_FORMAT, aError.Format());
        mError = aError;
        onErrorCalledTimes++;
        mError = aError;
    }
    void OnDone(chip::app::CommandSender * apCommandSender) override { onFinalCalledTimes++; }

    void ResetCounter()
    {
        onResponseCalledTimes = 0;
        onErrorCalledTimes    = 0;
        onFinalCalledTimes    = 0;
    }

    int onResponseCalledTimes = 0;
    int onErrorCalledTimes    = 0;
    int onFinalCalledTimes    = 0;
    CHIP_ERROR mError         = CHIP_NO_ERROR;
} mockCommandSenderDelegate;

class MockCommandHandlerCallback : public CommandHandler::Callback
{
public:
    void OnDone(CommandHandler & apCommandHandler) final { onFinalCalledTimes++; }
    void DispatchCommand(CommandHandler & apCommandObj, const ConcreteCommandPath & aCommandPath, TLV::TLVReader & apPayload) final
    {
        DispatchSingleClusterCommand(aCommandPath, apPayload, &apCommandObj);
    }
    InteractionModel::Status CommandExists(const ConcreteCommandPath & aCommandPath)
    {
        return ServerClusterCommandExists(aCommandPath);
    }

    int onFinalCalledTimes = 0;
} mockCommandHandlerDelegate;

class TestCommandInteraction : public ::testing::Test
{
public:
    static TestContext ctx;
    static void SetUpTestSuite() { TestContext::Initialize(&ctx); }
    static void TearDownTestSuite() { TestContext::Finalize(&ctx); }

    static void TestCommandSenderWithWrongState();
    static void TestCommandHandlerWithWrongState();
    static void TestCommandSenderWithSendCommand();
    static void TestCommandHandlerWithSendEmptyCommand();
    static void TestCommandSenderWithProcessReceivedMsg();
    static void TestCommandHandlerWithProcessReceivedNotExistCommand();
    static void TestCommandHandlerWithSendSimpleCommandData();
    static void TestCommandHandlerCommandDataEncoding();
    static void TestCommandHandlerCommandEncodeFailure();
    static void TestCommandInvalidMessage1();
    static void TestCommandInvalidMessage2();
    static void TestCommandInvalidMessage3();
    static void TestCommandInvalidMessage4();
    static void TestCommandHandlerInvalidMessageSync();
    static void TestCommandHandlerInvalidMessageAsync();
    static void TestCommandHandlerCommandEncodeExternalFailure();
    static void TestCommandHandlerWithSendSimpleStatusCode();
    static void TestCommandHandlerWithSendEmptyResponse();

    static void TestCommandHandlerWithProcessReceivedEmptyDataMsg();
    static void TestCommandHandlerRejectMultipleCommands();

#if CONFIG_BUILD_FOR_HOST_UNIT_TEST
    static void TestCommandHandlerReleaseWithExchangeClosed();
#endif

    static void TestCommandSenderCommandSuccessResponseFlow();
    static void TestCommandSenderCommandAsyncSuccessResponseFlow();
    static void TestCommandSenderCommandFailureResponseFlow();
    static void TestCommandSenderCommandSpecificResponseFlow();

    static void TestCommandSenderAbruptDestruction();

    static size_t GetNumActiveHandlerObjects()
    {
        return chip::app::InteractionModelEngine::GetInstance()->mCommandHandlerObjs.Allocated();
    }

private:
    // Generate an invoke request.  If aCommandId is kTestCommandIdWithData, a
    // payload will be included.  Otherwise no payload will be included.
    static void GenerateInvokeRequest(System::PacketBufferHandle & aPayload, bool aIsTimedRequest, CommandId aCommandId,
                                      ClusterId aClusterId = kTestClusterId, EndpointId aEndpointId = kTestEndpointId);
    // Generate an invoke response.  If aCommandId is kTestCommandIdWithData, a
    // payload will be included.  Otherwise no payload will be included.
    static void GenerateInvokeResponse(System::PacketBufferHandle & aPayload, CommandId aCommandId,
                                       ClusterId aClusterId = kTestClusterId, EndpointId aEndpointId = kTestEndpointId);
    static void AddInvokeRequestData(CommandSender * apCommandSender, CommandId aCommandId = kTestCommandIdWithData);
    static void AddInvalidInvokeRequestData(CommandSender * apCommandSender, CommandId aCommandId = kTestCommandIdWithData);
    static void AddInvokeResponseData(CommandHandler * apCommandHandler, bool aNeedStatusCode,
                                      CommandId aCommandId = kTestCommandIdWithData);
    static void ValidateCommandHandlerWithSendCommand(bool aNeedStatusCode);
};

TestContext TestCommandInteraction::ctx;

class TestExchangeDelegate : public Messaging::ExchangeDelegate
{
    CHIP_ERROR OnMessageReceived(Messaging::ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && payload) override
    {
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(Messaging::ExchangeContext * ec) override {}
};

CommandPathParams MakeTestCommandPath(CommandId aCommandId = kTestCommandIdWithData)
{
    return CommandPathParams(kTestEndpointId, 0, kTestClusterId, aCommandId, (chip::app::CommandPathFlags::kEndpointIdValid));
}

void TestCommandInteraction::GenerateInvokeRequest(System::PacketBufferHandle & aPayload, bool aIsTimedRequest,
                                                   CommandId aCommandId, ClusterId aClusterId, EndpointId aEndpointId)

{
    CHIP_ERROR err = CHIP_NO_ERROR;
    InvokeRequestMessage::Builder invokeRequestMessageBuilder;
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(aPayload));

    err = invokeRequestMessageBuilder.Init(&writer);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    invokeRequestMessageBuilder.SuppressResponse(true).TimedRequest(aIsTimedRequest);
    InvokeRequests::Builder & invokeRequests = invokeRequestMessageBuilder.CreateInvokeRequests();
    EXPECT_TRUE(invokeRequestMessageBuilder.GetError() == CHIP_NO_ERROR);

    CommandDataIB::Builder & commandDataIBBuilder = invokeRequests.CreateCommandData();
    EXPECT_TRUE(invokeRequests.GetError() == CHIP_NO_ERROR);

    CommandPathIB::Builder & commandPathBuilder = commandDataIBBuilder.CreatePath();
    EXPECT_TRUE(commandDataIBBuilder.GetError() == CHIP_NO_ERROR);

    commandPathBuilder.EndpointId(aEndpointId).ClusterId(aClusterId).CommandId(aCommandId).EndOfCommandPathIB();
    EXPECT_TRUE(commandPathBuilder.GetError() == CHIP_NO_ERROR);

    if (aCommandId == kTestCommandIdWithData)
    {
        chip::TLV::TLVWriter * pWriter = commandDataIBBuilder.GetWriter();
        chip::TLV::TLVType dummyType   = chip::TLV::kTLVType_NotSpecified;
        err = pWriter->StartContainer(chip::TLV::ContextTag(chip::to_underlying(CommandDataIB::Tag::kFields)),
                                      chip::TLV::kTLVType_Structure, dummyType);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = pWriter->PutBoolean(chip::TLV::ContextTag(1), true);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = pWriter->EndContainer(dummyType);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }

    commandDataIBBuilder.EndOfCommandDataIB();
    EXPECT_TRUE(commandDataIBBuilder.GetError() == CHIP_NO_ERROR);

    invokeRequests.EndOfInvokeRequests();
    EXPECT_TRUE(invokeRequests.GetError() == CHIP_NO_ERROR);

    invokeRequestMessageBuilder.EndOfInvokeRequestMessage();
    EXPECT_TRUE(invokeRequestMessageBuilder.GetError() == CHIP_NO_ERROR);

    err = writer.Finalize(&aPayload);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

void TestCommandInteraction::GenerateInvokeResponse(System::PacketBufferHandle & aPayload, CommandId aCommandId,
                                                    ClusterId aClusterId, EndpointId aEndpointId)

{
    CHIP_ERROR err = CHIP_NO_ERROR;
    InvokeResponseMessage::Builder invokeResponseMessageBuilder;
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(aPayload));

    err = invokeResponseMessageBuilder.Init(&writer);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    invokeResponseMessageBuilder.SuppressResponse(true);
    InvokeResponseIBs::Builder & invokeResponses = invokeResponseMessageBuilder.CreateInvokeResponses();
    EXPECT_TRUE(invokeResponseMessageBuilder.GetError() == CHIP_NO_ERROR);

    InvokeResponseIB::Builder & invokeResponseIBBuilder = invokeResponses.CreateInvokeResponse();
    EXPECT_TRUE(invokeResponses.GetError() == CHIP_NO_ERROR);

    CommandDataIB::Builder & commandDataIBBuilder = invokeResponseIBBuilder.CreateCommand();
    EXPECT_TRUE(commandDataIBBuilder.GetError() == CHIP_NO_ERROR);

    CommandPathIB::Builder & commandPathBuilder = commandDataIBBuilder.CreatePath();
    EXPECT_TRUE(commandDataIBBuilder.GetError() == CHIP_NO_ERROR);

    commandPathBuilder.EndpointId(aEndpointId).ClusterId(aClusterId).CommandId(aCommandId).EndOfCommandPathIB();
    EXPECT_TRUE(commandPathBuilder.GetError() == CHIP_NO_ERROR);

    if (aCommandId == kTestCommandIdWithData)
    {
        chip::TLV::TLVWriter * pWriter = commandDataIBBuilder.GetWriter();
        chip::TLV::TLVType dummyType   = chip::TLV::kTLVType_NotSpecified;
        err = pWriter->StartContainer(chip::TLV::ContextTag(chip::to_underlying(CommandDataIB::Tag::kFields)),
                                      chip::TLV::kTLVType_Structure, dummyType);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = pWriter->PutBoolean(chip::TLV::ContextTag(1), true);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = pWriter->EndContainer(dummyType);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }

    commandDataIBBuilder.EndOfCommandDataIB();
    EXPECT_TRUE(commandDataIBBuilder.GetError() == CHIP_NO_ERROR);

    invokeResponseIBBuilder.EndOfInvokeResponseIB();
    EXPECT_TRUE(invokeResponseIBBuilder.GetError() == CHIP_NO_ERROR);

    invokeResponses.EndOfInvokeResponses();
    EXPECT_TRUE(invokeResponses.GetError() == CHIP_NO_ERROR);

    invokeResponseMessageBuilder.EndOfInvokeResponseMessage();
    EXPECT_TRUE(invokeResponseMessageBuilder.GetError() == CHIP_NO_ERROR);

    err = writer.Finalize(&aPayload);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

void TestCommandInteraction::AddInvokeRequestData(CommandSender * apCommandSender, CommandId aCommandId)
{
    CHIP_ERROR err         = CHIP_NO_ERROR;
    auto commandPathParams = MakeTestCommandPath(aCommandId);

    err = apCommandSender->PrepareCommand(commandPathParams);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::TLV::TLVWriter * writer = apCommandSender->GetCommandDataIBTLVWriter();

    err = writer->PutBoolean(chip::TLV::ContextTag(1), true);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = apCommandSender->FinishCommand();
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

void TestCommandInteraction::AddInvalidInvokeRequestData(CommandSender * apCommandSender, CommandId aCommandId)
{
    CHIP_ERROR err         = CHIP_NO_ERROR;
    auto commandPathParams = MakeTestCommandPath(aCommandId);

    err = apCommandSender->PrepareCommand(commandPathParams);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::TLV::TLVWriter * writer = apCommandSender->GetCommandDataIBTLVWriter();

    err = writer->PutBoolean(chip::TLV::ContextTag(1), true);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    apCommandSender->MoveToState(CommandSender::State::AddedCommand);
}

void TestCommandInteraction::AddInvokeResponseData(CommandHandler * apCommandHandler, bool aNeedStatusCode, CommandId aCommandId)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (aNeedStatusCode)
    {
        chip::app::ConcreteCommandPath commandPath(1, // Endpoint
                                                   3, // ClusterId
                                                   4  // CommandId
        );
        apCommandHandler->AddStatus(commandPath, Protocols::InteractionModel::Status::Success);
    }
    else
    {
        ConcreteCommandPath path = { kTestEndpointId, kTestClusterId, aCommandId };
        err                      = apCommandHandler->PrepareCommand(path);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        chip::TLV::TLVWriter * writer = apCommandHandler->GetCommandDataIBTLVWriter();

        err = writer->PutBoolean(chip::TLV::ContextTag(1), true);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        err = apCommandHandler->FinishCommand();
        EXPECT_TRUE(err == CHIP_NO_ERROR);
    }
}

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
        Messaging::ReliableMessageMgr * localRm    = ctx.GetExchangeManager().GetReliableMessageMgr();                             \
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

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name) { test_fixture::test_name(); }                                                                 \
    void test_fixture::test_name()

// Command Sender sends invoke request, command handler drops invoke response, then test injects status response message with
// busy to client, client sends out a status response with invalid action.
STATIC_TEST(TestCommandInteraction, TestCommandInvalidMessage1)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender);
    asyncCommand = false;

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    err                                                 = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 0 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);

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

    // Since we are dropping packets, things are not getting acked.  Set up our
    // MRP state to look like what it would have looked like if the packet had
    // not gotten dropped.
    PretendWeGotReplyFromServer(commandSender.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;

    err = commandSender.OnMessageReceived(commandSender.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_TRUE(err == CHIP_IM_GLOBAL_STATUS(Busy));
    EXPECT_TRUE(mockCommandSenderDelegate.mError == CHIP_IM_GLOBAL_STATUS(Busy));
    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);

    ctx.DrainAndServiceIO();

    // Client sent status report with invalid action, server's exchange has been closed, so all it sent is an MRP Ack
    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    CheckForInvalidAction(messageLog);
    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Command Sender sends invoke request, command handler drops invoke response, then test injects unknown message to client,
// client sends out status response with invalid action.
STATIC_TEST(TestCommandInteraction, TestCommandInvalidMessage2)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender);
    asyncCommand = false;

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    err                                                 = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 0 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);

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
    chip::Test::MessageCapturer messageLog(ctx);
    messageLog.mCaptureStandaloneAcks = false;

    // Since we are dropping packets, things are not getting acked.  Set up our
    // MRP state to look like what it would have looked like if the packet had
    // not gotten dropped.
    PretendWeGotReplyFromServer(commandSender.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;

    err = commandSender.OnMessageReceived(commandSender.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_MESSAGE_TYPE);
    EXPECT_TRUE(mockCommandSenderDelegate.mError == CHIP_ERROR_INVALID_MESSAGE_TYPE);
    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);

    ctx.DrainAndServiceIO();

    // Client sent status report with invalid action, server's exchange has been closed, so all it sent is an MRP Ack
    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    CheckForInvalidAction(messageLog);
    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Command Sender sends invoke request, command handler drops invoke response, then test injects malformed invoke response
// message to client, client sends out status response with invalid action.
STATIC_TEST(TestCommandInteraction, TestCommandInvalidMessage3)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender);
    asyncCommand = false;

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    err                                                 = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 0 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);

    System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    EXPECT_TRUE(!msgBuf.IsNull());
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(msgBuf));
    InvokeResponseMessage::Builder response;
    response.Init(&writer);
    EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);

    PayloadHeader payloadHeader;
    payloadHeader.SetExchangeID(0);
    payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::InvokeCommandResponse);
    chip::Test::MessageCapturer messageLog(ctx);
    messageLog.mCaptureStandaloneAcks = false;

    // Since we are dropping packets, things are not getting acked.  Set up our
    // MRP state to look like what it would have looked like if the packet had
    // not gotten dropped.
    PretendWeGotReplyFromServer(commandSender.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;

    err = commandSender.OnMessageReceived(commandSender.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_TRUE(err == CHIP_ERROR_END_OF_TLV);
    EXPECT_TRUE(mockCommandSenderDelegate.mError == CHIP_ERROR_END_OF_TLV);
    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);

    ctx.DrainAndServiceIO();

    // Client sent status report with invalid action, server's exchange has been closed, so all it sent is an MRP Ack
    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    CheckForInvalidAction(messageLog);
    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

// Command Sender sends invoke request, command handler drops invoke response, then test injects malformed status response to
// client, client responds to the status response with invalid action.
STATIC_TEST(TestCommandInteraction, TestCommandInvalidMessage4)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender);
    asyncCommand = false;

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 1;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 1;
    err                                                 = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    EXPECT_TRUE(ctx.GetLoopback().mDroppedMessageCount == 1);

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 0 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);

    System::PacketBufferHandle msgBuf = System::PacketBufferHandle::New(kMaxSecureSduLengthBytes);
    EXPECT_TRUE(!msgBuf.IsNull());
    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(msgBuf));
    StatusResponseMessage::Builder response;
    response.Init(&writer);
    EXPECT_TRUE(writer.Finalize(&msgBuf) == CHIP_NO_ERROR);

    PayloadHeader payloadHeader;
    payloadHeader.SetExchangeID(0);
    payloadHeader.SetMessageType(chip::Protocols::InteractionModel::MsgType::StatusResponse);
    chip::Test::MessageCapturer messageLog(ctx);
    messageLog.mCaptureStandaloneAcks = false;

    // Since we are dropping packets, things are not getting acked.  Set up our
    // MRP state to look like what it would have looked like if the packet had
    // not gotten dropped.
    PretendWeGotReplyFromServer(commandSender.mExchangeCtx.Get());

    ctx.GetLoopback().mSentMessageCount                 = 0;
    ctx.GetLoopback().mNumMessagesToDrop                = 0;
    ctx.GetLoopback().mNumMessagesToAllowBeforeDropping = 0;
    ctx.GetLoopback().mDroppedMessageCount              = 0;

    err = commandSender.OnMessageReceived(commandSender.mExchangeCtx.Get(), payloadHeader, std::move(msgBuf));
    EXPECT_TRUE(err == CHIP_ERROR_END_OF_TLV);
    EXPECT_TRUE(mockCommandSenderDelegate.mError == CHIP_ERROR_END_OF_TLV);
    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);

    ctx.DrainAndServiceIO();

    // Client sent status report with invalid action, server's exchange has been closed, so all it sent is an MRP Ack
    EXPECT_TRUE(ctx.GetLoopback().mSentMessageCount == 2);
    CheckForInvalidAction(messageLog);
    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    ctx.ExpireSessionAliceToBob();
    ctx.ExpireSessionBobToAlice();
    ctx.CreateSessionAliceToBob();
    ctx.CreateSessionBobToAlice();
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderWithWrongState)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_ERROR_INCORRECT_STATE);
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerWithWrongState)
{
    CHIP_ERROR err           = CHIP_NO_ERROR;
    ConcreteCommandPath path = { kTestEndpointId, kTestClusterId, kTestCommandIdNoData };

    app::CommandHandler commandHandler(&mockCommandHandlerDelegate);

    err = commandHandler.PrepareCommand(path);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    TestExchangeDelegate delegate;

    auto exchange = ctx.NewExchangeToAlice(&delegate, false);
    commandHandler.mExchangeCtx.Grab(exchange);

    err = commandHandler.SendCommandResponse();

    EXPECT_TRUE(err == CHIP_ERROR_INCORRECT_STATE);

    //
    // Ordinarily, the ExchangeContext will close itself upon sending the final message / error'ing out on a responder exchange
    // when unwinding back from an OnMessageReceived callback. Since that isn't the case in this artificial setup here
    // (where we created a responder exchange that's not responding to anything), we need
    // to explicitly close it out. This is not expected in normal application logic.
    //
    exchange->Close();
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderWithSendCommand)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    AddInvokeRequestData(&commandSender);
    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    GenerateInvokeResponse(buf, kTestCommandIdWithData);
    err = commandSender.ProcessInvokeResponse(std::move(buf));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerWithSendEmptyCommand)
{
    CHIP_ERROR err           = CHIP_NO_ERROR;
    ConcreteCommandPath path = { kTestEndpointId, kTestClusterId, kTestCommandIdNoData };

    app::CommandHandler commandHandler(&mockCommandHandlerDelegate);
    System::PacketBufferHandle commandDatabuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    TestExchangeDelegate delegate;
    auto exchange = ctx.NewExchangeToAlice(&delegate, false);
    commandHandler.mExchangeCtx.Grab(exchange);

    err = commandHandler.PrepareCommand(path);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    err = commandHandler.FinishCommand();
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    err = commandHandler.SendCommandResponse();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    commandHandler.Close();
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderWithProcessReceivedMsg)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    System::PacketBufferHandle buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

    GenerateInvokeResponse(buf, kTestCommandIdWithData);
    err = commandSender.ProcessInvokeResponse(std::move(buf));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

void TestCommandInteraction::ValidateCommandHandlerWithSendCommand(bool aNeedStatusCode)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    app::CommandHandler commandHandler(&mockCommandHandlerDelegate);
    System::PacketBufferHandle commandPacket;

    TestExchangeDelegate delegate;
    auto exchange = ctx.NewExchangeToAlice(&delegate, false);
    commandHandler.mExchangeCtx.Grab(exchange);

    AddInvokeResponseData(&commandHandler, aNeedStatusCode);
    err = commandHandler.Finalize(commandPacket);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

#if CHIP_CONFIG_IM_PRETTY_PRINT
    chip::System::PacketBufferTLVReader reader;
    InvokeResponseMessage::Parser invokeResponseMessageParser;
    reader.Init(std::move(commandPacket));
    err = invokeResponseMessageParser.Init(reader);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    invokeResponseMessageParser.PrettyPrint();
#endif

    //
    // Ordinarily, the ExchangeContext will close itself on a responder exchange when unwinding back from an
    // OnMessageReceived callback and not having sent a subsequent message. Since that isn't the case in this artificial setup here
    // (where we created a responder exchange that's not responding to anything), we need to explicitly close it out. This is not
    // expected in normal application logic.
    //
    exchange->Close();
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerWithSendSimpleCommandData)
{
    // Send response which has simple command data and command path
    ValidateCommandHandlerWithSendCommand(false /*aNeedStatusCode=false*/);
}

struct Fields
{
    static constexpr chip::CommandId GetCommandId() { return 4; }
    CHIP_ERROR Encode(TLV::TLVWriter & aWriter, TLV::Tag aTag) const
    {
        TLV::TLVType outerContainerType;
        ReturnErrorOnFailure(aWriter.StartContainer(aTag, TLV::kTLVType_Structure, outerContainerType));
        ReturnErrorOnFailure(aWriter.PutBoolean(TLV::ContextTag(1), true));
        return aWriter.EndContainer(outerContainerType);
    }
};

struct BadFields
{
    static constexpr chip::CommandId GetCommandId() { return 4; }
    CHIP_ERROR Encode(TLV::TLVWriter & aWriter, TLV::Tag aTag) const
    {
        TLV::TLVType outerContainerType;
        uint8_t data[36] = { 0 };
        ReturnErrorOnFailure(aWriter.StartContainer(aTag, TLV::kTLVType_Structure, outerContainerType));
        // Just encode something bad to return a failure state here.
        for (uint8_t i = 1; i < UINT8_MAX; i++)
        {
            ReturnErrorOnFailure(app::DataModel::Encode(aWriter, TLV::ContextTag(i), ByteSpan(data)));
        }
        return aWriter.EndContainer(outerContainerType);
    }
};

STATIC_TEST(TestCommandInteraction, TestCommandHandlerCommandDataEncoding)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    app::CommandHandler commandHandler(nullptr);
    System::PacketBufferHandle commandPacket;

    TestExchangeDelegate delegate;
    auto exchange = ctx.NewExchangeToAlice(&delegate, false);
    commandHandler.mExchangeCtx.Grab(exchange);

    auto path = MakeTestCommandPath();

    commandHandler.AddResponse(ConcreteCommandPath(path.mEndpointId, path.mClusterId, path.mCommandId), Fields());
    err = commandHandler.Finalize(commandPacket);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

#if CHIP_CONFIG_IM_PRETTY_PRINT
    chip::System::PacketBufferTLVReader reader;
    InvokeResponseMessage::Parser invokeResponseMessageParser;
    reader.Init(std::move(commandPacket));
    err = invokeResponseMessageParser.Init(reader);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    invokeResponseMessageParser.PrettyPrint();
#endif

    //
    // Ordinarily, the ExchangeContext will close itself on a responder exchange when unwinding back from an
    // OnMessageReceived callback and not having sent a subsequent message. Since that isn't the case in this artificial setup here
    //  (where we created a responder exchange that's not responding to anything), we need to explicitly close it out. This is not
    //  expected in normal application logic.
    //
    exchange->Close();
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerCommandEncodeFailure)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    app::CommandHandler commandHandler(nullptr);
    System::PacketBufferHandle commandPacket;

    TestExchangeDelegate delegate;
    auto exchange = ctx.NewExchangeToAlice(&delegate, false);
    commandHandler.mExchangeCtx.Grab(exchange);

    auto path = MakeTestCommandPath();

    commandHandler.AddResponse(ConcreteCommandPath(path.mEndpointId, path.mClusterId, path.mCommandId), BadFields());
    err = commandHandler.Finalize(commandPacket);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

#if CHIP_CONFIG_IM_PRETTY_PRINT
    chip::System::PacketBufferTLVReader reader;
    InvokeResponseMessage::Parser invokeResponseMessageParser;
    reader.Init(std::move(commandPacket));
    err = invokeResponseMessageParser.Init(reader);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    invokeResponseMessageParser.PrettyPrint();
#endif

    //
    // Ordinarily, the ExchangeContext will close itself on a responder exchange when unwinding back from an
    // OnMessageReceived callback and not having sent a subsequent message. Since that isn't the case in this artificial setup here
    // (where we created a responder exchange that's not responding to anything), we need to explicitly close it out. This is not
    // expected in normal application logic.
    //
    exchange->Close();
}

// Command Sender sends malformed invoke request, handler fails to process it and sends status report with invalid action
STATIC_TEST(TestCommandInteraction, TestCommandHandlerInvalidMessageSync)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvalidInvokeRequestData(&commandSender);
    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);
    EXPECT_TRUE(mockCommandSenderDelegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

// Command Sender sends malformed invoke request, this command is aysnc command, handler fails to process it and sends status
// report with invalid action
STATIC_TEST(TestCommandInteraction, TestCommandHandlerInvalidMessageAsync)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());
    asyncCommand = true;
    AddInvalidInvokeRequestData(&commandSender);
    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Decrease CommandHandler refcount and send response
    asyncCommandHandle = nullptr;

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);
    EXPECT_TRUE(mockCommandSenderDelegate.mError == CHIP_IM_GLOBAL_STATUS(InvalidAction));
    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerCommandEncodeExternalFailure)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    app::CommandHandler commandHandler(nullptr);
    System::PacketBufferHandle commandPacket;

    TestExchangeDelegate delegate;
    auto exchange = ctx.NewExchangeToAlice(&delegate, false);
    commandHandler.mExchangeCtx.Grab(exchange);

    auto path = MakeTestCommandPath();

    err = commandHandler.AddResponseData(ConcreteCommandPath(path.mEndpointId, path.mClusterId, path.mCommandId), BadFields());
    EXPECT_TRUE(err != CHIP_NO_ERROR);
    commandHandler.AddStatus(ConcreteCommandPath(path.mEndpointId, path.mClusterId, path.mCommandId),
                             Protocols::InteractionModel::Status::Failure);
    err = commandHandler.Finalize(commandPacket);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

#if CHIP_CONFIG_IM_PRETTY_PRINT
    chip::System::PacketBufferTLVReader reader;
    InvokeResponseMessage::Parser invokeResponseMessageParser;
    reader.Init(std::move(commandPacket));
    err = invokeResponseMessageParser.Init(reader);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    invokeResponseMessageParser.PrettyPrint();
#endif

    //
    // Ordinarily, the ExchangeContext will close itself on a responder exchange when unwinding back from an
    // OnMessageReceived callback and not having sent a subsequent message. Since that isn't the case in this artificial setup here
    // (where we created a responder exchange that's not responding to anything), we need to explicitly close it out. This is not
    // expected in normal application logic.
    //
    exchange->Close();
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerWithSendSimpleStatusCode)
{
    // Send response which has simple status code and command path
    ValidateCommandHandlerWithSendCommand(true /*aNeedStatusCode=true*/);
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerWithProcessReceivedNotExistCommand)
{
    app::CommandHandler commandHandler(&mockCommandHandlerDelegate);
    System::PacketBufferHandle commandDatabuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    TestExchangeDelegate delegate;
    commandHandler.mExchangeCtx.Grab(ctx.NewExchangeToAlice(&delegate));
    // Use some invalid endpoint / cluster / command.
    GenerateInvokeRequest(commandDatabuf, /* aIsTimedRequest = */ false, 0xEF /* command */, 0xADBE /* cluster */,
                          0xDE /* endpoint */);

    // TODO: Need to find a way to get the response instead of only check if a function on key path is called.
    // We should not reach CommandDispatch if requested command does not exist.
    chip::isCommandDispatched = false;
    commandHandler.ProcessInvokeRequest(std::move(commandDatabuf), false);
    EXPECT_TRUE(!chip::isCommandDispatched);
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerWithProcessReceivedEmptyDataMsg)
{
    bool allBooleans[] = { true, false };
    for (auto messageIsTimed : allBooleans)
    {
        for (auto transactionIsTimed : allBooleans)
        {
            app::CommandHandler commandHandler(&mockCommandHandlerDelegate);
            System::PacketBufferHandle commandDatabuf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

            TestExchangeDelegate delegate;
            auto exchange = ctx.NewExchangeToAlice(&delegate, false);
            commandHandler.mExchangeCtx.Grab(exchange);

            chip::isCommandDispatched = false;
            GenerateInvokeRequest(commandDatabuf, messageIsTimed, kTestCommandIdNoData);
            Protocols::InteractionModel::Status status =
                commandHandler.ProcessInvokeRequest(std::move(commandDatabuf), transactionIsTimed);
            if (messageIsTimed != transactionIsTimed)
            {
                EXPECT_TRUE(status == Protocols::InteractionModel::Status::UnsupportedAccess);
            }
            else
            {
                EXPECT_TRUE(status == Protocols::InteractionModel::Status::Success);
            }
            EXPECT_TRUE(chip::isCommandDispatched == (messageIsTimed == transactionIsTimed));

            //
            // Ordinarily, the ExchangeContext will close itself on a responder exchange when unwinding back from an
            // OnMessageReceived callback and not having sent a subsequent message (as is the case when calling ProcessInvokeRequest
            // above, which doesn't actually send back a response in these cases). Since that isn't the case in this artificial
            // setup here (where we created a responder exchange that's not responding to anything), we need to explicitly close it
            // out. This is not expected in normal application logic.
            //
            exchange->Close();
        }
    }
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderCommandSuccessResponseFlow)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender);
    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 1 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderCommandAsyncSuccessResponseFlow)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender);
    asyncCommand = true;
    err          = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 0 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 1);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 2);

    // Decrease CommandHandler refcount and send response
    asyncCommandHandle = nullptr;

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 1 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderCommandSpecificResponseFlow)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender, kTestCommandIdCommandSpecificResponse);
    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 1 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderCommandFailureResponseFlow)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender, kTestNonExistCommandId);
    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

STATIC_TEST(TestCommandInteraction, TestCommandSenderAbruptDestruction)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    //
    // Don't send back a response, just keep the CommandHandler
    // hanging to give us enough time to do what we want with the CommandSender object.
    //
    sendResponse = false;

    mockCommandSenderDelegate.ResetCounter();

    {
        app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

        AddInvokeRequestData(&commandSender, kTestCommandIdCommandSpecificResponse);
        err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());

        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        //
        // No callbacks should be invoked yet - let's validate that.
        //
        EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 0 &&
                    mockCommandSenderDelegate.onErrorCalledTimes == 0);

        EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 1);
    }

    //
    // Upon the sender being destructed by the application, our exchange should get cleaned up too.
    //
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
}

STATIC_TEST(TestCommandInteraction, TestCommandHandlerRejectMultipleCommands)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    isCommandDispatched = false;
    mockCommandSenderDelegate.ResetCounter();
    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    {
        // Command ID is not important here, since the command handler should reject the commands without handling it.
        auto commandPathParams = MakeTestCommandPath(kTestCommandIdCommandSpecificResponse);

        commandSender.AllocateBuffer();

        // CommandSender does not support sending multiple commands with public API, so we craft a message manaully.
        for (int i = 0; i < 2; i++)
        {
            InvokeRequests::Builder & invokeRequests = commandSender.mInvokeRequestBuilder.GetInvokeRequests();
            CommandDataIB::Builder & invokeRequest   = invokeRequests.CreateCommandData();
            EXPECT_TRUE(CHIP_NO_ERROR == invokeRequests.GetError());
            CommandPathIB::Builder & path = invokeRequest.CreatePath();
            EXPECT_TRUE(CHIP_NO_ERROR == invokeRequest.GetError());
            EXPECT_TRUE(CHIP_NO_ERROR == path.Encode(commandPathParams));
            EXPECT_TRUE(CHIP_NO_ERROR ==
                        invokeRequest.GetWriter()->StartContainer(TLV::ContextTag(CommandDataIB::Tag::kFields),
                                                                  TLV::kTLVType_Structure,
                                                                  commandSender.mDataElementContainerType));
            EXPECT_TRUE(CHIP_NO_ERROR == invokeRequest.GetWriter()->PutBoolean(chip::TLV::ContextTag(1), true));
            EXPECT_TRUE(CHIP_NO_ERROR == invokeRequest.GetWriter()->EndContainer(commandSender.mDataElementContainerType));
            EXPECT_TRUE(CHIP_NO_ERROR == invokeRequest.EndOfCommandDataIB());
        }

        EXPECT_TRUE(CHIP_NO_ERROR == commandSender.mInvokeRequestBuilder.GetInvokeRequests().EndOfInvokeRequests());
        EXPECT_TRUE(CHIP_NO_ERROR == commandSender.mInvokeRequestBuilder.EndOfInvokeRequestMessage());

        commandSender.MoveToState(app::CommandSender::State::AddedCommand);
    }

    err = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    EXPECT_TRUE(mockCommandSenderDelegate.onResponseCalledTimes == 0 && mockCommandSenderDelegate.onFinalCalledTimes == 1 &&
                mockCommandSenderDelegate.onErrorCalledTimes == 1);
    EXPECT_TRUE(!chip::isCommandDispatched);

    EXPECT_TRUE(GetNumActiveHandlerObjects() == 0);
    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);
}

#if CONFIG_BUILD_FOR_HOST_UNIT_TEST
//
// This test needs a special unit-test only API being exposed in ExchangeContext to be able to correctly simulate
// the release of a session on the exchange.
//

STATIC_TEST(TestCommandInteraction, TestCommandHandlerReleaseWithExchangeClosed)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    app::CommandSender commandSender(&mockCommandSenderDelegate, &ctx.GetExchangeManager());

    AddInvokeRequestData(&commandSender);
    asyncCommandHandle = nullptr;
    asyncCommand       = true;
    err                = commandSender.SendCommandRequest(ctx.GetSessionBobToAlice());

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx.DrainAndServiceIO();

    // Verify that async command handle has been allocated
    EXPECT_TRUE(asyncCommandHandle.Get() != nullptr);

    // Mimick closure of the exchange that would happen on a session release and verify that releasing the handle there-after
    // is handled gracefully.
    asyncCommandHandle.Get()->mExchangeCtx->GetSessionHolder().Release();
    asyncCommandHandle.Get()->mExchangeCtx->OnSessionReleased();

    asyncCommandHandle = nullptr;
}
#endif

} // namespace app
} // namespace chip