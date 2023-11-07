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
 *      This file implements unit tests for the ReliableMessageProtocol
 *      implementation.
 */

#include <lib/core/CHIPCore.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/UnitTestContext.h>

#include <lib/support/UnitTestUtils.h>
#include <messaging/ReliableMessageContext.h>
#include <messaging/ReliableMessageMgr.h>
#include <messaging/ReliableMessageProtocolConfig.h>
#include <protocols/Protocols.h>
#include <protocols/echo/Echo.h>
#include <transport/SessionManager.h>
#include <transport/TransportMgr.h>

#include <gtest/gtest.h>
#include <nlbyteorder.h>

#include <errno.h>

#include <messaging/ExchangeContext.h>
#include <messaging/ExchangeMgr.h>
#include <messaging/Flags.h>
#include <messaging/tests/MessagingContext.h>

namespace {

using namespace chip;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::Messaging;
using namespace chip::Protocols;
using namespace chip::System::Clock::Literals;

using TestContext = Test::LoopbackMessagingContext;

const char PAYLOAD[] = "Hello!";

// The CHIP_CONFIG_MRP_RETRY_INTERVAL_SENDER_BOOST can be set to non-zero value
// to boost the retransmission timeout for a high latency network like Thread to
// avoid spurious retransmits.
//
// This adds extra I/O time to account for this. See the documentation for
// CHIP_CONFIG_MRP_RETRY_INTERVAL_SENDER_BOOST for more details.
constexpr auto retryBoosterTimeout = CHIP_CONFIG_RMP_DEFAULT_MAX_RETRANS * CHIP_CONFIG_MRP_RETRY_INTERVAL_SENDER_BOOST;

class MockAppDelegate : public UnsolicitedMessageHandler, public ExchangeDelegate
{
public:
    MockAppDelegate(TestContext & ctx) : mTestContext(ctx) {}

    CHIP_ERROR OnUnsolicitedMessageReceived(const PayloadHeader & payloadHeader, ExchangeDelegate *& newDelegate) override
    {
        // Handle messages by myself
        newDelegate = this;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR OnMessageReceived(ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && buffer) override
    {
        IsOnMessageReceivedCalled = true;
        if (payloadHeader.IsAckMsg())
        {
            mReceivedPiggybackAck = true;
        }
        if (mDropAckResponse)
        {
            auto * rc = ec->GetReliableMessageContext();
            if (rc->HasPiggybackAckPending())
            {
                // Make sure we don't accidentally retransmit and end up acking
                // the retransmit.
                rc->GetReliableMessageMgr()->StopTimer();
                (void) rc->TakePendingPeerAckMessageCounter();
            }
        }

        if (mExchange != ec)
        {
            CloseExchangeIfNeeded();
        }

        if (!mRetainExchange)
        {
            ec = nullptr;
        }
        else
        {
            ec->WillSendMessage();
        }
        mExchange = ec;

        if (mTestSuite != nullptr)
        {
            NL_TEST_ASSERT(mTestSuite, buffer->TotalLength() == sizeof(PAYLOAD));
            NL_TEST_ASSERT(mTestSuite, memcmp(buffer->Start(), PAYLOAD, buffer->TotalLength()) == 0);
        }
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(ExchangeContext * ec) override {}

    void CloseExchangeIfNeeded()
    {
        if (mExchange != nullptr)
        {
            mExchange->Close();
            mExchange = nullptr;
        }
    }

    void SetDropAckResponse(bool dropResponse)
    {
        mDropAckResponse = dropResponse;
        if (!mDropAckResponse)
        {
            // Restart the MRP retransmit timer, now that we are not going to be
            // dropping acks anymore, so we send out pending retransmits, if
            // any, as needed.
            mTestContext.GetExchangeManager().GetReliableMessageMgr()->StartTimer();
        }
    }

    bool IsOnMessageReceivedCalled = false;
    bool mReceivedPiggybackAck     = false;
    bool mRetainExchange           = false;
    ExchangeContext * mExchange    = nullptr;
    nlTestSuite * mTestSuite       = nullptr;

private:
    TestContext & mTestContext;
    bool mDropAckResponse = false;
};

class MockSessionEstablishmentExchangeDispatch : public Messaging::ApplicationExchangeDispatch
{
public:
    bool IsReliableTransmissionAllowed() const override { return mRetainMessageOnSend; }

    bool MessagePermitted(Protocols::Id protocol, uint8_t type) override { return true; }

    bool IsEncryptionRequired() const override { return mRequireEncryption; }

    bool mRetainMessageOnSend = true;

    bool mRequireEncryption = false;
};

class MockSessionEstablishmentDelegate : public UnsolicitedMessageHandler, public ExchangeDelegate
{
public:
    CHIP_ERROR OnUnsolicitedMessageReceived(const PayloadHeader & payloadHeader, ExchangeDelegate *& newDelegate) override
    {
        // Handle messages by myself
        newDelegate = this;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR OnMessageReceived(ExchangeContext * ec, const PayloadHeader & payloadHeader,
                                 System::PacketBufferHandle && buffer) override
    {
        IsOnMessageReceivedCalled = true;
        if (mTestSuite != nullptr)
        {
            NL_TEST_ASSERT(mTestSuite, buffer->TotalLength() == sizeof(PAYLOAD));
            NL_TEST_ASSERT(mTestSuite, memcmp(buffer->Start(), PAYLOAD, buffer->TotalLength()) == 0);
        }
        return CHIP_NO_ERROR;
    }

    void OnResponseTimeout(ExchangeContext * ec) override {}

    virtual ExchangeMessageDispatch & GetMessageDispatch() override { return mMessageDispatch; }

    bool IsOnMessageReceivedCalled = false;
    MockSessionEstablishmentExchangeDispatch mMessageDispatch;
    nlTestSuite * mTestSuite = nullptr;
};

struct BackoffComplianceTestVector
{
    uint8_t sendCount;
    System::Clock::Timeout backoffBase;
    System::Clock::Timeout backoffMin;
    System::Clock::Timeout backoffMax;
};

struct BackoffComplianceTestVector theBackoffComplianceTestVector[] = {
    {
        .sendCount   = 0,
        .backoffBase = System::Clock::Timeout(300),
        .backoffMin  = System::Clock::Timeout(330),
        .backoffMax  = System::Clock::Timeout(413),
    },
    {
        .sendCount   = 1,
        .backoffBase = System::Clock::Timeout(300),
        .backoffMin  = System::Clock::Timeout(330),
        .backoffMax  = System::Clock::Timeout(413),
    },
    {
        .sendCount   = 2,
        .backoffBase = System::Clock::Timeout(300),
        .backoffMin  = System::Clock::Timeout(528),
        .backoffMax  = System::Clock::Timeout(660),
    },
    {
        .sendCount   = 3,
        .backoffBase = System::Clock::Timeout(300),
        .backoffMin  = System::Clock::Timeout(844),
        .backoffMax  = System::Clock::Timeout(1057),
    },
    {
        .sendCount   = 4,
        .backoffBase = System::Clock::Timeout(300),
        .backoffMin  = System::Clock::Timeout(1351),
        .backoffMax  = System::Clock::Timeout(1690),
    },
    {
        .sendCount   = 5,
        .backoffBase = System::Clock::Timeout(300),
        .backoffMin  = System::Clock::Timeout(2162),
        .backoffMax  = System::Clock::Timeout(2704),
    },
    {
        .sendCount   = 6,
        .backoffBase = System::Clock::Timeout(300),
        .backoffMin  = System::Clock::Timeout(2162),
        .backoffMax  = System::Clock::Timeout(2704),
    },
    {
        .sendCount   = 0,
        .backoffBase = System::Clock::Timeout(4000),
        .backoffMin  = System::Clock::Timeout(4400),
        .backoffMax  = System::Clock::Timeout(5500),
    },
    {
        .sendCount   = 1,
        .backoffBase = System::Clock::Timeout(4000),
        .backoffMin  = System::Clock::Timeout(4400),
        .backoffMax  = System::Clock::Timeout(5500),
    },
    {
        .sendCount   = 2,
        .backoffBase = System::Clock::Timeout(4000),
        .backoffMin  = System::Clock::Timeout(7040),
        .backoffMax  = System::Clock::Timeout(8800),
    },
    {
        .sendCount   = 3,
        .backoffBase = System::Clock::Timeout(4000),
        .backoffMin  = System::Clock::Timeout(11264),
        .backoffMax  = System::Clock::Timeout(14081),
    },
    {
        .sendCount   = 4,
        .backoffBase = System::Clock::Timeout(4000),
        .backoffMin  = System::Clock::Timeout(18022),
        .backoffMax  = System::Clock::Timeout(22529),
    },
    {
        .sendCount   = 5,
        .backoffBase = System::Clock::Timeout(4000),
        .backoffMin  = System::Clock::Timeout(28835),
        .backoffMax  = System::Clock::Timeout(36045),
    },
    {
        .sendCount   = 6,
        .backoffBase = System::Clock::Timeout(4000),
        .backoffMin  = System::Clock::Timeout(28835),
        .backoffMax  = System::Clock::Timeout(36045),
    },
};

class TestReliableMessageProtocol : public ::testing::Test
{
public: // protected
    static TestContext ctx;

    static void SetUpTestSuite() { ASSERT_EQ(ctx.Init(), CHIP_NO_ERROR); }
    static void TearDownTestSuite() { ctx.Shutdown(); }

    void SetUp() override
    {
        ctx.GetSessionAliceToBob()->AsSecureSession()->SetRemoteMRPConfig(GetLocalMRPConfig().ValueOr(GetDefaultMRPConfig()));
        ctx.GetSessionBobToAlice()->AsSecureSession()->SetRemoteMRPConfig(GetLocalMRPConfig().ValueOr(GetDefaultMRPConfig()));
    }
    void TearDown() override {}
};

TestContext TestReliableMessageProtocol::ctx{};

TEST_F(TestReliableMessageProtocol, CheckAddClearRetrans)
{
    MockAppDelegate mockAppDelegate(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockAppDelegate);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm     = ctx.GetExchangeManager().GetReliableMessageMgr();
    ReliableMessageContext * rc = exchange->GetReliableMessageContext();
    EXPECT_TRUE(rm != nullptr);
    EXPECT_TRUE(rc != nullptr);

    ReliableMessageMgr::RetransTableEntry * entry;

    rm->AddToRetransTable(rc, &entry);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);
    rm->ClearRetransTable(*entry);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    exchange->Close();
}

/**
 * Tests MRP retransmission logic with the following scenario:
 *
 *      DUT = sender, PEER = remote device
 *
 * 1) DUT configured to use sleepy peer parameters of active = 64ms, idle = 64ms
 * 2) DUT sends message attempt #1 to PEER
 *      - Force PEER to drop message
 *      - Observe DUT timeout with no ack
 *      - Confirm MRP backoff interval is correct
 * 3) DUT resends message attempt #2 to PEER
 *      - Force PEER to drop message
 *      - Observe DUT timeout with no ack
 *      - Confirm MRP backoff interval is correct
 * 4) DUT resends message attempt #3 to PEER
 *      - Force PEER to drop message
 *      - Observe DUT timeout with no ack
 *      - Confirm MRP backoff interval is correct
 * 5) DUT resends message attempt #4 to PEER
 *      - Force PEER to drop message
 *      - Observe DUT timeout with no ack
 *      - Confirm MRP backoff interval is correct
 * 6) DUT resends message attempt #5 to PEER
 *      - PEER to acknowledge message
 *      - Observe DUT signal successful reliable transmission
 */
TEST_F(TestReliableMessageProtocol, CheckResendApplicationMessage)
{
    BackoffComplianceTestVector * expectedBackoff;
    System::Clock::Timestamp now, startTime;
    System::Clock::Timeout timeoutTime, margin;
    margin = System::Clock::Timeout(15);

    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockSender(ctx);
    // TODO: temporarily create a SessionHandle from node id, will be fix in PR 3602
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        System::Clock::Timestamp(300), // CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
        System::Clock::Timestamp(300), // CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
    });

    // Let's drop the initial message
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 4;
    loopback.mDroppedMessageCount = 0;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // Ensure the exchange stays open after we send (unlike the CheckCloseExchangeAndResendApplicationMessage case), by claiming to
    // expect a response.
    startTime = System::SystemClock().GetMonotonicTimestamp();
    err       = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendMessageFlags::kExpectResponse);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the initial message was dropped and was added to retransmit table
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 3);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Wait for the initial message to fail (should take 330-413ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32 + retryBoosterTimeout, [&] { return loopback.mSentMessageCount >= 2; });
    now         = System::SystemClock().GetMonotonicTimestamp();
    timeoutTime = now - startTime;
    ChipLogProgress(Test, "Attempt #1  Timeout : %" PRIu32 "ms", timeoutTime.count());
    expectedBackoff = &theBackoffComplianceTestVector[0];
    EXPECT_TRUE(timeoutTime >= expectedBackoff->backoffMin - margin);

    startTime = System::SystemClock().GetMonotonicTimestamp();
    ctx.DrainAndServiceIO();

    // Ensure the 1st retry was dropped, and is still there in the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 2);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Wait for the 1st retry to fail (should take 330-413ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32 + retryBoosterTimeout, [&] { return loopback.mSentMessageCount >= 3; });
    now         = System::SystemClock().GetMonotonicTimestamp();
    timeoutTime = now - startTime;
    ChipLogProgress(Test, "Attempt #2  Timeout : %" PRIu32 "ms", timeoutTime.count());
    expectedBackoff = &theBackoffComplianceTestVector[1];
    EXPECT_TRUE(timeoutTime >= expectedBackoff->backoffMin - margin);

    startTime = System::SystemClock().GetMonotonicTimestamp();
    ctx.DrainAndServiceIO();

    // Ensure the 2nd retry was dropped, and is still there in the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount == 3);
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 3);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Wait for the 2nd retry to fail (should take 528-660ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32 + retryBoosterTimeout, [&] { return loopback.mSentMessageCount >= 4; });
    now         = System::SystemClock().GetMonotonicTimestamp();
    timeoutTime = now - startTime;
    ChipLogProgress(Test, "Attempt #3  Timeout : %" PRIu32 "ms", timeoutTime.count());
    expectedBackoff = &theBackoffComplianceTestVector[2];
    EXPECT_TRUE(timeoutTime >= expectedBackoff->backoffMin - margin);

    startTime = System::SystemClock().GetMonotonicTimestamp();
    ctx.DrainAndServiceIO();

    // Ensure the 3rd retry was dropped, and is still there in the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount == 4);
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 0);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 4);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Wait for the 3rd retry to fail (should take 845-1056ms)
    ctx.GetIOContext().DriveIOUntil(1500_ms32 + retryBoosterTimeout, [&] { return loopback.mSentMessageCount >= 5; });
    now         = System::SystemClock().GetMonotonicTimestamp();
    timeoutTime = now - startTime;
    ChipLogProgress(Test, "Attempt #4  Timeout : %" PRIu32 "ms", timeoutTime.count());
    expectedBackoff = &theBackoffComplianceTestVector[3];
    EXPECT_TRUE(timeoutTime >= expectedBackoff->backoffMin - margin);

    // Trigger final transmission
    ctx.DrainAndServiceIO();

    // Ensure the last retransmission was NOT dropped, and the retransmit table is empty, as we should have gotten an ack
    EXPECT_TRUE(loopback.mSentMessageCount >= 5);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 4);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    exchange->Close();
}

TEST_F(TestReliableMessageProtocol, CheckCloseExchangeAndResendApplicationMessage)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockSender(ctx);
    // TODO: temporarily create a SessionHandle from node id, will be fixed in PR 3602
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
    });

    // Let's drop the initial message
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 2;
    loopback.mDroppedMessageCount = 0;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was dropped, and was added to retransmit table
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Wait for the first re-transmit (should take 64ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 2; });
    ctx.DrainAndServiceIO();

    // Ensure the retransmit message was dropped, and is still there in the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 0);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 2);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Wait for the second re-transmit (should take 64ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 3; });
    ctx.DrainAndServiceIO();

    // Ensure the retransmit message was NOT dropped, and the retransmit table is empty, as we should have gotten an ack
    EXPECT_TRUE(loopback.mSentMessageCount >= 3);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 2);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckFailedMessageRetainOnSend)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockSessionEstablishmentDelegate mockSender;
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
    });

    mockSender.mMessageDispatch.mRetainMessageOnSend = false;
    // Let's drop the initial message
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 1;
    loopback.mDroppedMessageCount = 0;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was dropped
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);

    // Wait for the first re-transmit (should take 64ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 2; });
    ctx.DrainAndServiceIO();

    // Ensure the retransmit table is empty, as we did not provide a message to retain
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckUnencryptedMessageReceiveFailure)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    MockSessionEstablishmentDelegate mockReceiver;
    CHIP_ERROR err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Expect the received messages to be encrypted
    mockReceiver.mMessageDispatch.mRequireEncryption = true;

    MockSessionEstablishmentDelegate mockSender;
    ExchangeContext * exchange = ctx.NewUnauthenticatedExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;

    // We are sending a malicious packet, doesn't expect an ack
    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendFlags(SendMessageFlags::kNoAutoRequestAck));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Test that the message was actually sent (and not dropped)
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
    // Test that the message was dropped by the receiver
    EXPECT_TRUE(!mockReceiver.IsOnMessageReceivedCalled);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckResendApplicationMessageWithPeerExchange)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
    });

    // Let's drop the initial message
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 1;
    loopback.mDroppedMessageCount = 0;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was dropped, and was added to retransmit table
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 0);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);
    EXPECT_TRUE(!mockReceiver.IsOnMessageReceivedCalled);

    // Wait for the first re-transmit (should take 64ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 2; });
    ctx.DrainAndServiceIO();

    // Ensure the retransmit message was not dropped, and is no longer in the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount >= 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);

    mockReceiver.mTestSuite = nullptr;

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

TEST_F(TestReliableMessageProtocol, CheckDuplicateMessageClosedExchange)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_RMP_DEFAULT_INITIAL_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_RMP_DEFAULT_ACTIVE_RETRY_INTERVAL
    });

    // Let's not drop the message. Expectation is that it is received by the peer, but the ack is dropped
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;

    // Drop the ack, and also close the peer exchange
    mockReceiver.SetDropAckResponse(true);
    mockReceiver.mRetainExchange = false;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent
    // The ack was dropped, and message was added to the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Let's not drop the duplicate message
    mockReceiver.SetDropAckResponse(false);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Wait for the first re-transmit and ack (should take 64ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 3; });
    ctx.DrainAndServiceIO();

    // Ensure the retransmit message was sent and the ack was sent
    // and retransmit table was cleared
    EXPECT_TRUE(loopback.mSentMessageCount == 3);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckDuplicateOldMessageClosedExchange)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_RMP_DEFAULT_INITIAL_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_RMP_DEFAULT_ACTIVE_RETRY_INTERVAL
    });

    // Let's not drop the message. Expectation is that it is received by the peer, but the ack is dropped
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;

    // Drop the ack, and also close the peer exchange
    mockReceiver.SetDropAckResponse(true);
    mockReceiver.mRetainExchange = false;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent
    // The ack was dropped, and message was added to the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Now send CHIP_CONFIG_MESSAGE_COUNTER_WINDOW_SIZE + 2 messages to make
    // sure our original message is out of the message counter window.  These
    // messages can be sent withour MRP, because we are not expecting acks for
    // them anyway.
    size_t extraMessages = CHIP_CONFIG_MESSAGE_COUNTER_WINDOW_SIZE + 2;
    for (size_t i = 0; i < extraMessages; ++i)
    {
        buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
        EXPECT_TRUE(!buffer.IsNull());

        ExchangeContext * newExchange = ctx.NewExchangeToAlice(&mockSender);
        EXPECT_TRUE(newExchange != nullptr);

        mockReceiver.mRetainExchange = false;

        // Ensure the retransmit table has our one message right now
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

        // Send without MRP.
        err = newExchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendMessageFlags::kNoAutoRequestAck);
        EXPECT_TRUE(err == CHIP_NO_ERROR);
        ctx.DrainAndServiceIO();

        // Ensure the message was sent, but not added to the retransmit table.
        EXPECT_TRUE(loopback.mSentMessageCount == 1 + (i + 1));
        EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);
    }

    // Let's not drop the duplicate message's ack.
    mockReceiver.SetDropAckResponse(false);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Wait for the first re-transmit and ack (should take 64ms)
    rm->StartTimer();
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 3 + extraMessages; });
    ctx.DrainAndServiceIO();

    // Ensure the retransmit message was sent and the ack was sent
    // and retransmit table was cleared
    EXPECT_TRUE(loopback.mSentMessageCount == 3 + extraMessages);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckResendSessionEstablishmentMessageWithPeerExchange)
{
    TestContext & inctx = TestReliableMessageProtocol::ctx;

    // Making this static to reduce stack usage, as some platforms have limits on stack size.
    static TestContext ctx2;

    CHIP_ERROR err = ctx2.InitFromExisting(inctx);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    MockSessionEstablishmentDelegate mockReceiver;
    err = ctx2.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockSessionEstablishmentDelegate mockSender;
    ExchangeContext * exchange = ctx2.NewUnauthenticatedExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx2.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsUnauthenticatedSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
    });

    // Let's drop the initial message
    auto & loopback               = inctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 1;
    loopback.mDroppedMessageCount = 0;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    inctx.DrainAndServiceIO();

    // Ensure the message was dropped, and was added to retransmit table
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 0);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);
    EXPECT_TRUE(!mockReceiver.IsOnMessageReceivedCalled);

    // Wait for the first re-transmit (should take 64ms)
    inctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 2; });
    inctx.DrainAndServiceIO();

    // Ensure the retransmit message was not dropped, and is no longer in the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount >= 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);

    mockReceiver.mTestSuite = nullptr;

    err = ctx2.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    ctx2.ShutdownAndRestoreExisting(inctx);
}

TEST_F(TestReliableMessageProtocol, CheckDuplicateMessage)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_RMP_DEFAULT_INITIAL_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_RMP_DEFAULT_ACTIVE_RETRY_INTERVAL
    });

    // Let's not drop the message. Expectation is that it is received by the peer, but the ack is dropped
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;

    // Drop the ack, and keep the exchange around to receive the duplicate message
    mockReceiver.SetDropAckResponse(true);
    mockReceiver.mRetainExchange = true;

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent
    // The ack was dropped, and message was added to the retransmit table
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Let's not drop the duplicate message
    mockReceiver.SetDropAckResponse(false);
    mockReceiver.mRetainExchange = false;

    // Wait for the first re-transmit and ack (should take 64ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 3; });
    ctx.DrainAndServiceIO();

    // Ensure the retransmit message was sent and the ack was sent
    // and retransmit table was cleared
    EXPECT_TRUE(loopback.mSentMessageCount == 3);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    mockReceiver.CloseExchangeIfNeeded();
}

TEST_F(TestReliableMessageProtocol, CheckReceiveAfterStandaloneAck)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // We send a message, have it get received by the peer, then an ack is
    // returned, then a reply is returned.  We need to keep the receiver
    // exchange alive until it does the message send (so we can send the
    // response from the receiver and so the initial sender exchange can get
    // it).
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;
    mockReceiver.mRetainExchange  = true;

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendFlags(SendMessageFlags::kExpectResponse));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was received.
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);

    // And that we have not seen an ack yet.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    ReliableMessageContext * receiverRc = mockReceiver.mExchange->GetReliableMessageContext();
    EXPECT_TRUE(receiverRc->IsAckPending());

    // Send the standalone ack.
    receiverRc->SendStandaloneAckMessage();
    ctx.DrainAndServiceIO();

    // Ensure the ack was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // Ensure that we have not gotten any app-level responses so far.
    EXPECT_TRUE(!mockSender.IsOnMessageReceivedCalled);

    // And that we have now gotten our ack.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // Now send a message from the other side.
    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err = mockReceiver.mExchange->SendMessage(Echo::MsgType::EchoResponse, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the response and its ack was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 4);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // Ensure that we have received that response.
    EXPECT_TRUE(mockSender.IsOnMessageReceivedCalled);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckPiggybackAfterPiggyback)
{
    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // We send a message, have it get received by the peer, have the peer return
    // a piggybacked ack.  Then we send a second message this time _not_
    // requesting an ack, get a response, and see whether an ack was
    // piggybacked.  We need to keep both exchanges alive for that (so we can
    // send the response from the receiver and so the initial sender exchange
    // can get it).
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;
    mockReceiver.mRetainExchange  = true;
    mockSender.mRetainExchange    = true;

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendFlags(SendMessageFlags::kExpectResponse));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was received.
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);

    // And that we have not seen an ack yet.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    ReliableMessageContext * receiverRc = mockReceiver.mExchange->GetReliableMessageContext();
    EXPECT_TRUE(receiverRc->IsAckPending());

    // Ensure that we have not gotten any app-level responses or acks so far.
    EXPECT_TRUE(!mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(!mockSender.mReceivedPiggybackAck);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Now send a message from the other side.
    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err =
        mockReceiver.mExchange->SendMessage(Echo::MsgType::EchoResponse, std::move(buffer),
                                            SendFlags(SendMessageFlags::kExpectResponse).Set(SendMessageFlags::kNoAutoRequestAck));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the response was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // Ensure that we have received that response and it had a piggyback ack.
    EXPECT_TRUE(mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(mockSender.mReceivedPiggybackAck);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // Reset various state so we can measure things again.
    mockReceiver.IsOnMessageReceivedCalled = false;
    mockSender.IsOnMessageReceivedCalled   = false;
    mockSender.mReceivedPiggybackAck       = false;

    // Now send a new message to the other side, but don't ask for an ack.
    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer),
                                SendFlags(SendMessageFlags::kExpectResponse).Set(SendMessageFlags::kNoAutoRequestAck));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 3);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was received.
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);

    // And that we are not expecting an ack.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // Send the final response.  At this point we don't need to keep the
    // exchanges alive anymore.
    mockReceiver.mRetainExchange = false;
    mockSender.mRetainExchange   = false;

    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err = mockReceiver.mExchange->SendMessage(Echo::MsgType::EchoResponse, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the response and its ack was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 5);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // Ensure that we have received that response and it had a piggyback ack.
    EXPECT_TRUE(mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(mockSender.mReceivedPiggybackAck);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckSendUnsolicitedStandaloneAckMessage)
{
    /**
     * Tests sending a standalone ack message that is:
     * 1) Unsolicited.
     * 2) Requests an ack.
     *
     * This is not a thing that would normally happen, but a malicious entity
     * could absolutely do this.
     */

    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData("", 0);
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // We send a message, have it get received by the peer, expect an ack from
    // the peer.
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;

    // Purposefully sending a standalone ack that requests an ack!
    err = exchange->SendMessage(SecureChannel::MsgType::StandaloneAck, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    // Needs a manual close, because SendMessage does not close for standalone acks.
    exchange->Close();
    ctx.DrainAndServiceIO();

    // Ensure the message and its ack were sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that nothing is waiting for acks.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckSendStandaloneAckMessage)
{
    MockAppDelegate mockAppDelegate(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockAppDelegate);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm     = ctx.GetExchangeManager().GetReliableMessageMgr();
    ReliableMessageContext * rc = exchange->GetReliableMessageContext();
    EXPECT_TRUE(rm != nullptr);
    EXPECT_TRUE(rc != nullptr);

    EXPECT_TRUE(rc->SendStandaloneAckMessage() == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Need manual close because standalone acks don't close exchanges.
    exchange->Close();
}

TEST_F(TestReliableMessageProtocol, CheckMessageAfterClosed)
{
    /**
     * This test performs the following sequence of actions, where all messages
     * are sent with MRP enabled:
     *
     * 1) Initiator sends message to responder.
     * 2) Responder responds to the message (piggybacking an ack) and closes
     *    the exchange.
     * 3) Initiator sends a response to the response on the same exchange, again
     *    piggybacking an ack.
     *
     * This is basically the "command, response, status response" flow, with the
     * responder closing the exchange after it sends the response.
     */

    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;
    // We need to keep both exchanges alive for the thing we are testing here.
    mockReceiver.mRetainExchange = true;
    mockSender.mRetainExchange   = true;

    EXPECT_TRUE(!mockReceiver.IsOnMessageReceivedCalled);
    EXPECT_TRUE(!mockReceiver.mReceivedPiggybackAck);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendFlags(SendMessageFlags::kExpectResponse));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was received.
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);
    EXPECT_TRUE(!mockReceiver.mReceivedPiggybackAck);

    // And that we have not seen an ack yet.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    ReliableMessageContext * receiverRc = mockReceiver.mExchange->GetReliableMessageContext();
    EXPECT_TRUE(receiverRc->IsAckPending());

    // Ensure that we have not gotten any app-level responses or acks so far.
    EXPECT_TRUE(!mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(!mockSender.mReceivedPiggybackAck);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Now send a message from the other side.
    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err = mockReceiver.mExchange->SendMessage(Echo::MsgType::EchoResponse, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the response was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // Ensure that we have received that response and it had a piggyback ack.
    EXPECT_TRUE(mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(mockSender.mReceivedPiggybackAck);
    // And that we are now waiting for an ack for the response.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // Reset various state so we can measure things again.
    mockReceiver.IsOnMessageReceivedCalled = false;
    mockReceiver.mReceivedPiggybackAck     = false;
    mockSender.IsOnMessageReceivedCalled   = false;
    mockSender.mReceivedPiggybackAck       = false;

    // Now send a second message to the other side.
    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent (and the ack for it was also sent).
    EXPECT_TRUE(loopback.mSentMessageCount == 4);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was not received (because the exchange is closed on the
    // receiver).
    EXPECT_TRUE(!mockReceiver.IsOnMessageReceivedCalled);

    // And that we are not expecting an ack; acks should have been flushed
    // immediately on the receiver, due to the exchange being closed.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    err = ctx.GetExchangeManager().UnregisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckLostResponseWithPiggyback)
{
    /**
     * This tests the following scenario:
     * 1) A reliable message is sent from initiator to responder.
     * 2) The responder sends a response with a piggybacked ack, which is lost.
     * 3) Initiator resends the message.
     * 4) Responder responds to the resent message with a standalone ack.
     * 5) The responder retransmits the application-level response.
     * 4) The initiator should receive the application-level response.
     */

    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // Make sure that we resend our message before the other side does.
    exchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
        64_ms32, // CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
    });

    // We send a message, the other side sends an application-level response
    // (which is lost), then we do a retransmit that is acked, then the other
    // side does a retransmit.  We need to keep the receiver exchange alive (so
    // we can send the response from the receiver), but don't need anything
    // special for the sender exchange, because it will be waiting for the
    // application-level response.
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;
    mockReceiver.mRetainExchange  = true;

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendFlags(SendMessageFlags::kExpectResponse));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was received.
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);

    // And that we have not gotten any app-level responses or acks so far.
    EXPECT_TRUE(!mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    ReliableMessageContext * receiverRc = mockReceiver.mExchange->GetReliableMessageContext();
    // Should have pending ack here.
    EXPECT_TRUE(receiverRc->IsAckPending());
    // Make sure receiver resends after sender does, and there's enough of a gap
    // that we are very unlikely to actually trigger the resends on the receiver
    // when we trigger the resends on the sender.
    mockReceiver.mExchange->GetSessionHandle()->AsSecureSession()->SetRemoteMRPConfig({
        256_ms32, // CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
        256_ms32, // CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
    });

    // Now send a message from the other side, but drop it.
    loopback.mNumMessagesToDrop = 1;

    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    // Stop keeping receiver exchange alive.
    mockReceiver.mRetainExchange = true;

    err = mockReceiver.mExchange->SendMessage(Echo::MsgType::EchoResponse, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the response was sent but dropped.
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mNumMessagesToDrop == 0);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);

    // Ensure that we have not received that response.
    EXPECT_TRUE(!mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(!mockSender.mReceivedPiggybackAck);
    // We now have our un-acked message still waiting to retransmit and the
    // message that the other side sent is waiting for an ack.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 2);

    // Reset various state so we can measure things again.
    mockReceiver.IsOnMessageReceivedCalled = false;
    mockReceiver.mReceivedPiggybackAck     = false;
    mockSender.IsOnMessageReceivedCalled   = false;
    mockSender.mReceivedPiggybackAck       = false;

    // Wait for re-transmit from sender and ack (should take 64ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 4; });
    ctx.DrainAndServiceIO();

    // We resent our first message, which did not make it to the app-level
    // listener on the receiver (because it's a duplicate) but did trigger a
    // standalone ack.
    //
    // Now the annoying part is that depending on how long we _actually_ slept
    // we might have also triggered the retransmit from the other side, even
    // though we did not want to.  Handle both cases here.
    EXPECT_TRUE(loopback.mSentMessageCount == 4 || loopback.mSentMessageCount == 6);
    if (loopback.mSentMessageCount == 4)
    {
        // Just triggered the retransmit from the sender.
        EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
        EXPECT_TRUE(!mockSender.IsOnMessageReceivedCalled);
        EXPECT_TRUE(!mockReceiver.IsOnMessageReceivedCalled);
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);
    }
    else
    {
        // Also triggered the retransmit from the receiver.
        EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
        EXPECT_TRUE(mockSender.IsOnMessageReceivedCalled);
        EXPECT_TRUE(!mockReceiver.IsOnMessageReceivedCalled);
        EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
    }

    // Wait for re-transmit from receiver (should take 256ms)
    ctx.GetIOContext().DriveIOUntil(1000_ms32, [&] { return loopback.mSentMessageCount >= 6; });
    ctx.DrainAndServiceIO();

    // And now we've definitely resent our response message, which should show
    // up as an app-level message and trigger a standalone ack.
    EXPECT_TRUE(loopback.mSentMessageCount == 6);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 1);
    EXPECT_TRUE(mockSender.IsOnMessageReceivedCalled);

    // Should be all done now.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckLostStandaloneAck)
{
    /**
     * This tests the following scenario:
     * 1) A reliable message is sent from initiator to responder.
     * 2) The responder sends a standalone ack, which is lost.
     * 3) The responder sends an application-level response.
     * 4) The initiator sends a reliable response to the app-level response.
     *
     * This should succeed, with all application-level messages being delivered
     * and no crashes.
     */

    chip::System::PacketBufferHandle buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    CHIP_ERROR err = CHIP_NO_ERROR;

    MockAppDelegate mockReceiver(ctx);
    err = ctx.GetExchangeManager().RegisterUnsolicitedMessageHandlerForType(Echo::MsgType::EchoRequest, &mockReceiver);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    MockAppDelegate mockSender(ctx);
    ExchangeContext * exchange = ctx.NewExchangeToAlice(&mockSender);
    EXPECT_TRUE(exchange != nullptr);

    ReliableMessageMgr * rm = ctx.GetExchangeManager().GetReliableMessageMgr();
    EXPECT_TRUE(rm != nullptr);

    // Ensure the retransmit table is empty right now
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);

    // We send a message, the other side sends a standalone ack first (which is
    // lost), then an application response, then we respond to that response.
    // We need to keep both exchanges alive for that (so we can send the
    // response from the receiver and so the initial sender exchange can send a
    // response to that).
    auto & loopback               = ctx.GetLoopback();
    loopback.mSentMessageCount    = 0;
    loopback.mNumMessagesToDrop   = 0;
    loopback.mDroppedMessageCount = 0;
    mockReceiver.mRetainExchange  = true;
    mockSender.mRetainExchange    = true;

    // And ensure the ack heading back our way is dropped.
    mockReceiver.SetDropAckResponse(true);

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer), SendFlags(SendMessageFlags::kExpectResponse));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 1);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was received.
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);

    // And that we have not gotten any app-level responses or acks so far.
    EXPECT_TRUE(!mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    ReliableMessageContext * receiverRc = mockReceiver.mExchange->GetReliableMessageContext();
    // Ack should have been dropped.
    EXPECT_TRUE(!receiverRc->IsAckPending());

    // Don't drop any more acks.
    mockReceiver.SetDropAckResponse(false);

    // Now send a message from the other side.
    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err = mockReceiver.mExchange->SendMessage(Echo::MsgType::EchoResponse, std::move(buffer),
                                              SendFlags(SendMessageFlags::kExpectResponse));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the response was sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 2);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // Ensure that we have received that response and had a piggyback ack.
    EXPECT_TRUE(mockSender.IsOnMessageReceivedCalled);
    EXPECT_TRUE(mockSender.mReceivedPiggybackAck);
    // We now have just the received message waiting for an ack.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 1);

    // And receiver still has no ack pending.
    EXPECT_TRUE(!receiverRc->IsAckPending());

    // Reset various state so we can measure things again.
    mockReceiver.IsOnMessageReceivedCalled = false;
    mockReceiver.mReceivedPiggybackAck     = false;
    mockSender.IsOnMessageReceivedCalled   = false;
    mockSender.mReceivedPiggybackAck       = false;

    // Stop retaining the recipient exchange.
    mockReceiver.mRetainExchange = false;

    // Now send a new message to the other side.
    buffer = chip::MessagePacketBuffer::NewWithData(PAYLOAD, sizeof(PAYLOAD));
    EXPECT_TRUE(!buffer.IsNull());

    err = exchange->SendMessage(Echo::MsgType::EchoRequest, std::move(buffer));
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    ctx.DrainAndServiceIO();

    // Ensure the message and the standalone ack to it were sent.
    EXPECT_TRUE(loopback.mSentMessageCount == 4);
    EXPECT_TRUE(loopback.mDroppedMessageCount == 0);

    // And that it was received.
    EXPECT_TRUE(mockReceiver.IsOnMessageReceivedCalled);
    EXPECT_TRUE(mockReceiver.mReceivedPiggybackAck);

    // At this point all our exchanges and reliable message contexts should be
    // dead, so we can't test anything about their state.

    // And that there are no un-acked messages left.
    EXPECT_TRUE(rm->TestGetCountRetransTable() == 0);
}

TEST_F(TestReliableMessageProtocol, CheckGetBackoff)
{
    // Run 3x iterations to thoroughly test random jitter always results in backoff within bounds.
    for (uint32_t j = 0; j < 3; j++)
    {
        for (const auto & test : theBackoffComplianceTestVector)
        {
            System::Clock::Timeout backoff = ReliableMessageMgr::GetBackoff(test.backoffBase, test.sendCount);
            ChipLogProgress(Test, "Backoff base %" PRIu32 " # %d: %" PRIu32, test.backoffBase.count(), test.sendCount,
                            backoff.count());

            EXPECT_TRUE(backoff >= test.backoffMin);
            EXPECT_TRUE(backoff <= test.backoffMax + retryBoosterTimeout);
        }
    }
}

/**
 * TODO: A test that we should have but can't write with the existing
 * infrastructure we have:
 *
 * 1. A sends message 1 to B
 * 2. B is slow to respond, A does a resend and the resend is delayed in the network.
 * 3. B responds with message 2, which acks message 1.
 * 4. A sends message 3 to B
 * 5. B sends standalone ack to message 3, which is lost
 * 6. The duplicate message from step 3 is delivered and triggers a standalone ack.
 * 7. B responds with message 4, which should carry a piggyback ack for message 3
 *    (this is the part that needs testing!)
 * 8. A sends message 5 to B.
 */

// nlTestSuite sSuite = {
//     "Test-CHIP-ReliableMessageProtocol",
//     &sTests[0],
//     TestContext::Initialize,
//     TestContext::Finalize,
//     InitializeTestCase,
// };

} // namespace
