/*
 *
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

#include <app/clusters/operational-state-server/operational-state-cluster-objects.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>

#include <gtest/gtest.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters::OperationalState;

namespace {

class TestOperationalDeviceStateClusterObjects : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalStateConstructorWithOnlyStateID)
{
    using namespace chip::app::Clusters::OperationalState;
    // General state: Stopped
    GenericOperationalState operationalStateStopped(to_underlying(OperationalStateEnum::kStopped));
    EXPECT_TRUE(operationalStateStopped.operationalStateID == to_underlying(OperationalStateEnum::kStopped));
    EXPECT_TRUE(operationalStateStopped.operationalStateLabel.HasValue() == false);

    // General state: Running
    GenericOperationalState operationalStateRunning(to_underlying(OperationalStateEnum::kRunning));
    EXPECT_TRUE(operationalStateRunning.operationalStateID == to_underlying(OperationalStateEnum::kRunning));
    EXPECT_TRUE(operationalStateRunning.operationalStateLabel.HasValue() == false);

    // General state: Paused
    GenericOperationalState operationalStatePaused(to_underlying(OperationalStateEnum::kPaused));
    EXPECT_TRUE(operationalStatePaused.operationalStateID == to_underlying(OperationalStateEnum::kPaused));
    EXPECT_TRUE(operationalStatePaused.operationalStateLabel.HasValue() == false);

    // General state: Error
    GenericOperationalState operationalStateError(to_underlying(OperationalStateEnum::kError));
    EXPECT_TRUE(operationalStateError.operationalStateID == to_underlying(OperationalStateEnum::kError));
    EXPECT_TRUE(operationalStateError.operationalStateLabel.HasValue() == false);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalStateConstructorWithStateIDAndStateLabel)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalStateEnum : uint8_t
    {
        kRebooting = 0x81,
    };

    char buffer[kOperationalStateLabelMaxSize] = "rebooting";

    // ManufacturerStates state, label len = 9:
    GenericOperationalState operationalState(to_underlying(ManufactureOperationalStateEnum::kRebooting),
                                             Optional<CharSpan>(CharSpan::fromCharString(buffer)));

    EXPECT_TRUE(operationalState.operationalStateID == to_underlying(ManufactureOperationalStateEnum::kRebooting));
    EXPECT_TRUE(operationalState.operationalStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalState.operationalStateLabel.Value().size() == strlen(buffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalState.operationalStateLabel.Value().data()), buffer, strlen(buffer)) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalStateCopyConstructor)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalStateEnum : uint8_t
    {
        kRebooting = 0x81,
    };

    char buffer[kOperationalStateLabelMaxSize] = "rebooting";

    GenericOperationalState srcOperationalState(to_underlying(ManufactureOperationalStateEnum::kRebooting),
                                                Optional<CharSpan>(CharSpan::fromCharString(buffer)));

    GenericOperationalState desOperationalState(srcOperationalState);

    EXPECT_TRUE(desOperationalState.operationalStateID == srcOperationalState.operationalStateID);
    EXPECT_TRUE(desOperationalState.operationalStateLabel.HasValue() == true);
    EXPECT_TRUE(desOperationalState.operationalStateLabel.Value().size() ==
                srcOperationalState.operationalStateLabel.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(desOperationalState.operationalStateLabel.Value().data()),
                       const_cast<char *>(srcOperationalState.operationalStateLabel.Value().data()),
                       desOperationalState.operationalStateLabel.Value().size()) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalStateCopyAssignment)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalStateEnum : uint8_t
    {
        kRebooting = 0x81,
    };

    char buffer[kOperationalStateLabelMaxSize] = "rebooting";

    GenericOperationalState srcOperationalState(to_underlying(ManufactureOperationalStateEnum::kRebooting),
                                                Optional<CharSpan>(CharSpan::fromCharString(buffer)));

    GenericOperationalState desOperationalState = srcOperationalState;

    EXPECT_TRUE(desOperationalState.operationalStateID == srcOperationalState.operationalStateID);
    EXPECT_TRUE(desOperationalState.operationalStateLabel.HasValue() == true);
    EXPECT_TRUE(desOperationalState.operationalStateLabel.Value().size() ==
                srcOperationalState.operationalStateLabel.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(desOperationalState.operationalStateLabel.Value().data()),
                       const_cast<char *>(srcOperationalState.operationalStateLabel.Value().data()),
                       desOperationalState.operationalStateLabel.Value().size()) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalStateFuncSet)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalStateEnum : uint8_t
    {
        kRebooting = 0x81,
    };

    char buffer[kOperationalStateLabelMaxSize] = "rebooting";

    // init state
    GenericOperationalState operationalState(to_underlying(ManufactureOperationalStateEnum::kRebooting),
                                             Optional<CharSpan>(CharSpan::fromCharString(buffer)));

    // change state without label
    operationalState.Set(to_underlying(OperationalStateEnum::kStopped));
    EXPECT_TRUE(operationalState.operationalStateID == to_underlying(OperationalStateEnum::kStopped));
    EXPECT_TRUE(operationalState.operationalStateLabel.HasValue() == false);

    // change state with label
    operationalState.Set(to_underlying(ManufactureOperationalStateEnum::kRebooting),
                         Optional<CharSpan>(CharSpan::fromCharString(buffer)));
    EXPECT_TRUE(operationalState.operationalStateID == to_underlying(ManufactureOperationalStateEnum::kRebooting));
    EXPECT_TRUE(operationalState.operationalStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalState.operationalStateLabel.Value().size() == strlen(buffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalState.operationalStateLabel.Value().data()), buffer, strlen(buffer)) == 0);

    // change state with label, label len = kOperationalStateLabelMaxSize
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        buffer[i] = 1;
    }
    operationalState.Set(to_underlying(ManufactureOperationalStateEnum::kRebooting),
                         Optional<CharSpan>(CharSpan(buffer, sizeof(buffer))));
    EXPECT_TRUE(operationalState.operationalStateID == to_underlying(ManufactureOperationalStateEnum::kRebooting));
    EXPECT_TRUE(operationalState.operationalStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalState.operationalStateLabel.Value().size() == sizeof(buffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalState.operationalStateLabel.Value().data()), buffer, sizeof(buffer)) == 0);

    // change state with label, label len larger than kOperationalStateLabelMaxSize
    char buffer2[kOperationalStateLabelMaxSize + 1];

    for (size_t i = 0; i < sizeof(buffer2); i++)
    {
        buffer2[i] = 1;
    }
    operationalState.Set(to_underlying(ManufactureOperationalStateEnum::kRebooting),
                         Optional<CharSpan>(CharSpan(buffer2, sizeof(buffer2))));
    EXPECT_TRUE(operationalState.operationalStateID == to_underlying(ManufactureOperationalStateEnum::kRebooting));
    EXPECT_TRUE(operationalState.operationalStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalState.operationalStateLabel.Value().size() == kOperationalStateLabelMaxSize);
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalState.operationalStateLabel.Value().data()), buffer2,
                       kOperationalStateLabelMaxSize) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalErrorConstructorWithOnlyStateID)
{
    using namespace chip::app::Clusters::OperationalState;
    // General errors: NoError
    GenericOperationalError operationalErrorNoErr(to_underlying(ErrorStateEnum::kNoError));

    EXPECT_TRUE(operationalErrorNoErr.errorStateID == to_underlying(ErrorStateEnum::kNoError));
    EXPECT_TRUE(operationalErrorNoErr.errorStateLabel.HasValue() == false);
    EXPECT_TRUE(operationalErrorNoErr.errorStateDetails.HasValue() == false);

    // General errors: UnableToStartOrResume
    GenericOperationalError operationalErrorUnableToStartOrResume(to_underlying(ErrorStateEnum::kUnableToStartOrResume));

    EXPECT_TRUE(operationalErrorUnableToStartOrResume.errorStateID == to_underlying(ErrorStateEnum::kUnableToStartOrResume));
    EXPECT_TRUE(operationalErrorUnableToStartOrResume.errorStateLabel.HasValue() == false);
    EXPECT_TRUE(operationalErrorUnableToStartOrResume.errorStateDetails.HasValue() == false);

    // General errors: UnableToCompleteOperation
    GenericOperationalError operationalErrorkUnableToCompleteOperation(to_underlying(ErrorStateEnum::kUnableToCompleteOperation));

    EXPECT_TRUE(operationalErrorkUnableToCompleteOperation.errorStateID ==
                to_underlying(ErrorStateEnum::kUnableToCompleteOperation));
    EXPECT_TRUE(operationalErrorkUnableToCompleteOperation.errorStateLabel.HasValue() == false);
    EXPECT_TRUE(operationalErrorkUnableToCompleteOperation.errorStateDetails.HasValue() == false);

    // General errors: CommandInvalidInState
    GenericOperationalError operationalErrorCommandInvalidInState(to_underlying(ErrorStateEnum::kCommandInvalidInState));

    EXPECT_TRUE(operationalErrorCommandInvalidInState.errorStateID == to_underlying(ErrorStateEnum::kCommandInvalidInState));
    EXPECT_TRUE(operationalErrorCommandInvalidInState.errorStateLabel.HasValue() == false);
    EXPECT_TRUE(operationalErrorCommandInvalidInState.errorStateDetails.HasValue() == false);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalErrorConstructorWithStateIDAndStateLabel)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalErrorEnum : uint8_t
    {
        kLowBattery = 0x81,
    };

    char labelBuffer[kOperationalErrorLabelMaxSize] = "low battery";

    // ManufacturerStates error with label, label len = 11:
    GenericOperationalError operationalError(to_underlying(ManufactureOperationalErrorEnum::kLowBattery),
                                             Optional<CharSpan>(CharSpan::fromCharString(labelBuffer)));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ManufactureOperationalErrorEnum::kLowBattery));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateLabel.Value().size() == strlen(labelBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateLabel.Value().data()), labelBuffer, strlen(labelBuffer)) == 0);
    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == false);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalErrorConstructorWithFullParam)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalErrorEnum : uint8_t
    {
        kLowBattery = 0x81,
    };

    // ManufacturerStates error with label(label len = 11) and detail (len = 25):
    char labelBuffer[kOperationalErrorLabelMaxSize]    = "low battery";
    char detailBuffer[kOperationalErrorDetailsMaxSize] = "Please plug in for charge";

    GenericOperationalError operationalError(to_underlying(ManufactureOperationalErrorEnum::kLowBattery),
                                             Optional<CharSpan>(CharSpan::fromCharString(labelBuffer)),
                                             Optional<CharSpan>(CharSpan::fromCharString(detailBuffer)));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ManufactureOperationalErrorEnum::kLowBattery));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateLabel.Value().size() == strlen(labelBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateLabel.Value().data()), labelBuffer, strlen(labelBuffer)) == 0);

    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateDetails.Value().size() == strlen(detailBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateDetails.Value().data()), detailBuffer, strlen(detailBuffer)) ==
                0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalErrorCopyConstructor)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalErrorEnum : uint8_t
    {
        kLowBattery = 0x81,
    };

    // ManufacturerStates error with label(label len = 11) and detail (len = 25):
    char labelBuffer[kOperationalErrorLabelMaxSize]    = "low battery";
    char detailBuffer[kOperationalErrorDetailsMaxSize] = "Please plug in for charge";

    GenericOperationalError srcOperationalError(to_underlying(ManufactureOperationalErrorEnum::kLowBattery),
                                                Optional<CharSpan>(CharSpan::fromCharString(labelBuffer)),
                                                Optional<CharSpan>(CharSpan::fromCharString(detailBuffer)));

    // call copy constructor
    GenericOperationalError desOperationalError(srcOperationalError);
    EXPECT_TRUE(desOperationalError.errorStateID == srcOperationalError.errorStateID);
    EXPECT_TRUE(desOperationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(desOperationalError.errorStateLabel.Value().size() == srcOperationalError.errorStateLabel.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(desOperationalError.errorStateLabel.Value().data()),
                       const_cast<char *>(srcOperationalError.errorStateLabel.Value().data()),
                       desOperationalError.errorStateLabel.Value().size()) == 0);

    EXPECT_TRUE(desOperationalError.errorStateDetails.HasValue() == true);
    EXPECT_TRUE(desOperationalError.errorStateDetails.Value().size() == srcOperationalError.errorStateDetails.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(desOperationalError.errorStateDetails.Value().data()),
                       const_cast<char *>(srcOperationalError.errorStateDetails.Value().data()),
                       desOperationalError.errorStateDetails.Value().size()) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalErrorCopyAssignment)
{
    using namespace chip::app::Clusters::OperationalState;

    enum class ManufactureOperationalErrorEnum : uint8_t
    {
        kLowBattery = 0x81,
    };

    // ManufacturerStates error with label(label len = 11) and detail (len = 25):
    char labelBuffer[kOperationalErrorLabelMaxSize]    = "low battery";
    char detailBuffer[kOperationalErrorDetailsMaxSize] = "Please plug in for charge";

    GenericOperationalError srcOperationalError(to_underlying(ManufactureOperationalErrorEnum::kLowBattery),
                                                Optional<CharSpan>(CharSpan::fromCharString(labelBuffer)),
                                                Optional<CharSpan>(CharSpan::fromCharString(detailBuffer)));

    // call copy assignment
    GenericOperationalError desOperationalError = srcOperationalError;
    EXPECT_TRUE(desOperationalError.errorStateID == srcOperationalError.errorStateID);
    EXPECT_TRUE(desOperationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(desOperationalError.errorStateLabel.Value().size() == srcOperationalError.errorStateLabel.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(desOperationalError.errorStateLabel.Value().data()),
                       const_cast<char *>(srcOperationalError.errorStateLabel.Value().data()),
                       desOperationalError.errorStateLabel.Value().size()) == 0);

    EXPECT_TRUE(desOperationalError.errorStateDetails.HasValue() == true);
    EXPECT_TRUE(desOperationalError.errorStateDetails.Value().size() == srcOperationalError.errorStateDetails.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(desOperationalError.errorStateDetails.Value().data()),
                       const_cast<char *>(srcOperationalError.errorStateDetails.Value().data()),
                       desOperationalError.errorStateDetails.Value().size()) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalErrorFuncSet)
{
    using namespace chip::app::Clusters::OperationalState;
    enum class ManufactureOperationalErrorEnum : uint8_t
    {
        kLowBattery = 0x81,
    };

    // ManufacturerStates error with label(label len = 11) and detail (len = 25):
    char labelBuffer[kOperationalErrorLabelMaxSize]    = "low battery";
    char detailBuffer[kOperationalErrorDetailsMaxSize] = "Please plug in for charge";

    // General errors: NoError
    GenericOperationalError operationalError(to_underlying(ErrorStateEnum::kNoError));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ErrorStateEnum::kNoError));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == false);
    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == false);

    // call Set with stateId
    operationalError.Set(to_underlying(ErrorStateEnum::kUnableToStartOrResume));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ErrorStateEnum::kUnableToStartOrResume));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == false);
    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == false);

    // call Set with stateId and StateLabel
    operationalError.Set(to_underlying(ErrorStateEnum::kUnableToStartOrResume),
                         Optional<CharSpan>(CharSpan::fromCharString(labelBuffer)));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ErrorStateEnum::kUnableToStartOrResume));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateLabel.Value().size() == strlen(labelBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateLabel.Value().data()), labelBuffer, strlen(labelBuffer)) == 0);
    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == false);

    // call Set with stateId, StateLabel and StateDetails
    operationalError.Set(to_underlying(ErrorStateEnum::kUnableToStartOrResume),
                         Optional<CharSpan>(CharSpan::fromCharString(labelBuffer)),
                         Optional<CharSpan>(CharSpan::fromCharString(detailBuffer)));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ErrorStateEnum::kUnableToStartOrResume));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateLabel.Value().size() == strlen(labelBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateLabel.Value().data()), labelBuffer, strlen(labelBuffer)) == 0);

    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateDetails.Value().size() == strlen(detailBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateDetails.Value().data()), detailBuffer, strlen(detailBuffer)) ==
                0);

    // change state with label, label len = kOperationalStateLabelMaxSize
    for (size_t i = 0; i < sizeof(labelBuffer); i++)
    {
        labelBuffer[i] = 1;
    }
    operationalError.Set(to_underlying(ErrorStateEnum::kUnableToStartOrResume),
                         Optional<CharSpan>(CharSpan(labelBuffer, sizeof(labelBuffer))));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ErrorStateEnum::kUnableToStartOrResume));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateLabel.Value().size() == sizeof(labelBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateLabel.Value().data()), labelBuffer, sizeof(labelBuffer)) == 0);
    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == false);

    // change state with label, label len = kOperationalStateLabelMaxSize + 1
    char labelBuffer2[kOperationalErrorLabelMaxSize + 1];
    for (size_t i = 0; i < sizeof(labelBuffer2); i++)
    {
        labelBuffer2[i] = 2;
    }
    operationalError.Set(to_underlying(ErrorStateEnum::kUnableToStartOrResume),
                         Optional<CharSpan>(CharSpan(labelBuffer2, sizeof(labelBuffer2))));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ErrorStateEnum::kUnableToStartOrResume));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateLabel.Value().size() == kOperationalErrorLabelMaxSize);
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateLabel.Value().data()), labelBuffer2,
                       kOperationalErrorLabelMaxSize) == 0);
    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == false);

    // change state with label and details, details len = kOperationalErrorDetailsMaxSize + 1
    char detailBuffer2[kOperationalErrorDetailsMaxSize + 1];
    for (size_t i = 0; i < sizeof(detailBuffer2); i++)
    {
        detailBuffer2[i] = 3;
    }
    operationalError.Set(to_underlying(ErrorStateEnum::kUnableToStartOrResume),
                         Optional<CharSpan>(CharSpan(labelBuffer2, sizeof(labelBuffer2))),
                         Optional<CharSpan>(CharSpan(detailBuffer2, sizeof(detailBuffer2))));

    EXPECT_TRUE(operationalError.errorStateID == to_underlying(ErrorStateEnum::kUnableToStartOrResume));
    EXPECT_TRUE(operationalError.errorStateLabel.HasValue() == true);
    EXPECT_TRUE(operationalError.errorStateLabel.Value().size() == kOperationalErrorLabelMaxSize);
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateLabel.Value().data()), labelBuffer2,
                       kOperationalErrorLabelMaxSize) == 0);

    EXPECT_TRUE(operationalError.errorStateDetails.HasValue() == true);

    EXPECT_TRUE(operationalError.errorStateDetails.Value().size() == kOperationalErrorDetailsMaxSize);
    EXPECT_TRUE(memcmp(const_cast<char *>(operationalError.errorStateDetails.Value().data()), detailBuffer2,
                       kOperationalErrorDetailsMaxSize) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalPhaseConstructor)
{
    using namespace chip::app;
    using namespace chip::app::Clusters::OperationalState;

    GenericOperationalPhase phase = GenericOperationalPhase(DataModel::Nullable<CharSpan>());
    EXPECT_TRUE(phase.IsMissing() == true);

    char phaseBuffer[kOperationalPhaseNameMaxSize] = "start";
    GenericOperationalPhase phase2(DataModel::Nullable<CharSpan>(CharSpan::fromCharString(phaseBuffer)));
    EXPECT_TRUE(phase2.IsMissing() == false);
    EXPECT_TRUE(phase2.mPhaseName.Value().size() == strlen(phaseBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(phase2.mPhaseName.Value().data()), phaseBuffer, strlen(phaseBuffer)) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalPhaseCopyConstructor)
{
    using namespace chip::app;
    using namespace chip::app::Clusters::OperationalState;

    char phaseBuffer[kOperationalPhaseNameMaxSize] = "start";
    GenericOperationalPhase phase(DataModel::Nullable<CharSpan>(CharSpan::fromCharString(phaseBuffer)));

    GenericOperationalPhase phase2(phase);

    EXPECT_TRUE(phase2.IsMissing() == false);
    EXPECT_TRUE(phase2.mPhaseName.Value().size() == phase.mPhaseName.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(phase2.mPhaseName.Value().data()), const_cast<char *>(phase.mPhaseName.Value().data()),
                       phase.mPhaseName.Value().size()) == 0);
}

TEST_F(TestOperationalDeviceStateClusterObjects, TestStructGenericOperationalPhaseCopyAssignment)
{
    using namespace chip::app;
    using namespace chip::app::Clusters::OperationalState;

    // copy assignment with null-name
    GenericOperationalPhase phase = GenericOperationalPhase(DataModel::Nullable<CharSpan>());
    EXPECT_TRUE(phase.IsMissing() == true);

    // copy assignment with name
    char phaseBuffer[kOperationalPhaseNameMaxSize] = "start";
    GenericOperationalPhase phase2(DataModel::Nullable<CharSpan>(CharSpan::fromCharString(phaseBuffer)));
    phase = phase2;

    EXPECT_TRUE(phase.IsMissing() == false);
    EXPECT_TRUE(phase.mPhaseName.Value().size() == phase2.mPhaseName.Value().size());
    EXPECT_TRUE(memcmp(const_cast<char *>(phase.mPhaseName.Value().data()), const_cast<char *>(phase2.mPhaseName.Value().data()),
                       phase.mPhaseName.Value().size()) == 0);

    // copy assignment with name, name's len = kOperationalPhaseNameMaxSize
    for (size_t i = 0; i < sizeof(phaseBuffer); i++)
    {
        phaseBuffer[i] = 1;
    }
    phase = GenericOperationalPhase(DataModel::Nullable<CharSpan>(CharSpan(phaseBuffer, sizeof(phaseBuffer))));

    EXPECT_TRUE(phase.IsMissing() == false);
    EXPECT_TRUE(phase.mPhaseName.Value().size() == sizeof(phaseBuffer));
    EXPECT_TRUE(memcmp(const_cast<char *>(phase.mPhaseName.Value().data()), phaseBuffer, sizeof(phaseBuffer)) == 0);

    // copy assignment with name, name's len = kOperationalPhaseNameMaxSize + 1
    char phaseBuffer2[kOperationalPhaseNameMaxSize + 1];
    for (size_t i = 0; i < sizeof(phaseBuffer2); i++)
    {
        phaseBuffer2[i] = 2;
    }
    phase = GenericOperationalPhase(DataModel::Nullable<CharSpan>(CharSpan(phaseBuffer2, sizeof(phaseBuffer2))));

    EXPECT_TRUE(phase.IsMissing() == false);
    EXPECT_TRUE(phase.mPhaseName.Value().size() == kOperationalPhaseNameMaxSize);
    EXPECT_TRUE(memcmp(const_cast<char *>(phase.mPhaseName.Value().data()), phaseBuffer2, kOperationalPhaseNameMaxSize) == 0);
}

} // namespace
