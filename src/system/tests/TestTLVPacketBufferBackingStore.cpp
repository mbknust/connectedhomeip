/*
 *    Copyright (c) 2023 Project CHIP Authors
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lib/support/CHIPMem.h>
#include <lib/support/ScopedBuffer.h>
#include <lib/support/Span.h>

#include <system/TLVPacketBufferBackingStore.h>

#include <gtest/gtest.h>

using ::chip::Platform::ScopedMemoryBuffer;
using ::chip::System::PacketBuffer;
using ::chip::System::PacketBufferHandle;
using ::chip::System::PacketBufferTLVReader;
using ::chip::System::PacketBufferTLVWriter;
using namespace ::chip;

namespace {

class TestTLVPacketBufferBackingStore : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }

    static void BasicEncodeDecode();
    static void MultiBufferEncode();
};

/**
 * Test that we can do a basic encode to TLV followed by decode.
 */
TEST_F(TestTLVPacketBufferBackingStore, BasicEncodeDecode)
{
    auto buffer = PacketBufferHandle::New(PacketBuffer::kMaxSizeWithoutReserve, 0);

    PacketBufferTLVWriter writer;
    writer.Init(std::move(buffer));

    TLV::TLVType outerContainerType;
    CHIP_ERROR error = writer.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Array, outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.Put(TLV::AnonymousTag(), static_cast<uint8_t>(7));
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.Put(TLV::AnonymousTag(), static_cast<uint8_t>(8));
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.Put(TLV::AnonymousTag(), static_cast<uint8_t>(9));
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.EndContainer(outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.Finalize(&buffer);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    // Array start/end is 2 bytes.  Each entry is also 2 bytes: control +
    // value.  So 8 bytes total.
    EXPECT_TRUE(!buffer->HasChainedBuffer());
    EXPECT_TRUE(buffer->TotalLength() == 8);
    EXPECT_TRUE(buffer->DataLength() == 8);

    PacketBufferTLVReader reader;
    reader.Init(std::move(buffer));

    error = reader.Next(TLV::kTLVType_Array, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.EnterContainer(outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.Next(TLV::kTLVType_UnsignedInteger, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    uint8_t value;
    error = reader.Get(value);
    EXPECT_TRUE(error == CHIP_NO_ERROR);
    EXPECT_TRUE(value == 7);

    error = reader.Next(TLV::kTLVType_UnsignedInteger, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.Get(value);
    EXPECT_TRUE(error == CHIP_NO_ERROR);
    EXPECT_TRUE(value == 8);

    error = reader.Next(TLV::kTLVType_UnsignedInteger, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.Get(value);
    EXPECT_TRUE(error == CHIP_NO_ERROR);
    EXPECT_TRUE(value == 9);

    error = reader.Next();
    EXPECT_TRUE(error == CHIP_END_OF_TLV);

    error = reader.ExitContainer(outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.Next();
    EXPECT_TRUE(error == CHIP_END_OF_TLV);
}

/**
 * Test that we can do an encode that's going to split across multiple buffers correctly.
 */
TEST_F(TestTLVPacketBufferBackingStore, MultiBufferEncode)
{
    // Start with a too-small buffer.
    auto buffer = PacketBufferHandle::New(2, 0);

    PacketBufferTLVWriter writer;
    writer.Init(std::move(buffer), /* useChainedBuffers = */ true);

    TLV::TLVType outerContainerType;
    CHIP_ERROR error = writer.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Array, outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.Put(TLV::AnonymousTag(), static_cast<uint8_t>(7));
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.Put(TLV::AnonymousTag(), static_cast<uint8_t>(8));
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    // Something to make sure we have 3 buffers.
    uint8_t bytes[2000] = { 0 };
    error               = writer.Put(TLV::AnonymousTag(), ByteSpan(bytes));
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.EndContainer(outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = writer.Finalize(&buffer);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    // Array start/end is 2 bytes.  First two entries are 2 bytes each.
    // Third entry is 1 control byte, 2 length bytes, 2000 bytes of data,
    // for a total of 2009 bytes.
    constexpr size_t totalSize = 2009;
    EXPECT_TRUE(buffer->HasChainedBuffer());
    EXPECT_TRUE(buffer->TotalLength() == totalSize);
    EXPECT_TRUE(buffer->DataLength() == 2);
    auto nextBuffer = buffer->Next();
    EXPECT_TRUE(nextBuffer->HasChainedBuffer());
    EXPECT_TRUE(nextBuffer->TotalLength() == totalSize - 2);
    EXPECT_TRUE(nextBuffer->DataLength() == PacketBuffer::kMaxSizeWithoutReserve);
    nextBuffer = nextBuffer->Next();
    EXPECT_TRUE(!nextBuffer->HasChainedBuffer());
    EXPECT_TRUE(nextBuffer->TotalLength() == nextBuffer->DataLength());
    EXPECT_TRUE(nextBuffer->DataLength() == totalSize - 2 - PacketBuffer::kMaxSizeWithoutReserve);

    // PacketBufferTLVReader cannot handle non-contiguous buffers, and our
    // buffers are too big to stick into a single packet buffer.
    ScopedMemoryBuffer<uint8_t> buf;
    EXPECT_TRUE(buf.Calloc(totalSize));
    size_t offset = 0;
    while (!buffer.IsNull())
    {
        memcpy(buf.Get() + offset, buffer->Start(), buffer->DataLength());
        offset += buffer->DataLength();
        buffer.Advance();
        EXPECT_TRUE(offset < totalSize || (offset == totalSize && buffer.IsNull()));
    }

    TLV::TLVReader reader;
    reader.Init(buf.Get(), totalSize);

    error = reader.Next(TLV::kTLVType_Array, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.EnterContainer(outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.Next(TLV::kTLVType_UnsignedInteger, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    uint8_t value;
    error = reader.Get(value);
    EXPECT_TRUE(error == CHIP_NO_ERROR);
    EXPECT_TRUE(value == 7);

    error = reader.Next(TLV::kTLVType_UnsignedInteger, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.Get(value);
    EXPECT_TRUE(error == CHIP_NO_ERROR);
    EXPECT_TRUE(value == 8);

    error = reader.Next(TLV::kTLVType_ByteString, TLV::AnonymousTag());
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    ByteSpan byteValue;
    error = reader.Get(byteValue);
    EXPECT_TRUE(error == CHIP_NO_ERROR);
    EXPECT_TRUE(byteValue.size() == sizeof(bytes));

    error = reader.Next();
    EXPECT_TRUE(error == CHIP_END_OF_TLV);

    error = reader.ExitContainer(outerContainerType);
    EXPECT_TRUE(error == CHIP_NO_ERROR);

    error = reader.Next();
    EXPECT_TRUE(error == CHIP_END_OF_TLV);
}

} // anonymous namespace
