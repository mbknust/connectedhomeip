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
 *      This file implements unit tests for the MessageCounterManager implementation.
 */

#include <lib/core/CHIPCore.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/UnitTestContext.h>

#include <lib/support/logging/CHIPLogging.h>
#include <messaging/ExchangeContext.h>
#include <messaging/ExchangeMgr.h>
#include <messaging/Flags.h>
#include <messaging/tests/MessagingContext.h>
#include <protocols/Protocols.h>
#include <protocols/echo/Echo.h>
#include <transport/SessionManager.h>
#include <transport/TransportMgr.h>

#include <gtest/gtest.h>
#include <nlbyteorder.h>

#include <errno.h>

namespace {

using namespace chip;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::Messaging;
using namespace chip::Protocols;

using TestContext = chip::Test::LoopbackMessagingContext;

const char PAYLOAD[] = "Hello!";

class MockAppDelegate : public ExchangeDelegate
{
public:
    CHIP_ERROR OnMessageReceived(ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && msgBuf) override
    {
        ++ReceiveHandlerCallCount;
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(ExchangeContext * ec) override {}

    int ReceiveHandlerCallCount = 0;
};

class TestMessageCounterManager : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(ctx.Init() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { ctx.Shutdown(); }
    static TestContext ctx;
};

TestContext TestMessageCounterManager::ctx;

TEST_F(TestMessageCounterManager, MessageCounterSyncProcess)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    SessionHandle localSession = ctx.GetSessionBobToAlice();
    SessionHandle peerSession  = ctx.GetSessionAliceToBob();

    Transport::SecureSession * localState = ctx.GetSecureSessionManager().GetSecureSession(localSession);
    Transport::SecureSession * peerState  = ctx.GetSecureSessionManager().GetSecureSession(peerSession);

    localState->GetSessionMessageCounter().GetPeerMessageCounter().Reset();
    err = ctx.GetMessageCounterManager().SendMsgCounterSyncReq(localSession, localState);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MessageCounter & peerCounter      = peerState->GetSessionMessageCounter().GetLocalMessageCounter();
    PeerMessageCounter & localCounter = localState->GetSessionMessageCounter().GetPeerMessageCounter();
    EXPECT_TRUE(localCounter.IsSynchronized());
    EXPECT_TRUE(localCounter.GetCounter() == peerCounter.Value());
}

TEST_F(TestMessageCounterManager, CheckReceiveMessage)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    SessionHandle peerSession            = ctx.GetSessionAliceToBob();
    Transport::SecureSession * peerState = ctx.GetSecureSessionManager().GetSecureSession(peerSession);
    peerState->GetSessionMessageCounter().GetPeerMessageCounter().Reset();

    MockAppDelegate callback;
    ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(chip::Protocols::Echo::MsgType::EchoRequest, &callback);

    uint16_t payload_len              = sizeof(PAYLOAD);
    System::PacketBufferHandle msgBuf = MessagePacketBuffer::NewWithData(PAYLOAD, payload_len);
    EXPECT_TRUE(!msgBuf.IsNull());

    Messaging::ExchangeContext * ec = ctx.NewExchangeToAlice(nullptr);
    EXPECT_TRUE(ec != nullptr);

    err = ec->SendMessage(chip::Protocols::Echo::MsgType::EchoRequest, std::move(msgBuf),
                          Messaging::SendFlags{ Messaging::SendMessageFlags::kNoAutoRequestAck });
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(peerState->GetSessionMessageCounter().GetPeerMessageCounter().IsSynchronized());
    EXPECT_TRUE(callback.ReceiveHandlerCallCount == 1);
}

} // namespace
