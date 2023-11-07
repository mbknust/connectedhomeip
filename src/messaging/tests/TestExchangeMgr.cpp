/*
 *
 *    Copyright (c) 2020-2021 Project CHIP Authors
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
 *      This file implements unit tests for the ExchangeManager implementation.
 */

#include <lib/core/CHIPCore.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/UnitTestContext.h>

#include <messaging/ExchangeContext.h>
#include <messaging/ExchangeMgr.h>
#include <messaging/Flags.h>
#include <messaging/tests/MessagingContext.h>
#include <protocols/Protocols.h>
#include <transport/SessionManager.h>
#include <transport/TransportMgr.h>

#include <gtest/gtest.h>
#include <nlbyteorder.h>

#include <errno.h>
#include <utility>

namespace {

using namespace chip;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::Messaging;

using TestContext = Test::LoopbackMessagingContext;

enum : uint8_t
{
    kMsgType_TEST1 = 1,
    kMsgType_TEST2 = 2,
};

class MockAppDelegate : public UnsolicitedMessageHandler, public ExchangeDelegate
{
public:
    CHIP_ERROR OnUnsolicitedMessageReceived(const PayloadHeader & payloadHeader, ExchangeDelegate *& newDelegate) override
    {
        newDelegate = this;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR OnMessageReceived(ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && buffer) override
    {
        IsOnMessageReceivedCalled = true;
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(ExchangeContext * ec) override {}

    bool IsOnMessageReceivedCalled = false;
};

class WaitForTimeoutDelegate : public ExchangeDelegate
{
public:
    CHIP_ERROR OnMessageReceived(ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && buffer) override
    {
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(ExchangeContext * ec) override { IsOnResponseTimeoutCalled = true; }

    bool IsOnResponseTimeoutCalled = false;
};

class ExpireSessionFromTimeoutDelegate : public WaitForTimeoutDelegate
{
    void OnResponseTimeout(ExchangeContext * ec) override
    {
        ec->GetSessionHandle()->AsSecureSession()->MarkForEviction();
        WaitForTimeoutDelegate::OnResponseTimeout(ec);
    }
};

class TestExchangeMgr : public ::testing::Test
{
public: // protected
    static TestContext ctx;

    static void SetUpTestSuite() { ASSERT_EQ(ctx.Init(), CHIP_NO_ERROR); }
    static void TearDownTestSuite() { ctx.Shutdown(); }
};

TestContext TestExchangeMgr::ctx;

TEST_F(TestExchangeMgr, CheckNewContextTest)
{
    MockAppDelegate mockAppDelegate;
    ExchangeContext * ec1 = ctx.NewExchangeToBob(&mockAppDelegate);
    ASSERT_TRUE(ec1 != nullptr);
    EXPECT_TRUE(ec1->IsInitiator() == true);
    EXPECT_TRUE(ec1->GetSessionHandle() == ctx.GetSessionAliceToBob());
    EXPECT_TRUE(ec1->GetDelegate() == &mockAppDelegate);

    ExchangeContext * ec2 = ctx.NewExchangeToAlice(&mockAppDelegate);
    ASSERT_TRUE(ec2 != nullptr);
    EXPECT_TRUE(ec2->GetExchangeId() > ec1->GetExchangeId());
    EXPECT_TRUE(ec2->GetSessionHandle() == ctx.GetSessionBobToAlice());

    ec1->Close();
    ec2->Close();
}

TEST_F(TestExchangeMgr, CheckSessionExpirationBasics)
{
    MockAppDelegate sendDelegate;
    ExchangeContext * ec1 = ctx.NewExchangeToBob(&sendDelegate);

    // Expire the session this exchange is supposedly on.
    ec1->GetSessionHandle()->AsSecureSession()->MarkForEviction();

    MockAppDelegate receiveDelegate;
    CHIP_ERROR err =
        ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Protocols::BDX::Id, kMsgType_TEST1, &receiveDelegate);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = ec1->SendMessage(Protocols::BDX::Id, kMsgType_TEST1, System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize),
                           SendFlags(Messaging::SendMessageFlags::kNoAutoRequestAck));
    EXPECT_TRUE(err != CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    EXPECT_TRUE(!receiveDelegate.IsOnMessageReceivedCalled);
    ec1->Close();

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Protocols::BDX::Id, kMsgType_TEST1);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // recreate closed session.
    EXPECT_TRUE(ctx.CreateSessionAliceToBob() == CHIP_NO_ERROR);
}

TEST_F(TestExchangeMgr, CheckSessionExpirationTimeout)
{
    WaitForTimeoutDelegate sendDelegate;
    ExchangeContext * ec1 = ctx.NewExchangeToBob(&sendDelegate);

    ec1->SendMessage(Protocols::BDX::Id, kMsgType_TEST1, System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize),
                     SendFlags(Messaging::SendMessageFlags::kExpectResponse).Set(Messaging::SendMessageFlags::kNoAutoRequestAck));

    ctx.DrainAndServiceIO();
    EXPECT_TRUE(!sendDelegate.IsOnResponseTimeoutCalled);

    // Expire the session this exchange is supposedly on.  This should close the exchange.
    ec1->GetSessionHandle()->AsSecureSession()->MarkForEviction();
    EXPECT_TRUE(sendDelegate.IsOnResponseTimeoutCalled);

    // recreate closed session.
    EXPECT_TRUE(ctx.CreateSessionAliceToBob() == CHIP_NO_ERROR);
}

TEST_F(TestExchangeMgr, CheckSessionExpirationDuringTimeout)
{
    using namespace chip::System::Clock::Literals;

    ExpireSessionFromTimeoutDelegate sendDelegate;
    ExchangeContext * ec1 = ctx.NewExchangeToBob(&sendDelegate);

    auto timeout = System::Clock::Timeout(100);
    ec1->SetResponseTimeout(timeout);

    EXPECT_TRUE(!sendDelegate.IsOnResponseTimeoutCalled);

    ec1->SendMessage(Protocols::BDX::Id, kMsgType_TEST1, System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize),
                     SendFlags(Messaging::SendMessageFlags::kExpectResponse).Set(Messaging::SendMessageFlags::kNoAutoRequestAck));
    ctx.DrainAndServiceIO();

    // Wait for our timeout to elapse. Give it an extra 1000ms of slack,
    // because if we lose the timeslice for longer than the slack we could end
    // up breaking out of the loop before the timeout timer has actually fired.
    ctx.GetIOContext().DriveIOUntil(timeout + 1000_ms32, [&sendDelegate] { return sendDelegate.IsOnResponseTimeoutCalled; });

    EXPECT_TRUE(sendDelegate.IsOnResponseTimeoutCalled);

    // recreate closed session.
    EXPECT_TRUE(ctx.CreateSessionAliceToBob() == CHIP_NO_ERROR);
}

TEST_F(TestExchangeMgr, CheckUmhRegistrationTest)
{
    CHIP_ERROR err;
    MockAppDelegate mockAppDelegate;

    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForProtocol(Protocols::BDX::Id, &mockAppDelegate);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Protocols::Echo::Id, kMsgType_TEST1, &mockAppDelegate);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForProtocol(Protocols::BDX::Id);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForProtocol(Protocols::Echo::Id);
    EXPECT_TRUE(err != CHIP_NO_ERROR);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Protocols::Echo::Id, kMsgType_TEST1);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Protocols::Echo::Id, kMsgType_TEST2);
    EXPECT_TRUE(err != CHIP_NO_ERROR);
}

TEST_F(TestExchangeMgr, CheckExchangeMessages)
{
    CHIP_ERROR err;

    // create solicited exchange
    MockAppDelegate mockSolicitedAppDelegate;
    ExchangeContext * ec1 = ctx.NewExchangeToAlice(&mockSolicitedAppDelegate);

    // create unsolicited exchange
    MockAppDelegate mockUnsolicitedAppDelegate;
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Protocols::BDX::Id, kMsgType_TEST1,
                                                                            &mockUnsolicitedAppDelegate);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // send a malicious packet
    ec1->SendMessage(Protocols::BDX::Id, kMsgType_TEST2, System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize),
                     SendFlags(Messaging::SendMessageFlags::kNoAutoRequestAck));

    ctx.DrainAndServiceIO();
    EXPECT_TRUE(!mockUnsolicitedAppDelegate.IsOnMessageReceivedCalled);

    ec1 = ctx.NewExchangeToAlice(&mockSolicitedAppDelegate);

    // send a good packet
    ec1->SendMessage(Protocols::BDX::Id, kMsgType_TEST1, System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize),
                     SendFlags(Messaging::SendMessageFlags::kNoAutoRequestAck));

    ctx.DrainAndServiceIO();
    EXPECT_TRUE(mockUnsolicitedAppDelegate.IsOnMessageReceivedCalled);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Protocols::BDX::Id, kMsgType_TEST1);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

} // namespace
