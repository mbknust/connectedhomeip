/*
 *
 *    Copyright (c) 2020-2022 Project CHIP Authors
 *    Copyright (c) 2016-2017 Nest Labs, Inc.
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
 *      This file implements a unit test suite for
 *      <tt>chip::System::PacketBuffer</tt>, a class that provides
 *      structure for network packet buffer management.
 */

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <vector>

#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/UnitTestContext.h>

#include <platform/CHIPDeviceLayer.h>
#include <system/SystemPacketBuffer.h>

#if CHIP_SYSTEM_CONFIG_USE_LWIP
#include <lwip/init.h>
#include <lwip/tcpip.h>
#endif // CHIP_SYSTEM_CONFIG_USE_LWIP

#include <gtest/gtest.h>

#if CHIP_SYSTEM_CONFIG_USE_LWIP
#if (LWIP_VERSION_MAJOR == 2) && (LWIP_VERSION_MINOR == 0)
#define PBUF_TYPE(pbuf) (pbuf)->type
#else // (LWIP_VERSION_MAJOR == 2) && (LWIP_VERSION_MINOR == 0)
#define PBUF_TYPE(pbuf) (pbuf)->type_internal
#endif // (LWIP_VERSION_MAJOR == 2) && (LWIP_VERSION_MINOR == 0)
#endif // CHIP_SYSTEM_CONFIG_USE_LWIP

using ::chip::Encoding::PacketBufferWriter;
using ::chip::System::PacketBuffer;
using ::chip::System::PacketBufferHandle;

#if !CHIP_SYSTEM_CONFIG_USE_LWIP
using ::chip::System::pbuf;
#endif

// Utility functions.

#define TO_LWIP_PBUF(x) (reinterpret_cast<struct pbuf *>(reinterpret_cast<void *>(x)))
#define OF_LWIP_PBUF(x) (reinterpret_cast<PacketBuffer *>(reinterpret_cast<void *>(x)))

namespace {
void ScrambleData(uint8_t * start, uint16_t length)
{
    for (uint16_t i = 0; i < length; ++i)
        ++start[i];
}
} // namespace

/*
 * An instance of this class created for the test suite.
 * It is a friend class of `PacketBuffer` and `PacketBufferHandle` because some tests
 * use or check private methods or properties.
 */
class PacketBufferTest : public ::testing::Test
{
public:
    struct TestContext
    {
        const uint16_t * const reserved_sizes;
        size_t reserved_size_count;
        const uint16_t * const lengths;
        size_t length_count;
    };

    static TestContext ctx;
    static void SetUpTestSuite()
    {
        VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR);
        VerifyOrDie(chip::DeviceLayer::PlatformMgr().InitChipStack() == CHIP_NO_ERROR);

        // Set up the buffer configuration vector for this suite.
        configurations.resize(0);
        for (size_t i = 0; i < ctx.reserved_size_count; ++i)
        {
            configurations.emplace_back<BufferConfiguration>(ctx.reserved_sizes[i]);
        }
    }

    static void TearDownTestSuite()
    {
        chip::DeviceLayer::PlatformMgr().Shutdown();
        chip::Platform::MemoryShutdown();
    }

    void TearDown() override
    {
        // Clear the configurations' bufffer handles.
        for (auto & configuration : configurations)
        {
            configuration.handle = nullptr;
        }
        ASSERT_TRUE(ResetHandles());
    }

    static void CheckNew();
    static void CheckStart();
    static void CheckSetStart();
    static void CheckDataLength();
    static void CheckSetDataLength();
    static void CheckTotalLength();
    static void CheckMaxDataLength();
    static void CheckAvailableDataLength();
    static void CheckReservedSize();
    static void CheckHasChainedBuffer();
    static void CheckAddToEnd();
    static void CheckPopHead();
    static void CheckCompactHead();
    static void CheckConsumeHead();
    static void CheckConsume();
    static void CheckEnsureReservedSize();
    static void CheckAlignPayload();
    static void CheckNext();
    static void CheckLast();
    static void CheckRead();
    static void CheckAddRef();
    static void CheckFree();
    static void CheckFreeHead();
    static void CheckHandleConstruct();
    static void CheckHandleMove();
    static void CheckHandleRelease();
    static void CheckHandleFree();
    static void CheckHandleRetain();
    static void CheckHandleAdopt();
    static void CheckHandleHold();
    static void CheckHandleAdvance();
    static void CheckHandleRightSize();
    static void CheckHandleCloneData();
    static void CheckPacketBufferWriter();
    static void CheckBuildFreeList();

    static void PrintHandle(const char * tag, const PacketBuffer * buffer)
    {
        printf("%s %p ref=%u len=%-4u next=%p\n", StringOrNullMarker(tag), buffer, buffer ? buffer->ref : 0,
               buffer ? buffer->len : 0, buffer ? buffer->next : nullptr);
    }
    static void PrintHandle(const char * tag, const PacketBufferHandle & handle) { PrintHandle(tag, handle.mBuffer); }

    static constexpr uint16_t kBlockSize = PacketBuffer::kBlockSize;

protected:
    struct BufferConfiguration
    {
        BufferConfiguration(uint16_t aReservedSize = 0) :
            init_len(0), reserved_size(aReservedSize), start_buffer(nullptr), end_buffer(nullptr), payload_ptr(nullptr),
            handle(nullptr)
        {}

        uint16_t init_len;
        uint16_t reserved_size;
        uint8_t * start_buffer;
        uint8_t * end_buffer;
        uint8_t * payload_ptr;
        PacketBufferHandle handle;
    };

    static void PrintHandle(const char * tag, const BufferConfiguration & config) { PrintHandle(tag, config.handle); }
    static void PrintConfig(const char * tag, const BufferConfiguration & config)
    {
        printf("%s pay=%-4zu len=%-4u res=%-4u:", StringOrNullMarker(tag), config.payload_ptr - config.start_buffer,
               config.init_len, config.reserved_size);
        PrintHandle("", config.handle);
    }

    /*
     * Buffers allocated through PrepareTestBuffer with kRecordHandle set will be recorded in `handles` so that their
     * reference counts can be verified by ResetHandles(). Initially they have two refs: the recorded one and the returned one.
     */
    static constexpr int kRecordHandle     = 0x01;
    static constexpr int kAllowHandleReuse = 0x02;
    static void PrepareTestBuffer(BufferConfiguration * config, int flags = 0);

    /*
     * Checks and clears the recorded handles. Returns true if it detects no leaks or double frees.
     * Called from `TerminateTest()`, but tests may choose to call it more often to verify reference counts.
     */
    static bool ResetHandles();

    static std::vector<BufferConfiguration> configurations;
    static std::vector<PacketBufferHandle> handles;
};

std::vector<PacketBufferTest::BufferConfiguration> PacketBufferTest::configurations;
std::vector<PacketBufferHandle> PacketBufferTest::handles;

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name) { test_fixture::test_name(); }                                                                 \
    void test_fixture::test_name()

const uint16_t sTestReservedSizes[] = { 0, 10, 128, 1536, PacketBuffer::kMaxSizeWithoutReserve, PacketBufferTest::kBlockSize };
const uint16_t sTestLengths[]       = { 0, 1, 10, 128, PacketBufferTest::kBlockSize, UINT16_MAX };

PacketBufferTest::TestContext PacketBufferTest::ctx = {
    sTestReservedSizes,
    sizeof(sTestReservedSizes) / sizeof(uint16_t),
    sTestLengths,
    sizeof(sTestLengths) / sizeof(uint16_t),
};

/**
 *  Allocate memory for a test buffer and configure according to test buffer configuration.
 */
void PacketBufferTest::PrepareTestBuffer(BufferConfiguration * config, int flags)
{
    if (config->handle.IsNull())
    {
        config->handle = PacketBufferHandle::New(chip::System::PacketBuffer::kMaxSizeWithoutReserve, 0);
        if (config->handle.IsNull())
        {
            printf("NewPacketBuffer: Failed to allocate packet buffer (%u retained): %s\n",
                   static_cast<unsigned int>(handles.size()), strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (flags & kRecordHandle)
        {
            handles.push_back(config->handle.Retain());
        }
    }
    else if ((flags & kAllowHandleReuse) == 0)
    {
        printf("Dirty test configuration\n");
        exit(EXIT_FAILURE);
    }

    const size_t lInitialSize = PacketBuffer::kStructureSize + config->reserved_size;
    const size_t lAllocSize   = kBlockSize;

    uint8_t * const raw = reinterpret_cast<uint8_t *>(config->handle.Get());
    memset(raw + PacketBuffer::kStructureSize, 0, lAllocSize - PacketBuffer::kStructureSize);

    config->start_buffer = raw;
    config->end_buffer   = raw + lAllocSize;

    if (lInitialSize > lAllocSize)
    {
        config->payload_ptr = config->end_buffer;
    }
    else
    {
        config->payload_ptr = config->start_buffer + lInitialSize;
    }

    if (config->handle->HasChainedBuffer())
    {
        // This should not happen.
        PacketBuffer::Free(config->handle->ChainedBuffer());
        config->handle->next = nullptr;
    }
    config->handle->payload = config->payload_ptr;
    config->handle->len     = config->init_len;
    config->handle->tot_len = config->init_len;
}

bool PacketBufferTest::ResetHandles()
{
    // Check against leaks or double-frees in tests: every handle obtained through
    // PacketBufferTest::NewPacketBuffer should have a reference count of 1.
    bool handles_ok = true;
    for (size_t i = 0; i < handles.size(); ++i)
    {
        const PacketBufferHandle & handle = handles[i];
        if (handle.Get() == nullptr)
        {
            printf("TestTerminate: handle %u null\n", static_cast<unsigned int>(i));
            handles_ok = false;
        }
        else if (handle->ref != 1)
        {
            printf("TestTerminate: handle %u buffer=%p ref=%u\n", static_cast<unsigned int>(i), handle.Get(), handle->ref);
            handles_ok = false;
            while (handle->ref > 1)
            {
                PacketBuffer::Free(handle.Get());
            }
        }
    }
    handles.resize(0);
    return handles_ok;
}

// Test functions invoked from the suite.

/**
 *  Test PacketBufferHandle::New() function.
 *
 *  Description: For every buffer-configuration from inContext, create a buffer's instance
 *               using the New() method. Then, verify that when the size of the reserved space
 *               passed to New() is greater than PacketBuffer::kMaxSizeWithoutReserve,
 *               the method returns nullptr. Otherwise, check for correctness of initializing
 *               the new buffer's internal state.
 */
STATIC_TEST(PacketBufferTest, CheckNew)
{
    for (const auto & config : configurations)
    {
        const PacketBufferHandle buffer = PacketBufferHandle::New(0, config.reserved_size);

        if (config.reserved_size > PacketBuffer::kMaxSizeWithoutReserve)
        {
            EXPECT_TRUE(buffer.IsNull());
            continue;
        }

        EXPECT_TRUE(config.reserved_size <= buffer->AllocSize());
        EXPECT_TRUE(!buffer.IsNull());

        if (!buffer.IsNull())
        {
            const pbuf * const pb = TO_LWIP_PBUF(buffer.Get());

            EXPECT_TRUE(pb->len == 0);
            EXPECT_TRUE(pb->tot_len == 0);
            EXPECT_TRUE(pb->next == nullptr);
            EXPECT_TRUE(pb->ref == 1);
        }
    }

#if CHIP_SYSTEM_PACKETBUFFER_FROM_LWIP_POOL || CHIP_SYSTEM_PACKETBUFFER_FROM_CHIP_POOL
    // Use the rest of the buffer space
    std::vector<PacketBufferHandle> allocate_all_the_things;
    for (;;)
    {
        PacketBufferHandle buffer = PacketBufferHandle::New(0, 0);
        if (buffer.IsNull())
        {
            break;
        }
        // Hold on to the buffer, to use up all the buffer space.
        allocate_all_the_things.push_back(std::move(buffer));
    }
#endif // CHIP_SYSTEM_PACKETBUFFER_FROM_LWIP_POOL || CHIP_SYSTEM_PACKETBUFFER_FROM_CHIP_POOL
}

/**
 *  Test PacketBuffer::Start() function.
 */
STATIC_TEST(PacketBufferTest, CheckStart)
{
    for (auto & config : configurations)
    {
        PrepareTestBuffer(&config, kRecordHandle);
        EXPECT_TRUE(config.handle->Start() == config.payload_ptr);
    }
}

/**
 *  Test PacketBuffer::SetStart() function.
 *
 *  Description: For every buffer-configuration from inContext, create a
 *               buffer's instance according to the configuration. Next,
 *               for any offset value from start_offset[], pass it to the
 *               buffer's instance through SetStart method. Then, verify that
 *               the beginning of the buffer has been correctly internally
 *               adjusted according to the offset value passed into the
 *               SetStart() method.
 */
STATIC_TEST(PacketBufferTest, CheckSetStart)
{
    static constexpr ptrdiff_t sSizePacketBuffer = kBlockSize;

    for (auto & config : configurations)
    {
        // clang-format off
        static constexpr ptrdiff_t start_offset[] =
        {
            -sSizePacketBuffer,
            -128,
            -1,
            0,
            1,
            128,
            sSizePacketBuffer
        };
        // clang-format on

        for (ptrdiff_t offset : start_offset)
        {
            PrepareTestBuffer(&config, kRecordHandle | kAllowHandleReuse);
            uint8_t * const test_start = config.payload_ptr + offset;
            uint8_t * verify_start     = test_start;

            config.handle->SetStart(test_start);

            if (verify_start < config.start_buffer + PacketBuffer::kStructureSize)
            {
                // Set start before valid payload beginning.
                verify_start = config.start_buffer + PacketBuffer::kStructureSize;
            }

            if (verify_start > config.end_buffer)
            {
                // Set start after valid payload beginning.
                verify_start = config.end_buffer;
            }

            EXPECT_TRUE(config.handle->payload == verify_start);

            if ((verify_start - config.payload_ptr) > config.init_len)
            {
                // Set start to the beginning of payload, right after handle's header.
                EXPECT_TRUE(config.handle->len == 0);
            }
            else
            {
                // Set start to somewhere between the end of the handle's
                // header and the end of payload.
                EXPECT_TRUE(config.handle->len == (config.init_len - (verify_start - config.payload_ptr)));
            }
        }
    }
}

/**
 *  Test PacketBuffer::DataLength() function.
 */
STATIC_TEST(PacketBufferTest, CheckDataLength)
{
    for (auto & config : configurations)
    {
        PrepareTestBuffer(&config, kRecordHandle);

        EXPECT_TRUE(config.handle->DataLength() == config.handle->len);
    }
}

/**
 *  Test PacketBuffer::SetDataLength() function.
 *
 *  Description: Take two initial configurations of PacketBuffer from
 *               inContext and create two PacketBuffer instances based on those
 *               configurations. For any two buffers, call SetDataLength with
 *               different value from sLength[]. If two buffers are created with
 *               the same configuration, test SetDataLength on one buffer,
 *               without specifying the head of the buffer chain. Otherwise,
 *               test SetDataLength with one buffer being down the chain and the
 *               other one being passed as the head of the chain. After calling
 *               the method verify that data lengths were correctly adjusted.
 */
STATIC_TEST(PacketBufferTest, CheckSetDataLength)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            for (size_t i = 0; i < ctx.length_count; ++i)
            {
                const uint16_t length = ctx.lengths[i];
                PrepareTestBuffer(&config_1, kRecordHandle | kAllowHandleReuse);
                PrepareTestBuffer(&config_2, kRecordHandle | kAllowHandleReuse);

                if (&config_1 == &config_2)
                {
                    // headOfChain (the second arg) is NULL
                    config_2.handle->SetDataLength(length, nullptr);

                    if (length > (config_2.end_buffer - config_2.payload_ptr))
                    {
                        EXPECT_TRUE(config_2.handle->len == (config_2.end_buffer - config_2.payload_ptr));
                        EXPECT_TRUE(config_2.handle->tot_len == (config_2.end_buffer - config_2.payload_ptr));
                        EXPECT_TRUE(config_2.handle->next == nullptr);
                    }
                    else
                    {
                        EXPECT_TRUE(config_2.handle->len == length);
                        EXPECT_TRUE(config_2.handle->tot_len == length);
                        EXPECT_TRUE(config_2.handle->next == nullptr);
                    }
                }
                else
                {
                    // headOfChain (the second arg) is config_1.handle
                    config_2.handle->SetDataLength(length, config_1.handle);

                    if (length > (config_2.end_buffer - config_2.payload_ptr))
                    {
                        EXPECT_TRUE(config_2.handle->len == (config_2.end_buffer - config_2.payload_ptr));
                        EXPECT_TRUE(config_2.handle->tot_len == (config_2.end_buffer - config_2.payload_ptr));
                        EXPECT_TRUE(config_2.handle->next == nullptr);

                        EXPECT_TRUE(config_1.handle->tot_len ==
                                    (config_1.init_len + static_cast<int32_t>(config_2.end_buffer - config_2.payload_ptr) -
                                     static_cast<int32_t>(config_2.init_len)));
                    }
                    else
                    {
                        EXPECT_TRUE(config_2.handle->len == length);
                        EXPECT_TRUE(config_2.handle->tot_len == length);
                        EXPECT_TRUE(config_2.handle->next == nullptr);

                        EXPECT_TRUE(config_1.handle->tot_len ==
                                    (config_1.init_len + static_cast<int32_t>(length) - static_cast<int32_t>(config_2.init_len)));
                    }
                }
            }
        }
    }
}

/**
 *  Test PacketBuffer::TotalLength() function.
 */
STATIC_TEST(PacketBufferTest, CheckTotalLength)
{
    for (auto & config : configurations)
    {
        PrepareTestBuffer(&config, kRecordHandle);
        EXPECT_TRUE(config.handle->TotalLength() == config.init_len);
    }
}

/**
 *  Test PacketBuffer::MaxDataLength() function.
 */
STATIC_TEST(PacketBufferTest, CheckMaxDataLength)
{
    for (auto & config : configurations)
    {
        PrepareTestBuffer(&config, kRecordHandle);

        EXPECT_TRUE(config.handle->MaxDataLength() == (config.end_buffer - config.payload_ptr));
    }
}

/**
 *  Test PacketBuffer::AvailableDataLength() function.
 */
STATIC_TEST(PacketBufferTest, CheckAvailableDataLength)
{
    for (auto & config : configurations)
    {
        PrepareTestBuffer(&config, kRecordHandle);

        EXPECT_TRUE(config.handle->AvailableDataLength() == ((config.end_buffer - config.payload_ptr) - config.init_len));
    }
}

/**
 *  Test PacketBuffer::ReservedSize() function.
 */
STATIC_TEST(PacketBufferTest, CheckReservedSize)
{
    for (auto & config : configurations)
    {
        PrepareTestBuffer(&config, kRecordHandle);
        const size_t kAllocSize = config.handle->AllocSize();

        if (config.reserved_size > kAllocSize)
        {
            EXPECT_TRUE(config.handle->ReservedSize() == kAllocSize);
        }
        else
        {
            EXPECT_TRUE(config.handle->ReservedSize() == config.reserved_size);
        }
    }
}

/**
 *  Test PacketBuffer::HasChainedBuffer() function.
 */
STATIC_TEST(PacketBufferTest, CheckHasChainedBuffer)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            PrepareTestBuffer(&config_1);
            PrepareTestBuffer(&config_2);

            EXPECT_TRUE(config_1.handle->HasChainedBuffer() == false);
            EXPECT_TRUE(config_2.handle->HasChainedBuffer() == false);

            config_1.handle->AddToEnd(config_2.handle.Retain());
            EXPECT_TRUE(config_1.handle->HasChainedBuffer() == true);
            EXPECT_TRUE(config_2.handle->HasChainedBuffer() == false);

            config_1.handle = nullptr;
            config_2.handle = nullptr;
        }
    }
}

/**
 *  Test PacketBuffer::AddToEnd() function.
 *
 *  Description: Take three initial configurations of PacketBuffer from
 *               inContext, create three PacketBuffers based on those
 *               configurations and then link those buffers together with
 *               PacketBuffer:AddToEnd(). Then, assert that after connecting
 *               buffers together, their internal states are correctly updated.
 *               This test function tests linking any combination of three
 *               buffer-configurations passed within inContext.
 */
STATIC_TEST(PacketBufferTest, CheckAddToEnd)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            for (auto & config_3 : configurations)
            {
                if (&config_1 == &config_2 || &config_1 == &config_3 || &config_2 == &config_3)
                {
                    continue;
                }

                PrepareTestBuffer(&config_1);
                PrepareTestBuffer(&config_2);
                PrepareTestBuffer(&config_3);
                EXPECT_TRUE(config_1.handle->ref == 1);
                EXPECT_TRUE(config_2.handle->ref == 1);
                EXPECT_TRUE(config_3.handle->ref == 1);

                config_1.handle->AddToEnd(config_2.handle.Retain());
                EXPECT_TRUE(config_1.handle->ref == 1); // config_1.handle
                EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle->next
                EXPECT_TRUE(config_3.handle->ref == 1); // config_3.handle

                EXPECT_TRUE(config_1.handle->tot_len == (config_1.init_len + config_2.init_len));
                EXPECT_TRUE(config_1.handle->next == config_2.handle.Get());
                EXPECT_TRUE(config_2.handle->next == nullptr);
                EXPECT_TRUE(config_3.handle->next == nullptr);

                config_1.handle->AddToEnd(config_3.handle.Retain());
                EXPECT_TRUE(config_1.handle->ref == 1); // config_1.handle
                EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle->next
                EXPECT_TRUE(config_3.handle->ref == 2); // config_3.handle and config_2.handle->next

                EXPECT_TRUE(config_1.handle->tot_len == (config_1.init_len + config_2.init_len + config_3.init_len));
                EXPECT_TRUE(config_1.handle->next == config_2.handle.Get());
                EXPECT_TRUE(config_2.handle->next == config_3.handle.Get());
                EXPECT_TRUE(config_3.handle->next == nullptr);

                config_1.handle = nullptr;
                config_2.handle = nullptr;
                config_3.handle = nullptr;
            }
        }
    }
}

/**
 *  Test PacketBuffer::PopHead() function.
 *
 *  Description: Take two initial configurations of PacketBuffer from
 *               inContext and create two PacketBuffer instances based on those
 *               configurations. Next, link those buffers together, with the first
 *               buffer instance pointing to the second one. Then, call PopHead()
 *               on the first buffer to unlink the second buffer. After the call,
 *               verify correct internal state of the first buffer.
 */
STATIC_TEST(PacketBufferTest, CheckPopHead)
{
    // Single buffer test.
    for (auto & config_1 : configurations)
    {
        PrepareTestBuffer(&config_1, kRecordHandle | kAllowHandleReuse);
        EXPECT_TRUE(config_1.handle->ref == 2);

        const PacketBuffer * const buffer_1 = config_1.handle.mBuffer;

        const PacketBufferHandle popped = config_1.handle.PopHead();

        EXPECT_TRUE(config_1.handle.IsNull());
        EXPECT_TRUE(popped.mBuffer == buffer_1);
        EXPECT_TRUE(popped->next == nullptr);
        EXPECT_TRUE(popped->tot_len == config_1.init_len);
        EXPECT_TRUE(popped->ref == 2);
    }
    ResetHandles();

    // Chained buffers test.
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            PrepareTestBuffer(&config_1, kRecordHandle | kAllowHandleReuse);
            PrepareTestBuffer(&config_2, kRecordHandle | kAllowHandleReuse);

            config_1.handle->AddToEnd(config_2.handle.Retain());

            const PacketBufferHandle popped = config_1.handle.PopHead();

            EXPECT_TRUE(config_1.handle == config_2.handle);
            EXPECT_TRUE(config_1.handle->next == nullptr);
            EXPECT_TRUE(config_1.handle->tot_len == config_1.init_len);
        }
    }
}

/**
 *  Test PacketBuffer::CompactHead() function.
 *
 *  Description: Take two initial configurations of PacketBuffer from
 *               inContext and create two PacketBuffer instances based on those
 *               configurations. Next, set both buffers' data length to any
 *               combination of values from sLengths[] and link those buffers
 *               into a chain. Then, call CompactHead() on the first buffer in
 *               the chain. After calling the method, verify correctly adjusted
 *               state of the first buffer.
 */
STATIC_TEST(PacketBufferTest, CheckCompactHead)
{
    // Single buffer test.
    for (auto & config : configurations)
    {
        for (size_t i = 0; i < ctx.length_count; ++i)
        {
            const uint16_t length = ctx.lengths[i];

            PrepareTestBuffer(&config, kRecordHandle | kAllowHandleReuse);
            config.handle->SetDataLength(length, config.handle);
            const uint16_t data_length = config.handle->DataLength();

            config.handle->CompactHead();

            EXPECT_TRUE(config.handle->payload == (config.start_buffer + PacketBuffer::kStructureSize));
            EXPECT_TRUE(config.handle->tot_len == data_length);
        }

        config.handle = nullptr;
    }
    EXPECT_TRUE(ResetHandles());

    // Chained buffers test.
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            // start with various initial length for the first buffer
            for (size_t i = 0; i < ctx.length_count; ++i)
            {
                const uint16_t length_1 = ctx.lengths[i];

                // start with various initial length for the second buffer
                for (size_t j = 0; j < ctx.length_count; ++j)
                {
                    const uint16_t length_2 = ctx.lengths[j];

                    PrepareTestBuffer(&config_1, kRecordHandle | kAllowHandleReuse);
                    EXPECT_TRUE(config_1.handle->ref == 2);

                    // CompactHead requires that there be no other references to the chained buffer,
                    // so we manage it manually.
                    PrepareTestBuffer(&config_2);
                    EXPECT_TRUE(config_2.handle->ref == 1);
                    PacketBuffer * buffer_2 = std::move(config_2.handle).UnsafeRelease();
                    EXPECT_TRUE(config_2.handle.IsNull());

                    config_1.handle->SetDataLength(length_1, config_1.handle);
                    const uint16_t data_length_1 = config_1.handle->DataLength();

                    // This chain will cause buffer_2 to be freed.
                    config_1.handle->next = buffer_2;

                    // Add various lengths to the second buffer
                    buffer_2->SetDataLength(length_2, config_1.handle);
                    const uint16_t data_length_2 = buffer_2->DataLength();

                    config_1.handle->CompactHead();

                    EXPECT_TRUE(config_1.handle->payload == (config_1.start_buffer + PacketBuffer::kStructureSize));

                    if (config_1.handle->tot_len > config_1.handle->MaxDataLength())
                    {
                        EXPECT_TRUE(config_1.handle->len == config_1.handle->MaxDataLength());
                        EXPECT_TRUE(buffer_2->len == config_1.handle->tot_len - config_1.handle->MaxDataLength());
                        EXPECT_TRUE(config_1.handle->next == buffer_2);
                        EXPECT_TRUE(config_1.handle->ref == 2);
                        EXPECT_TRUE(buffer_2->ref == 1);
                    }
                    else
                    {
                        EXPECT_TRUE(config_1.handle->len == config_1.handle->tot_len);
                        if (data_length_1 >= config_1.handle->MaxDataLength() && data_length_2 == 0)
                        {
                            /* make sure the second buffer is not freed */
                            EXPECT_TRUE(config_1.handle->next == buffer_2);
                            EXPECT_TRUE(buffer_2->ref == 1);
                        }
                        else
                        {
                            /* make sure the second buffer is freed */
                            EXPECT_TRUE(config_1.handle->next == nullptr);
                            buffer_2 = nullptr;
                        }
                    }

                    EXPECT_TRUE(config_1.handle->ref == 2);
                    config_1.handle = nullptr;

                    // Verify and release handles.
                    EXPECT_TRUE(ResetHandles());
                }
            }
        }
    }
}

/**
 *  Test PacketBuffer::ConsumeHead() function.
 *
 *  Description: For every buffer-configuration from inContext, create a
 *               buffer's instance according to the configuration. Next,
 *               for any value from sLengths[], pass it to the buffer's
 *               instance through ConsumeHead() method. Then, verify that
 *               the internal state of the buffer has been correctly
 *               adjusted according to the value passed into the method.
 */
STATIC_TEST(PacketBufferTest, CheckConsumeHead)
{
    for (auto & config : configurations)
    {
        for (size_t i = 0; i < ctx.length_count; ++i)
        {
            const uint16_t length = ctx.lengths[i];
            PrepareTestBuffer(&config, kRecordHandle | kAllowHandleReuse);

            config.handle->ConsumeHead(length);

            if (length > config.init_len)
            {
                EXPECT_TRUE(config.handle->payload == (config.payload_ptr + config.init_len));
                EXPECT_TRUE(config.handle->len == 0);
                EXPECT_TRUE(config.handle->tot_len == 0);
            }
            else
            {
                EXPECT_TRUE(config.handle->payload == (config.payload_ptr + length));
                EXPECT_TRUE(config.handle->len == (config.handle->len - length));
                EXPECT_TRUE(config.handle->tot_len == (config.handle->tot_len - length));
            }
        }
    }
}

/**
 *  Test PacketBuffer::Consume() function.
 *
 *  Description: Take two different initial configurations of PacketBuffer from
 *               inContext and create two PacketBuffer instances based on those
 *               configurations. Next, set both buffers' data length to any
 *               combination of values from sLengths[]  and link those buffers
 *               into a chain. Then, call Consume() on the first buffer in
 *               the chain with all values from sLengths[]. After calling the
 *               method, verify correctly adjusted the state of the first
 *               buffer and appropriate return pointer from the method's call.
 */
STATIC_TEST(PacketBufferTest, CheckConsume)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            // consume various amounts of memory
            for (size_t i = 0; i < ctx.length_count; ++i)
            {
                const uint16_t consumeLength = ctx.lengths[i];
                // start with various initial length for the first buffer
                for (size_t j = 0; j < ctx.length_count; ++j)
                {
                    const uint16_t len_1 = ctx.lengths[j];
                    // start with various initial length for the second buffer
                    for (size_t k = 0; k < ctx.length_count; ++k)
                    {
                        const uint16_t len_2 = ctx.lengths[k];

                        PrepareTestBuffer(&config_1);
                        PrepareTestBuffer(&config_2);
                        EXPECT_TRUE(config_1.handle->ref == 1);
                        EXPECT_TRUE(config_2.handle->ref == 1);

                        config_1.handle->AddToEnd(config_2.handle.Retain());

                        // Add various lengths to buffers
                        config_1.handle->SetDataLength(len_1, config_1.handle);
                        config_2.handle->SetDataLength(len_2, config_1.handle);

                        const uint16_t buf_1_len = config_1.handle->len;
                        const uint16_t buf_2_len = config_2.handle->len;

                        PacketBufferHandle original_handle_1 = config_1.handle.Retain();
                        EXPECT_TRUE(config_1.handle->ref == 2); // config_1.handle and original_handle_1
                        EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle->next

                        config_1.handle.Consume(consumeLength);

                        if (consumeLength == 0)
                        {
                            EXPECT_TRUE(config_1.handle == original_handle_1);
                            EXPECT_TRUE(config_1.handle->len == buf_1_len);
                            EXPECT_TRUE(config_2.handle->len == buf_2_len);
                            EXPECT_TRUE(config_1.handle->ref == 2); // config_1.handle and original_handle_1
                            EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle->next
                        }
                        else if (consumeLength < buf_1_len)
                        {
                            EXPECT_TRUE(config_1.handle == original_handle_1);
                            EXPECT_TRUE(config_1.handle->len == buf_1_len - consumeLength);
                            EXPECT_TRUE(config_2.handle->len == buf_2_len);
                            EXPECT_TRUE(config_1.handle->ref == 2); // config_1.handle and original_handle_1
                            EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle->next
                        }
                        else if ((consumeLength < buf_1_len + buf_2_len ||
                                  (consumeLength == buf_1_len + buf_2_len && buf_2_len == 0)))
                        {
                            EXPECT_TRUE(config_1.handle == config_2.handle);
                            EXPECT_TRUE(config_2.handle->len == buf_1_len + buf_2_len - consumeLength);
                            EXPECT_TRUE(original_handle_1->ref == 1); // original_handle_1
                            EXPECT_TRUE(config_2.handle->ref == 2);   // config_1.handle and config_2.handle
                        }
                        else
                        {
                            EXPECT_TRUE(config_1.handle.IsNull());
                            EXPECT_TRUE(original_handle_1->ref == 1); // original_handle_1
                            EXPECT_TRUE(config_2.handle->ref == 1);   // config_2.handle
                        }

                        original_handle_1 = nullptr;
                        config_1.handle   = nullptr;
                        config_2.handle   = nullptr;
                    }
                }
            }
        }
    }
}

/**
 *  Test PacketBuffer::EnsureReservedSize() function.
 *
 *  Description: For every buffer-configuration from inContext, create a
 *               buffer's instance according to the configuration. Next,
 *               manually specify how much space is reserved in the buffer.
 *               Then, verify that EnsureReservedSize() method correctly
 *               retrieves the amount of the reserved space.
 */
STATIC_TEST(PacketBufferTest, CheckEnsureReservedSize)
{
    for (auto & config : configurations)
    {
        for (size_t i = 0; i < ctx.length_count; ++i)
        {
            const uint16_t length = ctx.lengths[i];

            PrepareTestBuffer(&config, kRecordHandle | kAllowHandleReuse);
            const uint16_t kAllocSize = config.handle->AllocSize();
            uint16_t reserved_size    = config.reserved_size;

            if (PacketBuffer::kStructureSize + config.reserved_size > kAllocSize)
            {
                reserved_size = static_cast<uint16_t>(kAllocSize - PacketBuffer::kStructureSize);
            }

            if (length <= reserved_size)
            {
                EXPECT_TRUE(config.handle->EnsureReservedSize(length) == true);
                continue;
            }

            if ((length + config.init_len) > (kAllocSize - PacketBuffer::kStructureSize))
            {
                EXPECT_TRUE(config.handle->EnsureReservedSize(length) == false);
                continue;
            }

            EXPECT_TRUE(config.handle->EnsureReservedSize(length) == true);
            EXPECT_TRUE(config.handle->payload == (config.payload_ptr + length - reserved_size));
        }
    }
}

/**
 *  Test PacketBuffer::AlignPayload() function.
 *
 *  Description: For every buffer-configuration from inContext, create a
 *               buffer's instance according to the configuration. Next,
 *               manually specify how much space is reserved and the
 *               required payload shift. Then, verify that AlignPayload()
 *               method correctly aligns the payload start pointer.
 */
STATIC_TEST(PacketBufferTest, CheckAlignPayload)
{
    for (auto & config : configurations)
    {
        for (size_t n = 0; n < ctx.length_count; ++n)
        {
            PrepareTestBuffer(&config, kRecordHandle | kAllowHandleReuse);
            const uint16_t kAllocSize = config.handle->AllocSize();

            if (ctx.lengths[n] == 0)
            {
                EXPECT_TRUE(config.handle->AlignPayload(ctx.lengths[n]) == false);
                continue;
            }

            uint16_t reserved_size = config.reserved_size;
            if (config.reserved_size > kAllocSize)
            {
                reserved_size = kAllocSize;
            }

            const uint16_t payload_offset =
                static_cast<uint16_t>(reinterpret_cast<uintptr_t>(config.handle->Start()) % ctx.lengths[n]);
            uint16_t payload_shift = 0;
            if (payload_offset > 0)
                payload_shift = static_cast<uint16_t>(ctx.lengths[n] - payload_offset);

            if (payload_shift <= kAllocSize - reserved_size)
            {
                EXPECT_TRUE(config.handle->AlignPayload(ctx.lengths[n]) == true);
                EXPECT_TRUE(((unsigned long) config.handle->Start() % ctx.lengths[n]) == 0);
            }
            else
            {
                EXPECT_TRUE(config.handle->AlignPayload(ctx.lengths[n]) == false);
            }
        }
    }
}

/**
 *  Test PacketBuffer::Next() function.
 */
STATIC_TEST(PacketBufferTest, CheckNext)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            PrepareTestBuffer(&config_1, kRecordHandle | kAllowHandleReuse);
            PrepareTestBuffer(&config_2, kRecordHandle | kAllowHandleReuse);

            if (&config_1 != &config_2)
            {
                EXPECT_TRUE(config_1.handle->Next().IsNull());

                config_1.handle->AddToEnd(config_2.handle.Retain());

                EXPECT_TRUE(config_1.handle->Next() == config_2.handle);
                EXPECT_TRUE(config_1.handle->ChainedBuffer() == config_2.handle.Get());
            }
            else
            {
                EXPECT_TRUE(!config_1.handle->HasChainedBuffer());
            }

            EXPECT_TRUE(!config_2.handle->HasChainedBuffer());
        }
    }
}

/**
 *  Test PacketBuffer::Last() function.
 */
STATIC_TEST(PacketBufferTest, CheckLast)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            for (auto & config_3 : configurations)
            {
                if (&config_1 == &config_2 || &config_1 == &config_3 || &config_2 == &config_3)
                {
                    continue;
                }

                PrepareTestBuffer(&config_1);
                PrepareTestBuffer(&config_2);
                PrepareTestBuffer(&config_3);

                EXPECT_TRUE(config_1.handle->Last() == config_1.handle);
                EXPECT_TRUE(config_2.handle->Last() == config_2.handle);
                EXPECT_TRUE(config_3.handle->Last() == config_3.handle);

                config_1.handle->AddToEnd(config_2.handle.Retain());

                EXPECT_TRUE(config_1.handle->Last() == config_2.handle);
                EXPECT_TRUE(config_2.handle->Last() == config_2.handle);
                EXPECT_TRUE(config_3.handle->Last() == config_3.handle);

                config_1.handle->AddToEnd(config_3.handle.Retain());

                EXPECT_TRUE(config_1.handle->Last() == config_3.handle);
                EXPECT_TRUE(config_2.handle->Last() == config_3.handle);
                EXPECT_TRUE(config_3.handle->Last() == config_3.handle);

                config_1.handle = nullptr;
                config_2.handle = nullptr;
                config_3.handle = nullptr;
            }
        }
    }
}

/**
 *  Test PacketBuffer::Read() function.
 */
STATIC_TEST(PacketBufferTest, CheckRead)
{
    uint8_t payloads[2 * kBlockSize] = { 1 };
    uint8_t result[2 * kBlockSize];
    for (size_t i = 1; i < sizeof(payloads); ++i)
    {
        payloads[i] = static_cast<uint8_t>(random());
    }

    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            PrepareTestBuffer(&config_1, kAllowHandleReuse);
            PrepareTestBuffer(&config_2, kAllowHandleReuse);

            const uint16_t length_1     = config_1.handle->MaxDataLength();
            const uint16_t length_2     = config_2.handle->MaxDataLength();
            const size_t length_sum     = length_1 + length_2;
            const uint16_t length_total = static_cast<uint16_t>(length_sum);
            EXPECT_TRUE(length_total == length_sum);

            memcpy(config_1.handle->Start(), payloads, length_1);
            memcpy(config_2.handle->Start(), payloads + length_1, length_2);
            config_1.handle->SetDataLength(length_1);
            config_2.handle->SetDataLength(length_2);
            config_1.handle->AddToEnd(config_2.handle.Retain());
            EXPECT_TRUE(config_1.handle->TotalLength() == length_total);

            if (length_1 >= 1)
            {
                // Check a read that does not span packet buffers.
                CHIP_ERROR err = config_1.handle->Read(result, 1);
                EXPECT_TRUE(err == CHIP_NO_ERROR);
                EXPECT_TRUE(result[0] == payloads[0]);
            }

            // Check a read that spans packet buffers.
            CHIP_ERROR err = config_1.handle->Read(result, length_total);
            EXPECT_TRUE(err == CHIP_NO_ERROR);
            EXPECT_TRUE(memcmp(payloads, result, length_total) == 0);

            // Check a read that is too long fails.
            err = config_1.handle->Read(result, length_total + 1);
            EXPECT_TRUE(err == CHIP_ERROR_BUFFER_TOO_SMALL);

            // Check that running off the end of a corrupt buffer chain is detected.
            if (length_total < UINT16_MAX)
            {
                // First case: TotalLength() is wrong.
                config_1.handle->tot_len = static_cast<uint16_t>(config_1.handle->tot_len + 1);
                err                      = config_1.handle->Read(result, length_total + 1);
                EXPECT_TRUE(err == CHIP_ERROR_INTERNAL);
                config_1.handle->tot_len = static_cast<uint16_t>(config_1.handle->tot_len - 1);
            }
            if (length_1 >= 1)
            {
                // Second case: an individual buffer's DataLength() is wrong.
                config_1.handle->len = static_cast<uint16_t>(config_1.handle->len - 1);
                err                  = config_1.handle->Read(result, length_total);
                EXPECT_TRUE(err == CHIP_ERROR_INTERNAL);
                config_1.handle->len = static_cast<uint16_t>(config_1.handle->len + 1);
            }

            config_1.handle = nullptr;
            config_2.handle = nullptr;
        }
    }
}

/**
 *  Test PacketBuffer::AddRef() function.
 */
STATIC_TEST(PacketBufferTest, CheckAddRef)
{
    for (auto & config : configurations)
    {
        PrepareTestBuffer(&config, kRecordHandle);
        const auto refs = config.handle->ref;
        config.handle->AddRef();
        EXPECT_TRUE(config.handle->ref == refs + 1);
        config.handle->ref = refs; // Don't leak buffers.
    }
}

/**
 *  Test PacketBuffer::Free() function.
 *
 *  Description: Take two different initial configurations of PacketBuffer from
 *               inContext and create two PacketBuffer instances based on those
 *               configurations. Next, chain two buffers together and set each
 *               buffer's reference count to one of the values from
 *               init_ret_count[]. Then, call Free() on the first buffer in
 *               the chain and verify correctly adjusted states of the two
 *               buffers.
 */
STATIC_TEST(PacketBufferTest, CheckFree)
{
    const decltype(PacketBuffer::ref) init_ref_count[] = { 1, 2, 3 };
    constexpr size_t kRefs                             = sizeof(init_ref_count) / sizeof(init_ref_count[0]);

    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            // start with various buffer ref counts
            for (size_t r = 0; r < kRefs; r++)
            {
                config_1.handle = PacketBufferHandle::New(chip::System::PacketBuffer::kMaxSizeWithoutReserve, 0);
                config_2.handle = PacketBufferHandle::New(chip::System::PacketBuffer::kMaxSizeWithoutReserve, 0);
                EXPECT_TRUE(!config_1.handle.IsNull());
                EXPECT_TRUE(!config_2.handle.IsNull());

                PrepareTestBuffer(&config_1, kAllowHandleReuse);
                PrepareTestBuffer(&config_2, kAllowHandleReuse);
                EXPECT_TRUE(config_1.handle->ref == 1);
                EXPECT_TRUE(config_2.handle->ref == 1);

                // Chain buffers.
                config_1.handle->next = config_2.handle.Get();

                // Add various buffer ref counts.
                const auto initial_refs_1 = config_1.handle->ref = init_ref_count[r];
                const auto initial_refs_2 = config_2.handle->ref = init_ref_count[(r + 1) % kRefs];

                // Free head.
                PacketBuffer::Free(config_1.handle.mBuffer);
                if (initial_refs_1 == 1)
                {
                    config_1.handle.mBuffer = nullptr;
                }

                // Verification.
                if (initial_refs_1 > 1)
                {
                    // Verify that head ref count is decremented.
                    EXPECT_TRUE(config_1.handle->ref == initial_refs_1 - 1);
                    // Verify that chain is maintained.
                    EXPECT_TRUE(config_1.handle->next == config_2.handle.Get());
                    // Verify that chained buffer ref count has not changed.
                    EXPECT_TRUE(config_2.handle->ref == initial_refs_2);
                }
                else
                {
                    if (initial_refs_2 > 1)
                    {
                        // Verify that chained buffer ref count is decremented.
                        EXPECT_TRUE(config_2.handle->ref == initial_refs_2 - 1);
                    }
                    else
                    {
                        // Since the test used fake ref counts, config_2.handle now points
                        // to a freed buffer; clear the handle's internal pointer.
                        config_2.handle.mBuffer = nullptr;
                    }
                }

                // Clean up.
                if (!config_1.handle.IsNull())
                {
                    config_1.handle->next = nullptr;
                    config_1.handle->ref  = 1;
                    config_1.handle       = nullptr;
                }
                if (!config_2.handle.IsNull())
                {
                    config_2.handle->ref = 1;
                    config_2.handle      = nullptr;
                }
            }
        }
    }
}

/**
 *  Test PacketBuffer::FreeHead() function.
 *
 *  Description: Take two different initial configurations of PacketBuffer from
 *               inContext and create two PacketBuffer instances based on those
 *               configurations. Next, chain two buffers together. Then, call
 *               FreeHead() on the first buffer in the chain and verify that
 *               the method returned pointer to the second buffer.
 */
STATIC_TEST(PacketBufferTest, CheckFreeHead)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            // Test PacketBuffer::FreeHead

            PrepareTestBuffer(&config_1, kAllowHandleReuse);
            PrepareTestBuffer(&config_2, kAllowHandleReuse);
            EXPECT_TRUE(config_1.handle->ref == 1);
            EXPECT_TRUE(config_2.handle->ref == 1);

            PacketBufferHandle handle_1 = config_1.handle.Retain();
            config_1.handle->AddToEnd(config_2.handle.Retain());
            EXPECT_TRUE(config_1.handle->ref == 2);
            EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle->next

            PacketBuffer * const returned = PacketBuffer::FreeHead(std::move(config_1.handle).UnsafeRelease());

            EXPECT_TRUE(handle_1->ref == 1);
            EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and returned
            EXPECT_TRUE(returned == config_2.handle.Get());

            config_1.handle = nullptr;
            EXPECT_TRUE(config_2.handle->ref == 2);
            config_2.handle = nullptr;
            EXPECT_TRUE(returned->ref == 1);
            PacketBuffer::Free(returned);

            // Test PacketBufferHandle::FreeHead

            PrepareTestBuffer(&config_1, kAllowHandleReuse);
            PrepareTestBuffer(&config_2, kAllowHandleReuse);
            EXPECT_TRUE(config_1.handle->ref == 1);
            EXPECT_TRUE(config_2.handle->ref == 1);

            handle_1 = config_1.handle.Retain();
            config_1.handle->AddToEnd(config_2.handle.Retain());
            EXPECT_TRUE(config_1.handle->ref == 2);
            EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle->next

            PacketBuffer * const buffer_1 = config_1.handle.Get();

            config_1.handle.FreeHead();

            EXPECT_TRUE(buffer_1->ref == 1);
            EXPECT_TRUE(config_1.handle == config_2.handle);
            EXPECT_TRUE(config_2.handle->ref == 2); // config_2.handle and config_1.handle

            config_1.handle = nullptr;
            config_2.handle = nullptr;
        }
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleConstruct)
{
    PacketBufferHandle handle_1;
    EXPECT_TRUE(handle_1.IsNull());

    PacketBufferHandle handle_2(nullptr);
    EXPECT_TRUE(handle_2.IsNull());

    PacketBufferHandle handle_3(PacketBufferHandle::New(chip::System::PacketBuffer::kMaxSize));
    EXPECT_TRUE(!handle_3.IsNull());

    // Private constructor.
    PacketBuffer * const buffer_3 = std::move(handle_3).UnsafeRelease();
    PacketBufferHandle handle_4(buffer_3);
    EXPECT_TRUE(handle_4.Get() == buffer_3);
}

STATIC_TEST(PacketBufferTest, CheckHandleMove)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            PrepareTestBuffer(&config_1, kRecordHandle);
            PrepareTestBuffer(&config_2, kRecordHandle);

            const PacketBuffer * const buffer_1 = config_1.handle.Get();
            const PacketBuffer * const buffer_2 = config_2.handle.Get();
            EXPECT_TRUE(buffer_1 != buffer_2);
            EXPECT_TRUE(buffer_1->ref == 2); // test.handles and config_1.handle
            EXPECT_TRUE(buffer_2->ref == 2); // test.handles and config_2.handle

            config_1.handle = std::move(config_2.handle);
            EXPECT_TRUE(config_1.handle.Get() == buffer_2);
            EXPECT_TRUE(config_2.handle.Get() == nullptr);
            EXPECT_TRUE(buffer_1->ref == 1); // test.handles
            EXPECT_TRUE(buffer_2->ref == 2); // test.handles and config_1.handle

            config_1.handle = nullptr;
        }
        // Verify and release handles.
        EXPECT_TRUE(ResetHandles());
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleRelease)
{
    for (auto & config_1 : configurations)
    {
        PrepareTestBuffer(&config_1);

        PacketBuffer * const buffer_1 = config_1.handle.Get();
        PacketBuffer * const taken_1  = std::move(config_1.handle).UnsafeRelease();

        EXPECT_TRUE(buffer_1 == taken_1);
        EXPECT_TRUE(config_1.handle.IsNull());
        EXPECT_TRUE(buffer_1->ref == 1);
        PacketBuffer::Free(buffer_1);
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleFree)
{
    for (auto & config_1 : configurations)
    {
        PrepareTestBuffer(&config_1, kRecordHandle);

        const PacketBuffer * const buffer_1 = config_1.handle.Get();
        EXPECT_TRUE(buffer_1->ref == 2); // test.handles and config_1.handle

        config_1.handle = nullptr;
        EXPECT_TRUE(config_1.handle.IsNull());
        EXPECT_TRUE(config_1.handle.Get() == nullptr);
        EXPECT_TRUE(buffer_1->ref == 1); // test.handles only
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleRetain)
{
    for (auto & config_1 : configurations)
    {
        PrepareTestBuffer(&config_1, kRecordHandle);

        EXPECT_TRUE(config_1.handle->ref == 2); // test.handles and config_1.handle

        PacketBufferHandle handle_1 = config_1.handle.Retain();

        EXPECT_TRUE(config_1.handle == handle_1);
        EXPECT_TRUE(config_1.handle->ref == 3); // test.handles and config_1.handle and handle_1
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleAdopt)
{
    for (auto & config_1 : configurations)
    {
        PrepareTestBuffer(&config_1, kRecordHandle);
        PacketBuffer * buffer_1 = std::move(config_1.handle).UnsafeRelease();

        EXPECT_TRUE(config_1.handle.IsNull());
        EXPECT_TRUE(buffer_1->ref == 2); // test.handles and buffer_1

        config_1.handle = PacketBufferHandle::Adopt(buffer_1);
        EXPECT_TRUE(config_1.handle.Get() == buffer_1);
        EXPECT_TRUE(config_1.handle->ref == 2); // test.handles and config_1.handle

        config_1.handle = nullptr;
        EXPECT_TRUE(config_1.handle.IsNull());
        EXPECT_TRUE(buffer_1->ref == 1); // test.handles only
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleHold)
{
    for (auto & config_1 : configurations)
    {
        PrepareTestBuffer(&config_1, kRecordHandle);
        PacketBuffer * buffer_1 = std::move(config_1.handle).UnsafeRelease();

        EXPECT_TRUE(config_1.handle.IsNull());
        EXPECT_TRUE(buffer_1->ref == 2); // test.handles and buffer_1

        config_1.handle = PacketBufferHandle::Hold(buffer_1);
        EXPECT_TRUE(config_1.handle.Get() == buffer_1);
        EXPECT_TRUE(config_1.handle->ref == 3); // test.handles and config_1.handle and buffer_1

        config_1.handle = nullptr;
        EXPECT_TRUE(config_1.handle.IsNull());
        EXPECT_TRUE(buffer_1->ref == 2); // test.handles only and buffer_1

        PacketBuffer::Free(buffer_1);
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleAdvance)
{
    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            for (auto & config_3 : configurations)
            {
                if (&config_1 == &config_2 || &config_1 == &config_3 || &config_2 == &config_3)
                {
                    continue;
                }

                PrepareTestBuffer(&config_1);
                PrepareTestBuffer(&config_2);
                PrepareTestBuffer(&config_3);

                PacketBufferHandle handle_1 = config_1.handle.Retain();
                PacketBufferHandle handle_2 = config_2.handle.Retain();
                PacketBufferHandle handle_3 = config_3.handle.Retain();

                config_1.handle->AddToEnd(config_2.handle.Retain());
                config_1.handle->AddToEnd(config_3.handle.Retain());

                EXPECT_TRUE(config_1.handle->ChainedBuffer() == config_2.handle.Get());
                EXPECT_TRUE(config_2.handle->ChainedBuffer() == config_3.handle.Get());
                EXPECT_TRUE(config_3.handle->HasChainedBuffer() == false);
                EXPECT_TRUE(handle_1->ref == 2); // handle_1 and config_1.handle
                EXPECT_TRUE(handle_2->ref == 3); // handle_2 and config_2.handle and config_1.handle->next
                EXPECT_TRUE(handle_3->ref == 3); // handle_3 and config_3.handle and config_2.handle->next

                config_1.handle.Advance();

                EXPECT_TRUE(config_1.handle == handle_2);
                EXPECT_TRUE(handle_1->ref == 1); // handle_1 only
                EXPECT_TRUE(handle_2->ref == 4); // handle_2, config_[12].handle, handle_1->next
                EXPECT_TRUE(handle_3->ref == 3); // handle_3, config_3.handle, config_2.handle->next

                config_1.handle.Advance();

                EXPECT_TRUE(config_1.handle == handle_3);
                EXPECT_TRUE(handle_1->ref == 1); // handle_1 only
                EXPECT_TRUE(handle_2->ref == 3); // handle_2, config_2.handle, handle_1->next
                EXPECT_TRUE(handle_3->ref == 4); // handle_3, config_[13].handle, handle_2->next

                config_1.handle = nullptr;
                config_2.handle = nullptr;
                config_3.handle = nullptr;
            }
        }
    }
}

STATIC_TEST(PacketBufferTest, CheckHandleRightSize)
{
    const char kPayload[]     = "Joy!";
    PacketBufferHandle handle = PacketBufferHandle::New(chip::System::PacketBuffer::kMaxSizeWithoutReserve, 0);
    PacketBuffer * buffer     = handle.mBuffer;

    memcpy(handle->Start(), kPayload, sizeof kPayload);
    buffer->SetDataLength(sizeof kPayload);
    EXPECT_TRUE(handle->ref == 1);

    // RightSize should do nothing if there is another reference to the buffer.
    {
        PacketBufferHandle anotherHandle = handle.Retain();
        handle.RightSize();
        EXPECT_TRUE(handle.mBuffer == buffer);
    }

#if CHIP_SYSTEM_PACKETBUFFER_HAS_RIGHTSIZE

    handle.RightSize();
    EXPECT_TRUE(handle.mBuffer != buffer);
    EXPECT_TRUE(handle->DataLength() == sizeof kPayload);
    EXPECT_TRUE(memcmp(handle->Start(), kPayload, sizeof kPayload) == 0);

#else // CHIP_SYSTEM_PACKETBUFFER_HAS_RIGHTSIZE

    // For this configuration, RightSize() does nothing.
    handle.RightSize();
    EXPECT_TRUE(handle.mBuffer == buffer);

#endif // CHIP_SYSTEM_PACKETBUFFER_HAS_RIGHTSIZE
}

STATIC_TEST(PacketBufferTest, CheckHandleCloneData)
{
    uint8_t lPayload[2 * PacketBuffer::kMaxSizeWithoutReserve];
    for (uint8_t & payload : lPayload)
    {
        payload = static_cast<uint8_t>(random());
    }

    for (auto & config_1 : configurations)
    {
        for (auto & config_2 : configurations)
        {
            if (&config_1 == &config_2)
            {
                continue;
            }

            PrepareTestBuffer(&config_1);
            PrepareTestBuffer(&config_2);

            const uint8_t * payload_1 = lPayload;
            memcpy(config_1.handle->Start(), payload_1, config_1.handle->MaxDataLength());
            config_1.handle->SetDataLength(config_1.handle->MaxDataLength());

            const uint8_t * payload_2 = lPayload + config_1.handle->MaxDataLength();
            memcpy(config_2.handle->Start(), payload_2, config_2.handle->MaxDataLength());
            config_2.handle->SetDataLength(config_2.handle->MaxDataLength());

            // Clone single buffer.
            PacketBufferHandle clone_1 = config_1.handle.CloneData();
            EXPECT_TRUE(!clone_1.IsNull());
            EXPECT_TRUE(clone_1->DataLength() == config_1.handle->DataLength());
            EXPECT_TRUE(memcmp(clone_1->Start(), payload_1, clone_1->DataLength()) == 0);
            if (clone_1->DataLength())
            {
                // Verify that modifying the clone does not affect the original.
                ScrambleData(clone_1->Start(), clone_1->DataLength());
                EXPECT_TRUE(memcmp(clone_1->Start(), payload_1, clone_1->DataLength()) != 0);
                EXPECT_TRUE(memcmp(config_1.handle->Start(), payload_1, config_1.handle->DataLength()) == 0);
            }

            // Clone buffer chain.
            config_1.handle->AddToEnd(config_2.handle.Retain());
            EXPECT_TRUE(config_1.handle->HasChainedBuffer());
            clone_1                         = config_1.handle.CloneData();
            PacketBufferHandle clone_1_next = clone_1->Next();
            EXPECT_TRUE(!clone_1.IsNull());
            EXPECT_TRUE(clone_1->HasChainedBuffer());
            EXPECT_TRUE(clone_1->DataLength() == config_1.handle->DataLength());
            EXPECT_TRUE(clone_1->TotalLength() == config_1.handle->TotalLength());
            EXPECT_TRUE(clone_1_next->DataLength() == config_2.handle->DataLength());
            EXPECT_TRUE(memcmp(clone_1->Start(), payload_1, clone_1->DataLength()) == 0);
            EXPECT_TRUE(memcmp(clone_1_next->Start(), payload_2, clone_1_next->DataLength()) == 0);
            if (clone_1->DataLength())
            {
                ScrambleData(clone_1->Start(), clone_1->DataLength());
                EXPECT_TRUE(memcmp(clone_1->Start(), payload_1, clone_1->DataLength()) != 0);
                EXPECT_TRUE(memcmp(config_1.handle->Start(), payload_1, config_1.handle->DataLength()) == 0);
            }
            if (clone_1_next->DataLength())
            {
                ScrambleData(clone_1_next->Start(), clone_1_next->DataLength());
                EXPECT_TRUE(memcmp(clone_1_next->Start(), payload_2, clone_1_next->DataLength()) != 0);
                EXPECT_TRUE(memcmp(config_2.handle->Start(), payload_2, config_2.handle->DataLength()) == 0);
            }

            config_1.handle = nullptr;
            config_2.handle = nullptr;
        }
    }

#if CHIP_SYSTEM_PACKETBUFFER_FROM_CHIP_HEAP

    // It is possible for a packet buffer allocation to return a larger block than requested (e.g. when using a shared pool)
    // and in particular to return a larger block than it is possible to request from PackBufferHandle::New().
    // In that case, (a) it is incorrect to actually use the extra space, and (b) if it is not used, the clone will
    // be the maximum possible size.
    //
    // This is only testable on heap allocation configurations, where pbuf records the allocation size and we can manually
    // construct an oversize buffer.

    constexpr uint16_t kOversizeDataSize = PacketBuffer::kMaxSizeWithoutReserve + 99;
    PacketBuffer * p =
        reinterpret_cast<PacketBuffer *>(chip::Platform::MemoryAlloc(PacketBuffer::kStructureSize + kOversizeDataSize));
    EXPECT_TRUE(p != nullptr);

    p->next       = nullptr;
    p->payload    = reinterpret_cast<uint8_t *>(p) + PacketBuffer::kStructureSize;
    p->tot_len    = 0;
    p->len        = 0;
    p->ref        = 1;
    p->alloc_size = kOversizeDataSize;

    PacketBufferHandle handle = PacketBufferHandle::Adopt(p);

    // Fill the buffer to maximum and verify that it can be cloned.

    memset(handle->Start(), 1, PacketBuffer::kMaxSizeWithoutReserve);
    handle->SetDataLength(PacketBuffer::kMaxSizeWithoutReserve);
    EXPECT_TRUE(handle->DataLength() == PacketBuffer::kMaxSizeWithoutReserve);

    PacketBufferHandle clone = handle.CloneData();
    EXPECT_TRUE(!clone.IsNull());
    EXPECT_TRUE(clone->DataLength() == PacketBuffer::kMaxSizeWithoutReserve);
    EXPECT_TRUE(memcmp(handle->Start(), clone->Start(), PacketBuffer::kMaxSizeWithoutReserve) == 0);

    // Overfill the buffer and verify that it can not be cloned.
    memset(handle->Start(), 2, kOversizeDataSize);
    handle->SetDataLength(kOversizeDataSize);
    EXPECT_TRUE(handle->DataLength() == kOversizeDataSize);

    clone = handle.CloneData();
    EXPECT_TRUE(clone.IsNull());

    // Free the packet buffer memory ourselves, since we allocated it ourselves.
    chip::Platform::MemoryFree(std::move(handle).UnsafeRelease());

#endif // CHIP_SYSTEM_PACKETBUFFER_FROM_CHIP_HEAP
}

STATIC_TEST(PacketBufferTest, CheckPacketBufferWriter)
{
    const char kPayload[] = "Hello, world!";

    PacketBufferWriter yay(PacketBufferHandle::New(sizeof(kPayload)));
    PacketBufferWriter nay(PacketBufferHandle::New(sizeof(kPayload)), sizeof(kPayload) - 2);
    EXPECT_TRUE(!yay.IsNull());
    EXPECT_TRUE(!nay.IsNull());

    yay.Put(kPayload);
    yay.Put('\0');
    nay.Put(kPayload);
    nay.Put('\0');
    EXPECT_TRUE(yay.Fit());
    EXPECT_TRUE(!nay.Fit());

    PacketBufferHandle yayBuffer = yay.Finalize();
    PacketBufferHandle nayBuffer = nay.Finalize();
    EXPECT_TRUE(yay.IsNull());
    EXPECT_TRUE(nay.IsNull());
    EXPECT_TRUE(!yayBuffer.IsNull());
    EXPECT_TRUE(nayBuffer.IsNull());
    EXPECT_TRUE(memcmp(yayBuffer->Start(), kPayload, sizeof kPayload) == 0);
}