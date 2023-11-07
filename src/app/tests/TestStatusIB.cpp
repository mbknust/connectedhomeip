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

#include <app/AppConfig.h>
#include <app/MessageDef/StatusIB.h>
#include <lib/core/CHIPConfig.h>
#include <lib/core/CHIPError.h>
#include <lib/core/ErrorStr.h>
#include <lib/support/CHIPMem.h>

#include <gtest/gtest.h>

namespace {

using namespace chip;
using namespace chip::app;
using namespace chip::Protocols::InteractionModel;

// Macro so failures will blame the right line.
#define VERIFY_ROUNDTRIP(err, status)                                                                                              \
    do                                                                                                                             \
    {                                                                                                                              \
        StatusIB newStatus;                                                                                                        \
        newStatus.InitFromChipError(err);                                                                                          \
        EXPECT_TRUE(newStatus.mStatus == status.mStatus);                                                                          \
        EXPECT_TRUE(newStatus.mClusterStatus == status.mClusterStatus);                                                            \
    } while (0);

class TestStatusIB : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR);

        // Hand-register the error formatter.  Normally it's registered by
        // InteractionModelEngine::Init, but we don't want to mess with that here.
        StatusIB::RegisterErrorFormatter();
    }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

TEST_F(TestStatusIB, TestStatusIBToFromChipError)
{
    StatusIB status;

    status.mStatus = Status::Success;
    CHIP_ERROR err = status.ToChipError();
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    VERIFY_ROUNDTRIP(err, status);

    status.mStatus = Status::Failure;
    err            = status.ToChipError();
    EXPECT_TRUE(err != CHIP_NO_ERROR);
    VERIFY_ROUNDTRIP(err, status);

    status.mStatus = Status::InvalidAction;
    err            = status.ToChipError();
    EXPECT_TRUE(err != CHIP_NO_ERROR);
    VERIFY_ROUNDTRIP(err, status);

    status.mClusterStatus = MakeOptional(static_cast<ClusterStatus>(5));

    status.mStatus = Status::Success;
    err            = status.ToChipError();
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    status.mStatus = Status::Failure;
    err            = status.ToChipError();
    EXPECT_TRUE(err != CHIP_NO_ERROR);
    VERIFY_ROUNDTRIP(err, status);

    status.mStatus = Status::InvalidAction;
    err            = status.ToChipError();
    EXPECT_TRUE(err != CHIP_NO_ERROR);
    {
        StatusIB newStatus;
        newStatus.InitFromChipError(err);
        EXPECT_TRUE(newStatus.mStatus == Status::Failure);
        EXPECT_TRUE(newStatus.mClusterStatus == status.mClusterStatus);
    }

    err = CHIP_ERROR_NO_MEMORY;
    {
        StatusIB newStatus;
        newStatus.InitFromChipError(err);
        EXPECT_TRUE(newStatus.mStatus == Status::Failure);
        EXPECT_TRUE(!newStatus.mClusterStatus.HasValue());
    }
}

#if !CHIP_CONFIG_SHORT_ERROR_STR
TEST_F(TestStatusIB, TestStatusIBErrorToString)
{
    StatusIB status;
    status.mStatus   = Status::InvalidAction;
    CHIP_ERROR err   = status.ToChipError();
    const char * str = ErrorStr(err);

#if CHIP_CONFIG_IM_STATUS_CODE_VERBOSE_FORMAT
    EXPECT_TRUE(strcmp(str, "IM Error 0x00000580: General error: 0x80 (INVALID_ACTION)") == 0);
#else  // CHIP_CONFIG_IM_STATUS_CODE_VERBOSE_FORMAT
    EXPECT_TRUE(strcmp(str, "IM Error 0x00000580: General error: 0x80") == 0);
#endif // CHIP_CONFIG_IM_STATUS_CODE_VERBOSE_FORMAT

    status.mStatus        = Status::Failure;
    status.mClusterStatus = MakeOptional(static_cast<ClusterStatus>(5));
    err                   = status.ToChipError();
    str                   = ErrorStr(err);
    EXPECT_TRUE(strcmp(str, "IM Error 0x00000605: Cluster-specific error: 0x05") == 0);
}
#endif // !CHIP_CONFIG_SHORT_ERROR_STR
} // namespace