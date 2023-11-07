/*
 *    Copyright (c) 2022 Project CHIP Authors
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
 *      This file implements unit tests for aborting existing exchanges (except
 *      one) for a fabric.
 */

#include <gtest/gtest.h>
#include <lib/support/UnitTestContext.h>

#include <lib/support/UnitTestUtils.h>
#include <messaging/ExchangeContext.h>
#include <messaging/ExchangeMgr.h>
#include <messaging/ReliableMessageProtocolConfig.h>
#include <messaging/tests/MessagingContext.h>
#include <protocols/echo/Echo.h>
#include <system/SystemPacketBuffer.h>
#include <transport/SessionManager.h>

namespace {

using namespace chip;
using namespace chip::Messaging;
using namespace chip::System;
using namespace chip::System::Clock::Literals;
using namespace chip::Protocols;

using TestContext = Test::LoopbackMessagingContext;

class MockAppDelegate : public ExchangeDelegate
{
public:
    CHIP_ERROR OnMessageReceived(ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && buffer) override
    {
        mOnMessageReceivedCalled = true;
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(ExchangeContext * ec) override {}

    bool mOnMessageReceivedCalled = false;
};

class TestAbortExchangesForFabric : public ::testing::Test
{
public: // protected
    static TestContext ctx;

    static void SetUpTestSuite() { ASSERT_EQ(ctx.Init(), CHIP_NO_ERROR); }
    static void TearDownTestSuite() { ctx.Shutdown(); }
};

TestContext TestAbortExchangesForFabric::ctx;

void CommonCheckAbortAllButOneExchange(TestContext & ctx, bool dropResponseMessages)
{
    // We want to have two sessions using the same fabric id that we use for
    // creating our exchange contexts.  That lets us test exchanges on the same
    // session as the "special exchange" as well as on other sessions.
    auto & sessionManager = ctx.GetSecureSessionManager();

    // Use key ids that are not going to collide with anything else that ctx is
    // doing.
    // TODO: These should really be CASE sessions...
    SessionHolder session1;
    CHIP_ERROR err = sessionManager.InjectCaseSessionWithTestKey(session1, 100, 101, ctx.GetAliceFabric()->GetNodeId(),
                                                                 ctx.GetBobFabric()->GetNodeId(), ctx.GetAliceFabricIndex(),
                                                                 ctx.GetBobAddress(), CryptoContext::SessionRole::kInitiator, {});

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    SessionHolder session1Reply;
    err = sessionManager.InjectCaseSessionWithTestKey(session1Reply, 101, 100, ctx.GetBobFabric()->GetNodeId(),
                                                      ctx.GetAliceFabric()->GetNodeId(), ctx.GetBobFabricIndex(),
                                                      ctx.GetAliceAddress(), CryptoContext::SessionRole::kResponder, {});

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // TODO: Ideally this would go to a different peer, but we don't have that
    // set up right now: only Alice and Bob have useful node ids and whatnot.
    SessionHolder session2;
    err = sessionManager.InjectCaseSessionWithTestKey(session2, 200, 201, ctx.GetAliceFabric()->GetNodeId(),
                                                      ctx.GetBobFabric()->GetNodeId(), ctx.GetAliceFabricIndex(),
                                                      ctx.GetBobAddress(), CryptoContext::SessionRole::kInitiator, {});

    EXPECT_TRUE(err == CHIP_NO_ERROR);

    SessionHolder session2Reply;
    err = sessionManager.InjectCaseSessionWithTestKey(session2Reply, 101, 100, ctx.GetBobFabric()->GetNodeId(),
                                                      ctx.GetAliceFabric()->GetNodeId(), ctx.GetBobFabricIndex(),
                                                      ctx.GetAliceAddress(), CryptoContext::SessionRole::kResponder, {});
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    auto & exchangeMgr = ctx.GetExchangeManager();

    MockAppDelegate delegate;
    Echo::EchoServer server;
    err = server.Init(&exchangeMgr);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    auto & loopback = ctx.GetLoopback();

    auto trySendMessage = [&](ExchangeContext * exchange, SendMessageFlags flags) {
        PacketBufferHandle buffer = MessagePacketBuffer::New(0);
        EXPECT_TRUE(!buffer.IsNull());
        return exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), flags);
    };

    auto sendAndDropMessage = [&](ExchangeContext * exchange, SendMessageFlags flags) {
        // Send a message on the given exchange with the given flags, make sure
        // it's not delivered.
        loopback.mNumMessagesToDrop   = 1;
        loopback.mDroppedMessageCount = 0;

        err = trySendMessage(exchange, flags);
        EXPECT_TRUE(err == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();
        EXPECT_TRUE(!delegate.mOnMessageReceivedCalled);
        EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    };

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();

    // We want to test three possible exchange states:
    // 1) Closed but waiting for ack.
    // 2) Waiting for a response.
    // 3) Waiting for a send.
    auto * waitingForAck1 = exchangeMgr.NewContext(session1.Get().Value(), &delegate);
    EXPECT_TRUE(waitingForAck1 != nullptr);
    sendAndDropMessage(waitingForAck1, SendMessageFlags::kNone);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    auto * waitingForAck2 = exchangeMgr.NewContext(session2.Get().Value(), &delegate);
    EXPECT_TRUE(waitingForAck2 != nullptr);
    sendAndDropMessage(waitingForAck2, SendMessageFlags::kNone);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 2);

    auto * waitingForIncomingMessage1 = exchangeMgr.NewContext(session1.Get().Value(), &delegate);
    EXPECT_TRUE(waitingForIncomingMessage1 != nullptr);
    sendAndDropMessage(waitingForIncomingMessage1, SendMessageFlags::kExpectResponse);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 3);

    auto * waitingForIncomingMessage2 = exchangeMgr.NewContext(session2.Get().Value(), &delegate);
    EXPECT_TRUE(waitingForIncomingMessage2 != nullptr);
    sendAndDropMessage(waitingForIncomingMessage2, SendMessageFlags::kExpectResponse);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 4);

    auto * waitingForSend1 = exchangeMgr.NewContext(session1.Get().Value(), &delegate);
    EXPECT_TRUE(waitingForSend1 != nullptr);
    waitingForSend1->WillSendMessage();

    auto * waitingForSend2 = exchangeMgr.NewContext(session2.Get().Value(), &delegate);
    EXPECT_TRUE(waitingForSend2 != nullptr);
    waitingForSend2->WillSendMessage();

    // Grab handles to our sessions now, before we evict things.
    const auto & sessionHandle1 = session1.Get();
    const auto & sessionHandle2 = session2.Get();

    session1->AsSecureSession()->SetRemoteMRPConfig(ReliableMessageProtocolConfig(
        Test::MessagingContext::kResponsiveIdleRetransTimeout, Test::MessagingContext::kResponsiveActiveRetransTimeout));

    session1Reply->AsSecureSession()->SetRemoteMRPConfig(ReliableMessageProtocolConfig(
        Test::MessagingContext::kResponsiveIdleRetransTimeout, Test::MessagingContext::kResponsiveActiveRetransTimeout));

    EXPECT_TRUE(session1);
    EXPECT_TRUE(session2);
    auto * specialExhange = exchangeMgr.NewContext(session1.Get().Value(), &delegate);
    specialExhange->AbortAllOtherCommunicationOnFabric();

    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    EXPECT_TRUE(!session1);
    EXPECT_TRUE(!session2);

    EXPECT_TRUE(exchangeMgr.NewContext(sessionHandle1.Value(), &delegate) == nullptr);
    EXPECT_TRUE(exchangeMgr.NewContext(sessionHandle2.Value(), &delegate) == nullptr);

    // Make sure we can't send messages on any of the other exchanges.
    EXPECT_TRUE(trySendMessage(waitingForSend1, SendMessageFlags::kExpectResponse) != CHIP_NO_ERROR);
    EXPECT_TRUE(trySendMessage(waitingForSend2, SendMessageFlags::kExpectResponse) != CHIP_NO_ERROR);

    // Make sure we can send a message on the special exchange.
    EXPECT_TRUE(!delegate.mOnMessageReceivedCalled);
    err = trySendMessage(specialExhange, SendMessageFlags::kNone);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    // Should be waiting for an ack now.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    if (dropResponseMessages)
    {
        //
        // This version of the test allows us to validate logic that marks expired sessions as defunct
        // on encountering an MRP failure.
        //
        loopback.mNumMessagesToDrop   = Test::LoopbackTransport::kUnlimitedMessageCount;
        loopback.mDroppedMessageCount = 0;

        //
        // We've set the session into responsive mode by altering the MRP intervals, so we should be able to
        // trigger a MRP failure due to timing out waiting for an ACK.
        //
        auto waitTimeout = System::Clock::Milliseconds32(1000);
        // Account for the retry delay booster, so that we do not timeout our IO processing before the
        // retransmission failure is triggered.
        waitTimeout += CHIP_CONFIG_RMP_DEFAULT_MAX_RETRANS * CHIP_CONFIG_MRP_RETRY_INTERVAL_SENDER_BOOST;

        ctx.GetIOContext().DriveIOUntil(waitTimeout, [&]() { return false; });
    }
    else
    {
        ctx.DrainAndServiceIO();
    }

    // Should not get an app-level response, since we are not expecting one.
    EXPECT_TRUE(!delegate.mOnMessageReceivedCalled);

    // We should have gotten our ack.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    waitingForSend1->Close();
    waitingForSend2->Close();

    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;
}

TEST_F(TestAbortExchangesForFabric, CheckAbortAllButOneExchange)
{
    CommonCheckAbortAllButOneExchange(ctx, false);
}

TEST_F(TestAbortExchangesForFabric, CheckAbortAllButOneExchangeResponseTimeout)
{
    CommonCheckAbortAllButOneExchange(ctx, true);
}

} // namespace
