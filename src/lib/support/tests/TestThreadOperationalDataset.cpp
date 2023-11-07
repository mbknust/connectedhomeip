/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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
#include <lib/support/ThreadOperationalDataset.h>
#include <lib/support/UnitTestContext.h>

#include <gtest/gtest.h>

namespace {

using namespace chip;
class TestThreadOperationalDataset : public ::testing::Test
{
public:
    static Thread::OperationalDataset ctx;
};

Thread::OperationalDataset TestThreadOperationalDataset::ctx;

TEST_F(TestThreadOperationalDataset, TestInit)
{
    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint8_t longerThanOperationalDatasetSize[255]{};
    EXPECT_TRUE(dataset.Init(ByteSpan(longerThanOperationalDatasetSize)) == CHIP_ERROR_INVALID_ARGUMENT);
    EXPECT_TRUE(dataset.Init(ByteSpan()) == CHIP_NO_ERROR);

    {
        uint8_t data[] = { 0x01, 0x02, 0x03 };

        EXPECT_TRUE(dataset.Init(ByteSpan(data)) == CHIP_ERROR_INVALID_ARGUMENT);
    }

    {
        uint8_t data[] = { 0x01 };

        EXPECT_TRUE(dataset.Init(ByteSpan(data)) == CHIP_ERROR_INVALID_ARGUMENT);
    }
}

TEST_F(TestThreadOperationalDataset, TestActiveTimestamp)
{
    static constexpr uint64_t kActiveTimestampValue = 1;

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint64_t activeTimestamp = 0;

    EXPECT_TRUE(dataset.SetActiveTimestamp(kActiveTimestampValue) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetActiveTimestamp(activeTimestamp) == CHIP_NO_ERROR);
    EXPECT_TRUE(activeTimestamp == kActiveTimestampValue);
}

TEST_F(TestThreadOperationalDataset, TestChannel)
{
    static constexpr uint16_t kChannelValue = 15;

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint16_t channel = 0;

    EXPECT_TRUE(dataset.SetChannel(kChannelValue) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetChannel(channel) == CHIP_NO_ERROR);
    EXPECT_TRUE(channel == kChannelValue);
}

TEST_F(TestThreadOperationalDataset, TestExtendedPanId)
{
    static constexpr uint8_t kExtendedPanId[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint8_t extendedPanId[Thread::kSizeExtendedPanId] = { 0 };

    EXPECT_TRUE(dataset.SetExtendedPanId(kExtendedPanId) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetExtendedPanId(extendedPanId) == CHIP_NO_ERROR);
    EXPECT_TRUE(memcmp(extendedPanId, kExtendedPanId, sizeof(kExtendedPanId)) == 0);

    ByteSpan span;
    EXPECT_TRUE(dataset.GetExtendedPanIdAsByteSpan(span) == CHIP_NO_ERROR);
    EXPECT_TRUE(span.size() == sizeof(kExtendedPanId));
    EXPECT_TRUE(memcmp(extendedPanId, span.data(), sizeof(kExtendedPanId)) == 0);
}

TEST_F(TestThreadOperationalDataset, TestMasterKey)
{
    static constexpr uint8_t kMasterKey[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                              0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint8_t masterKey[Thread::kSizeMasterKey] = { 0 };

    EXPECT_TRUE(dataset.SetMasterKey(kMasterKey) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetMasterKey(masterKey) == CHIP_NO_ERROR);
    EXPECT_TRUE(memcmp(masterKey, kMasterKey, sizeof(kMasterKey)) == 0);
}

TEST_F(TestThreadOperationalDataset, TestMeshLocalPrefix)
{
    static constexpr uint8_t kMeshLocalPrefix[] = { 0xfd, 0xde, 0xad, 0x00, 0xbe, 0xef, 0x00, 0x00 };

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint8_t meshLocalPrefix[Thread::kSizeMeshLocalPrefix] = { 0 };

    EXPECT_TRUE(dataset.SetMeshLocalPrefix(kMeshLocalPrefix) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetMeshLocalPrefix(meshLocalPrefix) == CHIP_NO_ERROR);
    EXPECT_TRUE(memcmp(meshLocalPrefix, kMeshLocalPrefix, sizeof(kMeshLocalPrefix)) == 0);
}

TEST_F(TestThreadOperationalDataset, TestNetworkName)
{
    static constexpr char kNetworkName[] = "ThreadNetwork";

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    char networkName[Thread::kSizeNetworkName + 1] = { 0 };

    EXPECT_TRUE(dataset.SetNetworkName(kNetworkName) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetNetworkName(networkName) == CHIP_NO_ERROR);
    EXPECT_TRUE(strcmp(networkName, kNetworkName) == 0);

    EXPECT_TRUE(dataset.SetNetworkName("0123456789abcdef") == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.SetNetworkName("0123456789abcdefg") == CHIP_ERROR_INVALID_STRING_LENGTH);
    EXPECT_TRUE(dataset.SetNetworkName("") == CHIP_ERROR_INVALID_STRING_LENGTH);
}

TEST_F(TestThreadOperationalDataset, TestPanId)
{
    static constexpr uint16_t kPanIdValue = 0x1234;

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint16_t panid = 0;

    EXPECT_TRUE(dataset.SetPanId(kPanIdValue) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetPanId(panid) == CHIP_NO_ERROR);
    EXPECT_TRUE(panid == kPanIdValue);
}

TEST_F(TestThreadOperationalDataset, TestPSKc)
{
    static constexpr uint8_t kPSKc[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                         0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint8_t pskc[Thread::kSizePSKc] = { 0 };

    EXPECT_TRUE(dataset.SetPSKc(kPSKc) == CHIP_NO_ERROR);
    EXPECT_TRUE(dataset.GetPSKc(pskc) == CHIP_NO_ERROR);
    EXPECT_TRUE(memcmp(pskc, kPSKc, sizeof(kPSKc)) == 0);
}

TEST_F(TestThreadOperationalDataset, TestUnsetMasterKey)
{
    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint8_t masterKey[Thread::kSizeMasterKey] = { 0 };

    EXPECT_TRUE(dataset.GetMasterKey(masterKey) == CHIP_NO_ERROR);
    dataset.UnsetMasterKey();
    EXPECT_TRUE(dataset.GetMasterKey(masterKey) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    EXPECT_TRUE(dataset.SetMasterKey(masterKey) == CHIP_NO_ERROR);
}

TEST_F(TestThreadOperationalDataset, TestUnsetPSKc)
{
    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    uint8_t pskc[Thread::kSizePSKc] = { 0 };

    EXPECT_TRUE(dataset.GetPSKc(pskc) == CHIP_NO_ERROR);
    dataset.UnsetPSKc();
    EXPECT_TRUE(dataset.GetPSKc(pskc) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    EXPECT_TRUE(dataset.SetPSKc(pskc) == CHIP_NO_ERROR);
}

TEST_F(TestThreadOperationalDataset, TestClear)
{
    Thread::OperationalDataset & dataset = TestThreadOperationalDataset::ctx;

    {
        uint64_t activeTimestamp;
        EXPECT_TRUE(dataset.GetActiveTimestamp(activeTimestamp) == CHIP_NO_ERROR);
    }

    {
        uint16_t channel;
        EXPECT_TRUE(dataset.GetChannel(channel) == CHIP_NO_ERROR);
    }

    {
        uint8_t extendedPanId[Thread::kSizeExtendedPanId] = { 0 };
        EXPECT_TRUE(dataset.GetExtendedPanId(extendedPanId) == CHIP_NO_ERROR);
    }

    {
        uint8_t masterKey[Thread::kSizeMasterKey] = { 0 };
        EXPECT_TRUE(dataset.GetMasterKey(masterKey) == CHIP_NO_ERROR);
    }

    {
        uint8_t meshLocalPrefix[Thread::kSizeMeshLocalPrefix] = { 0 };
        EXPECT_TRUE(dataset.GetMeshLocalPrefix(meshLocalPrefix) == CHIP_NO_ERROR);
    }

    {
        char networkName[Thread::kSizeNetworkName + 1] = { 0 };
        EXPECT_TRUE(dataset.GetNetworkName(networkName) == CHIP_NO_ERROR);
    }

    {
        uint16_t panid;
        EXPECT_TRUE(dataset.GetPanId(panid) == CHIP_NO_ERROR);
    }

    {
        uint8_t pskc[Thread::kSizePSKc] = { 0 };
        EXPECT_TRUE(dataset.GetPSKc(pskc) == CHIP_NO_ERROR);
    }

    dataset.Clear();

    {
        uint64_t activeTimestamp;
        EXPECT_TRUE(dataset.GetActiveTimestamp(activeTimestamp) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }

    {
        uint16_t channel;
        EXPECT_TRUE(dataset.GetChannel(channel) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }

    {
        uint8_t extendedPanId[Thread::kSizeExtendedPanId] = { 0 };
        EXPECT_TRUE(dataset.GetExtendedPanId(extendedPanId) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }

    {
        uint8_t masterKey[Thread::kSizeMasterKey] = { 0 };
        EXPECT_TRUE(dataset.GetMasterKey(masterKey) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }

    {
        uint8_t meshLocalPrefix[Thread::kSizeMeshLocalPrefix] = { 0 };
        EXPECT_TRUE(dataset.GetMeshLocalPrefix(meshLocalPrefix) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }

    {
        char networkName[Thread::kSizeNetworkName + 1] = { 0 };
        EXPECT_TRUE(dataset.GetNetworkName(networkName) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }

    {
        uint16_t panid;
        EXPECT_TRUE(dataset.GetPanId(panid) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }

    {
        uint8_t pskc[Thread::kSizePSKc] = { 0 };
        EXPECT_TRUE(dataset.GetPSKc(pskc) == CHIP_ERROR_TLV_TAG_NOT_FOUND);
    }
}
} // namespace
