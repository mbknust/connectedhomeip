/*
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

#include <app/tests/AppTestContext.h>

#include <access/AccessControl.h>
#include <access/examples/PermissiveAccessControlDelegate.h>
#include <app/InteractionModelEngine.h>
#include <app/reporting/tests/MockReportScheduler.h>
#include <app/util/basic-types.h>
#include <app/util/mock/Constants.h>
#include <app/util/mock/Functions.h>
#include <gtest/gtest.h>
#include <lib/core/ErrorStr.h>
#include <lib/support/CodeUtils.h>

namespace {

class TestDeviceTypeResolver : public chip::Access::AccessControl::DeviceTypeResolver
{
public:
    bool IsDeviceTypeOnEndpoint(chip::DeviceTypeId deviceType, chip::EndpointId endpoint) override { return false; }
} gDeviceTypeResolver;

chip::Access::AccessControl gPermissiveAccessControl;

} // namespace

namespace chip {
namespace Test {

CHIP_ERROR AppContext::Init()
{
    ReturnErrorOnFailure(Super::Init());
    ReturnErrorOnFailure(chip::DeviceLayer::PlatformMgr().InitChipStack());
    ReturnErrorOnFailure(chip::app::InteractionModelEngine::GetInstance()->Init(&GetExchangeManager(), &GetFabricTable(),
                                                                                app::reporting::GetDefaultReportScheduler()));

    Access::SetAccessControl(gPermissiveAccessControl);
    ReturnErrorOnFailure(
        Access::GetAccessControl().Init(chip::Access::Examples::GetPermissiveAccessControlDelegate(), gDeviceTypeResolver));

    return CHIP_NO_ERROR;
}

void AppContext::Shutdown()
{
    Access::GetAccessControl().Finish();
    Access::ResetAccessControlToDefault();

    chip::app::InteractionModelEngine::GetInstance()->Shutdown();
    chip::DeviceLayer::PlatformMgr().Shutdown();
    Super::Shutdown();
}

} // namespace Test

namespace app {

bool __attribute__((weak)) ConcreteAttributePathExists(const ConcreteAttributePath & aPath)
{
    using namespace chip::Test::Constants::TestAclAttribute;
    return aPath.mClusterId != kTestDeniedClusterId1;
}

Protocols::InteractionModel::Status __attribute__((weak)) CheckEventSupportStatus(const ConcreteEventPath & aPath)
{
    using namespace chip::Test::Constants::TestAclAttribute;
    if (aPath.mClusterId == kTestDeniedClusterId1)
    {
        return Protocols::InteractionModel::Status::UnsupportedCluster;
    }

    return Protocols::InteractionModel::Status::Success;
}

void __attribute__((weak)) DispatchSingleClusterCommand(const ConcreteCommandPath & aCommandPath, chip::TLV::TLVReader & aReader,
                                                        CommandHandler * apCommandObj)
{
    VerifyOrDie(false);
}

const EmberAfAttributeMetadata * __attribute__((weak)) GetAttributeMetadata(const ConcreteAttributePath & aConcreteClusterPath)
{
    VerifyOrDie(false);
}

bool __attribute__((weak)) IsClusterDataVersionEqual(const ConcreteClusterPath & aConcreteClusterPath, DataVersion aRequiredVersion)
{
    VerifyOrDie(false);
}

bool __attribute__((weak)) IsDeviceTypeOnEndpoint(DeviceTypeId deviceType, EndpointId endpoint)
{
    VerifyOrDie(false);
}

CHIP_ERROR __attribute__((weak))
ReadSingleClusterData(const Access::SubjectDescriptor & aSubjectDescriptor, bool aIsFabricFiltered,
                      const ConcreteReadAttributePath & aPath, AttributeReportIBs::Builder & aAttributeReports,
                      AttributeValueEncoder::AttributeEncodeState * apEncoderState)
{
    using namespace chip::Test::Constants::TestReadInteraction;
    if (aPath.mClusterId >= Test::kMockEndpointMin)
    {
        return Test::ReadSingleMockClusterData(aSubjectDescriptor.fabricIndex, aPath, aAttributeReports, apEncoderState);
    }

    if (!(aPath.mClusterId == kTestClusterId && aPath.mEndpointId == kTestEndpointId))
    {
        AttributeReportIB::Builder & attributeReport = aAttributeReports.CreateAttributeReport();
        ReturnErrorOnFailure(aAttributeReports.GetError());
        ChipLogDetail(DataManagement, "TEST Cluster %" PRIx32 ", Field %" PRIx32 " is dirty", aPath.mClusterId, aPath.mAttributeId);

        AttributeStatusIB::Builder & attributeStatus = attributeReport.CreateAttributeStatus();
        ReturnErrorOnFailure(attributeReport.GetError());
        AttributePathIB::Builder & attributePath = attributeStatus.CreatePath();
        ReturnErrorOnFailure(attributeStatus.GetError());

        attributePath.Endpoint(aPath.mEndpointId).Cluster(aPath.mClusterId).Attribute(aPath.mAttributeId).EndOfAttributePathIB();
        ReturnErrorOnFailure(attributePath.GetError());
        StatusIB::Builder & errorStatus = attributeStatus.CreateErrorStatus();
        ReturnErrorOnFailure(attributeStatus.GetError());
        errorStatus.EncodeStatusIB(StatusIB(Protocols::InteractionModel::Status::UnsupportedAttribute));
        ReturnErrorOnFailure(errorStatus.GetError());
        ReturnErrorOnFailure(attributeStatus.EndOfAttributeStatusIB());
        return attributeReport.EndOfAttributeReportIB();
    }

    return AttributeValueEncoder(aAttributeReports, 0, aPath, 0).Encode(kTestFieldValue1);
}

Protocols::InteractionModel::Status __attribute__((weak)) ServerClusterCommandExists(const ConcreteCommandPath & aCommandPath)
{
    VerifyOrDie(false);
}

CHIP_ERROR __attribute__((weak))
WriteSingleClusterData(const Access::SubjectDescriptor & aSubjectDescriptor, const ConcreteDataAttributePath & aPath,
                       TLV::TLVReader & aReader, WriteHandler * apWriteHandler)
{
    VerifyOrDie(false);
}

} // namespace app
} // namespace chip
