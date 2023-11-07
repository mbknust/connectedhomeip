#include <protocols/user_directed_commissioning/UserDirectedCommissioning.h>

#include <gtest/gtest.h>

#include <lib/core/CHIPSafeCasts.h>
#include <lib/dnssd/TxtFields.h>
#include <lib/support/BufferWriter.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/UnitTestContext.h>

#include <transport/TransportMgr.h>
#include <transport/raw/MessageHeader.h>
#include <transport/raw/UDP.h>

#include <limits>

using namespace chip;
using namespace chip::Protocols::UserDirectedCommissioning;
using namespace chip::Dnssd;
using namespace chip::Dnssd::Internal;

ByteSpan GetSpan(char * key)
{
    size_t len = strlen(key);
    // Stop the string from being null terminated to ensure the code makes no assumptions.
    key[len] = '1';
    return ByteSpan(Uint8::from_char(key), len);
}
class DLL_EXPORT TestCallback : public UserConfirmationProvider, public InstanceNameResolver
{
public:
    void OnUserDirectedCommissioningRequest(UDCClientState state)
    {
        mOnUserDirectedCommissioningRequestCalled = true;
        mState                                    = state;
    }

    void FindCommissionableNode(char * instanceName)
    {
        mFindCommissionableNodeCalled = true;
        mInstanceName                 = instanceName;
    }

    // virtual ~UserConfirmationProvider() = default;
    UDCClientState mState;
    char * mInstanceName;

    bool mOnUserDirectedCommissioningRequestCalled = false;
    bool mFindCommissionableNodeCalled             = false;
};

using DeviceTransportMgr = TransportMgr<Transport::UDP>;

class TestUdcMessages : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

TEST_F(TestUdcMessages, TestUDCServerClients)
{
    UserDirectedCommissioningServer udcServer;
    const char * instanceName1 = "servertest1";

    // test setting UDC Clients
    EXPECT_TRUE(nullptr == udcServer.GetUDCClients().FindUDCClientState(instanceName1));
    udcServer.SetUDCClientProcessingState((char *) instanceName1, UDCClientProcessingState::kUserDeclined);
    UDCClientState * state = udcServer.GetUDCClients().FindUDCClientState(instanceName1);
    ASSERT_TRUE(nullptr != state);
    EXPECT_TRUE(UDCClientProcessingState::kUserDeclined == state->GetUDCClientProcessingState());
}

TEST_F(TestUdcMessages, TestUDCServerUserConfirmationProvider)
{
    UserDirectedCommissioningServer udcServer;
    TestCallback testCallback;
    const char * instanceName1 = "servertest1";
    const char * instanceName2 = "servertest2";
    const char * deviceName2   = "device1";
    uint16_t disc2             = 1234;
    UDCClientState * state;

    chip::Inet::IPAddress address;
    chip::Inet::IPAddress::FromString("127.0.0.1", address); // need to populate with something

    // setup for tests
    udcServer.SetUDCClientProcessingState((char *) instanceName1, UDCClientProcessingState::kUserDeclined);

    Dnssd::DiscoveredNodeData nodeData1;
    nodeData1.resolutionData.port         = 5540;
    nodeData1.resolutionData.ipAddress[0] = address;
    nodeData1.resolutionData.numIPs       = 1;
    Platform::CopyString(nodeData1.commissionData.instanceName, instanceName1);

    Dnssd::DiscoveredNodeData nodeData2;
    nodeData2.resolutionData.port              = 5540;
    nodeData2.resolutionData.ipAddress[0]      = address;
    nodeData2.resolutionData.numIPs            = 1;
    nodeData2.commissionData.longDiscriminator = disc2;
    Platform::CopyString(nodeData2.commissionData.instanceName, instanceName2);
    Platform::CopyString(nodeData2.commissionData.deviceName, deviceName2);

    // test empty UserConfirmationProvider
    udcServer.OnCommissionableNodeFound(nodeData2);
    udcServer.OnCommissionableNodeFound(nodeData1);
    state = udcServer.GetUDCClients().FindUDCClientState(instanceName1);
    ASSERT_TRUE(nullptr != state);
    EXPECT_TRUE(UDCClientProcessingState::kUserDeclined == state->GetUDCClientProcessingState());
    // test other fields on UDCClientState
    EXPECT_TRUE(0 == strcmp(state->GetInstanceName(), instanceName1));
    // check that instance2 was found
    state = udcServer.GetUDCClients().FindUDCClientState(instanceName2);
    EXPECT_TRUE(nullptr == state);

    // test current state check
    udcServer.SetUDCClientProcessingState((char *) instanceName1, UDCClientProcessingState::kUserDeclined);
    udcServer.SetUDCClientProcessingState((char *) instanceName2, UDCClientProcessingState::kDiscoveringNode);
    udcServer.OnCommissionableNodeFound(nodeData2);
    udcServer.OnCommissionableNodeFound(nodeData1);
    state = udcServer.GetUDCClients().FindUDCClientState(instanceName1);
    ASSERT_TRUE(nullptr != state);
    EXPECT_TRUE(UDCClientProcessingState::kUserDeclined == state->GetUDCClientProcessingState());
    state = udcServer.GetUDCClients().FindUDCClientState(instanceName2);
    ASSERT_TRUE(nullptr != state);
    EXPECT_TRUE(UDCClientProcessingState::kPromptingUser == state->GetUDCClientProcessingState());
    // test other fields on UDCClientState
    EXPECT_TRUE(0 == strcmp(state->GetInstanceName(), instanceName2));
    EXPECT_TRUE(0 == strcmp(state->GetDeviceName(), deviceName2));
    EXPECT_TRUE(state->GetLongDiscriminator() == disc2);

    // test non-empty UserConfirmationProvider
    udcServer.SetUserConfirmationProvider(&testCallback);
    udcServer.SetUDCClientProcessingState((char *) instanceName1, UDCClientProcessingState::kUserDeclined);
    udcServer.SetUDCClientProcessingState((char *) instanceName2, UDCClientProcessingState::kDiscoveringNode);
    udcServer.OnCommissionableNodeFound(nodeData1);
    EXPECT_TRUE(!testCallback.mOnUserDirectedCommissioningRequestCalled);
    udcServer.OnCommissionableNodeFound(nodeData2);
    EXPECT_TRUE(testCallback.mOnUserDirectedCommissioningRequestCalled);
    EXPECT_TRUE(0 == strcmp(testCallback.mState.GetInstanceName(), instanceName2));
}

TEST_F(TestUdcMessages, TestUDCServerInstanceNameResolver)
{
    UserDirectedCommissioningServer udcServer;
    UserDirectedCommissioningClient udcClient;
    TestCallback testCallback;
    UDCClientState * state;
    const char * instanceName1 = "servertest1";

    // setup for tests
    auto mUdcTransportMgr = chip::Platform::MakeUnique<DeviceTransportMgr>();
    mUdcTransportMgr->SetSessionManager(&udcServer);
    udcServer.SetInstanceNameResolver(&testCallback);

    // set state for instance1
    udcServer.SetUDCClientProcessingState((char *) instanceName1, UDCClientProcessingState::kUserDeclined);

    // encode our client message
    char nameBuffer[Dnssd::Commission::kInstanceNameMaxLength + 1] = "Chris";
    System::PacketBufferHandle payloadBuf = MessagePacketBuffer::NewWithData(nameBuffer, strlen(nameBuffer));
    udcClient.EncodeUDCMessage(payloadBuf);

    // prepare peerAddress for handleMessage
    Inet::IPAddress commissioner;
    Inet::IPAddress::FromString("127.0.0.1", commissioner);
    uint16_t port                      = 11100;
    Transport::PeerAddress peerAddress = Transport::PeerAddress::UDP(commissioner, port);

    // test OnMessageReceived
    mUdcTransportMgr->HandleMessageReceived(peerAddress, std::move(payloadBuf));

    // check if the state is set for the instance name sent
    state = udcServer.GetUDCClients().FindUDCClientState(nameBuffer);
    ASSERT_TRUE(nullptr != state);
    EXPECT_TRUE(UDCClientProcessingState::kDiscoveringNode == state->GetUDCClientProcessingState());

    // check if a callback happened
    EXPECT_TRUE(testCallback.mFindCommissionableNodeCalled);

    // reset callback tracker so we can confirm that when the
    // same instance name is received, there is no callback
    testCallback.mFindCommissionableNodeCalled = false;

    payloadBuf = MessagePacketBuffer::NewWithData(nameBuffer, strlen(nameBuffer));

    // reset the UDC message
    udcClient.EncodeUDCMessage(payloadBuf);

    // test OnMessageReceived again
    mUdcTransportMgr->HandleMessageReceived(peerAddress, std::move(payloadBuf));

    // verify it was not called
    EXPECT_TRUE(!testCallback.mFindCommissionableNodeCalled);

    // next, reset the cache state and confirm the callback
    udcServer.ResetUDCClientProcessingStates();

    payloadBuf = MessagePacketBuffer::NewWithData(nameBuffer, strlen(nameBuffer));

    // reset the UDC message
    udcClient.EncodeUDCMessage(payloadBuf);

    // test OnMessageReceived again
    mUdcTransportMgr->HandleMessageReceived(peerAddress, std::move(payloadBuf));

    // verify it was called
    EXPECT_TRUE(testCallback.mFindCommissionableNodeCalled);
}

TEST_F(TestUdcMessages, TestUserDirectedCommissioningClientMessage)
{
    char nameBuffer[Dnssd::Commission::kInstanceNameMaxLength + 1] = "Chris";
    System::PacketBufferHandle payloadBuf = MessagePacketBuffer::NewWithData(nameBuffer, strlen(nameBuffer));
    UserDirectedCommissioningClient udcClient;

    // obtain the UDC message
    CHIP_ERROR err = udcClient.EncodeUDCMessage(payloadBuf);

    // check the packet header fields
    PacketHeader packetHeader;
    packetHeader.DecodeAndConsume(payloadBuf);
    EXPECT_TRUE(!packetHeader.IsEncrypted());

    // check the payload header fields
    PayloadHeader payloadHeader;
    payloadHeader.DecodeAndConsume(payloadBuf);
    EXPECT_TRUE(payloadHeader.GetMessageType() == to_underlying(MsgType::IdentificationDeclaration));
    EXPECT_TRUE(payloadHeader.GetProtocolID() == Protocols::UserDirectedCommissioning::Id);
    EXPECT_TRUE(!payloadHeader.NeedsAck());
    EXPECT_TRUE(payloadHeader.IsInitiator());

    // check the payload
    char instanceName[Dnssd::Commission::kInstanceNameMaxLength + 1];
    size_t instanceNameLength = std::min<size_t>(payloadBuf->DataLength(), Dnssd::Commission::kInstanceNameMaxLength);
    payloadBuf->Read(Uint8::from_char(instanceName), instanceNameLength);
    instanceName[instanceNameLength] = '\0';
    ChipLogProgress(Inet, "UDC instance=%s", instanceName);
    EXPECT_TRUE(strcmp(instanceName, nameBuffer) == 0);

    // verify no errors
    EXPECT_TRUE(err == CHIP_NO_ERROR);
}

TEST_F(TestUdcMessages, TestUDCClients)
{
    UDCClients<3> mUdcClients;
    const char * instanceName1 = "test1";
    const char * instanceName2 = "test2";
    const char * instanceName3 = "test3";
    const char * instanceName4 = "test4";

    // test base case
    UDCClientState * state = mUdcClients.FindUDCClientState(instanceName1);
    EXPECT_TRUE(state == nullptr);

    // test max size
    EXPECT_TRUE(CHIP_NO_ERROR == mUdcClients.CreateNewUDCClientState(instanceName1, &state));
    EXPECT_TRUE(CHIP_NO_ERROR == mUdcClients.CreateNewUDCClientState(instanceName2, &state));
    EXPECT_TRUE(CHIP_NO_ERROR == mUdcClients.CreateNewUDCClientState(instanceName3, &state));
    EXPECT_TRUE(CHIP_ERROR_NO_MEMORY == mUdcClients.CreateNewUDCClientState(instanceName4, &state));

    // test reset
    mUdcClients.ResetUDCClientStates();
    EXPECT_TRUE(CHIP_NO_ERROR == mUdcClients.CreateNewUDCClientState(instanceName4, &state));

    // test find
    EXPECT_TRUE(nullptr == mUdcClients.FindUDCClientState(instanceName1));
    EXPECT_TRUE(nullptr == mUdcClients.FindUDCClientState(instanceName2));
    EXPECT_TRUE(nullptr == mUdcClients.FindUDCClientState(instanceName3));
    state = mUdcClients.FindUDCClientState(instanceName4);
    EXPECT_TRUE(nullptr != state);

    // test expiry
    state->Reset();
    EXPECT_TRUE(nullptr == mUdcClients.FindUDCClientState(instanceName4));

    // test re-activation
    EXPECT_TRUE(CHIP_NO_ERROR == mUdcClients.CreateNewUDCClientState(instanceName4, &state));
    System::Clock::Timestamp expirationTime = state->GetExpirationTime();
    state->SetExpirationTime(expirationTime - System::Clock::Milliseconds64(1));
    EXPECT_TRUE((expirationTime - System::Clock::Milliseconds64(1)) == state->GetExpirationTime());
    mUdcClients.MarkUDCClientActive(state);
    EXPECT_TRUE((expirationTime - System::Clock::Milliseconds64(1)) < state->GetExpirationTime());
}

TEST_F(TestUdcMessages, TestUDCClientState)
{
    UDCClients<3> mUdcClients;
    const char * instanceName1 = "test1";
    Inet::IPAddress address;
    Inet::IPAddress::FromString("127.0.0.1", address);
    uint16_t port              = 333;
    uint16_t longDiscriminator = 1234;
    uint16_t vendorId          = 1111;
    uint16_t productId         = 2222;
    const char * deviceName    = "test name";

    // Rotating ID is given as up to 50 hex bytes
    char rotatingIdString[chip::Dnssd::kMaxRotatingIdLen * 2 + 1];
    uint8_t rotatingId[chip::Dnssd::kMaxRotatingIdLen];
    size_t rotatingIdLen;
    strcpy(rotatingIdString, "92873498273948734534");
    GetRotatingDeviceId(GetSpan(rotatingIdString), rotatingId, &rotatingIdLen);

    // create a Rotating ID longer than kMaxRotatingIdLen
    char rotatingIdLongString[chip::Dnssd::kMaxRotatingIdLen * 4 + 1];
    uint8_t rotatingIdLong[chip::Dnssd::kMaxRotatingIdLen * 2];
    size_t rotatingIdLongLen;
    strcpy(
        rotatingIdLongString,
        "123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890");

    const ByteSpan & value = GetSpan(rotatingIdLongString);
    rotatingIdLongLen      = Encoding::HexToBytes(reinterpret_cast<const char *>(value.data()), value.size(), rotatingIdLong,
                                             chip::Dnssd::kMaxRotatingIdLen * 2);

    EXPECT_TRUE(rotatingIdLongLen > chip::Dnssd::kMaxRotatingIdLen);

    // test base case
    UDCClientState * state = mUdcClients.FindUDCClientState(instanceName1);
    EXPECT_TRUE(state == nullptr);

    // add a default state
    EXPECT_TRUE(CHIP_NO_ERROR == mUdcClients.CreateNewUDCClientState(instanceName1, &state));

    // get the state
    state = mUdcClients.FindUDCClientState(instanceName1);
    ASSERT_TRUE(nullptr != state);
    EXPECT_TRUE(strcmp(state->GetInstanceName(), instanceName1) == 0);

    state->SetPeerAddress(chip::Transport::PeerAddress::UDP(address, port));
    EXPECT_TRUE(port == state->GetPeerAddress().GetPort());

    state->SetDeviceName(deviceName);
    EXPECT_TRUE(strcmp(state->GetDeviceName(), deviceName) == 0);

    state->SetLongDiscriminator(longDiscriminator);
    EXPECT_TRUE(longDiscriminator == state->GetLongDiscriminator());

    state->SetVendorId(vendorId);
    EXPECT_TRUE(vendorId == state->GetVendorId());

    state->SetProductId(productId);
    EXPECT_TRUE(productId == state->GetProductId());

    state->SetRotatingId(rotatingId, rotatingIdLen);
    EXPECT_TRUE(rotatingIdLen == state->GetRotatingIdLength());

    const uint8_t * testRotatingId = state->GetRotatingId();
    for (size_t i = 0; i < rotatingIdLen; i++)
    {
        EXPECT_TRUE(testRotatingId[i] == rotatingId[i]);
    }

    state->SetRotatingId(rotatingIdLong, rotatingIdLongLen);

    EXPECT_TRUE(chip::Dnssd::kMaxRotatingIdLen == state->GetRotatingIdLength());

    const uint8_t * testRotatingIdLong = state->GetRotatingId();
    for (size_t i = 0; i < chip::Dnssd::kMaxRotatingIdLen; i++)
    {
        EXPECT_TRUE(testRotatingIdLong[i] == rotatingIdLong[i]);
    }
}
