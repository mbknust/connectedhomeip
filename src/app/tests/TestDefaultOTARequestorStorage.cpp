/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include <app/clusters/ota-requestor/DefaultOTARequestorStorage.h>
#include <app/clusters/ota-requestor/OTARequestorInterface.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <lib/support/TestPersistentStorageDelegate.h>

#include <gtest/gtest.h>

using namespace chip;
using namespace chip::DeviceLayer;

namespace {

TEST(OTA, TestDefaultProviders)
{
    TestPersistentStorageDelegate persistentStorage;
    DefaultOTARequestorStorage otaStorage;
    otaStorage.Init(persistentStorage);

    const auto makeProvider = [](FabricIndex fabric, NodeId nodeId, EndpointId endpointId) {
        OTARequestorStorage::ProviderLocationType provider;
        provider.fabricIndex    = fabric;
        provider.providerNodeID = nodeId;
        provider.endpoint       = endpointId;
        return provider;
    };

    ProviderLocationList providers = {};
    EXPECT_TRUE(CHIP_NO_ERROR == providers.Add(makeProvider(FabricIndex(1), NodeId(0x11111111), EndpointId(1))));
    EXPECT_TRUE(CHIP_NO_ERROR == providers.Add(makeProvider(FabricIndex(2), NodeId(0x22222222), EndpointId(2))));
    EXPECT_TRUE(CHIP_NO_ERROR == providers.Add(makeProvider(FabricIndex(3), NodeId(0x33333333), EndpointId(3))));
    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.StoreDefaultProviders(providers));

    providers = {};
    EXPECT_TRUE(!providers.Begin().Next());
    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.LoadDefaultProviders(providers));

    auto provider = providers.Begin();
    bool hasNext;

    EXPECT_TRUE(hasNext = provider.Next());

    if (hasNext)
    {
        EXPECT_TRUE(provider.GetValue().fabricIndex == 1);
        EXPECT_TRUE(provider.GetValue().providerNodeID == 0x11111111);
        EXPECT_TRUE(provider.GetValue().endpoint == 1);
    }

    EXPECT_TRUE(hasNext = provider.Next());

    if (hasNext)
    {
        EXPECT_TRUE(provider.GetValue().fabricIndex == 2);
        EXPECT_TRUE(provider.GetValue().providerNodeID == 0x22222222);
        EXPECT_TRUE(provider.GetValue().endpoint == 2);
    }

    EXPECT_TRUE(hasNext = provider.Next());

    if (hasNext)
    {
        EXPECT_TRUE(provider.GetValue().fabricIndex == 3);
        EXPECT_TRUE(provider.GetValue().providerNodeID == 0x33333333);
        EXPECT_TRUE(provider.GetValue().endpoint == 3);
    }

    EXPECT_TRUE(!provider.Next());
}

TEST(OTA, TestDefaultProvidersEmpty)
{
    TestPersistentStorageDelegate persistentStorage;
    DefaultOTARequestorStorage otaStorage;
    otaStorage.Init(persistentStorage);

    ProviderLocationList providers = {};

    EXPECT_TRUE(CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND == otaStorage.LoadDefaultProviders(providers));
    EXPECT_TRUE(!providers.Begin().Next());
}

TEST(OTA, TestCurrentProviderLocation)
{
    TestPersistentStorageDelegate persistentStorage;
    DefaultOTARequestorStorage otaStorage;
    otaStorage.Init(persistentStorage);

    OTARequestorStorage::ProviderLocationType provider;
    provider.fabricIndex    = 1;
    provider.providerNodeID = 0x12344321;
    provider.endpoint       = 10;

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.StoreCurrentProviderLocation(provider));

    provider = {};

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.LoadCurrentProviderLocation(provider));
    EXPECT_TRUE(provider.fabricIndex == 1);
    EXPECT_TRUE(provider.providerNodeID == 0x12344321);
    EXPECT_TRUE(provider.endpoint == 10);
    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.ClearCurrentProviderLocation());
    EXPECT_TRUE(CHIP_NO_ERROR != otaStorage.LoadCurrentProviderLocation(provider));
}

TEST(OTA, TestUpdateToken)
{
    TestPersistentStorageDelegate persistentStorage;
    DefaultOTARequestorStorage otaStorage;
    otaStorage.Init(persistentStorage);

    constexpr size_t updateTokenLength = 32;
    uint8_t updateTokenBuffer[updateTokenLength];
    ByteSpan updateToken(updateTokenBuffer);

    for (uint8_t i = 0; i < updateTokenLength; ++i)
        updateTokenBuffer[i] = i;

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.StoreUpdateToken(updateToken));

    uint8_t readBuffer[updateTokenLength + 10];
    MutableByteSpan readUpdateToken(readBuffer);
    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.LoadUpdateToken(readUpdateToken));
    EXPECT_TRUE(readUpdateToken.size() == updateTokenLength);

    for (uint8_t i = 0; i < updateTokenLength; ++i)
        EXPECT_TRUE(readBuffer[i] == i);

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.ClearUpdateToken());
    EXPECT_TRUE(CHIP_NO_ERROR != otaStorage.LoadUpdateToken(readUpdateToken));
}

TEST(OTA, TestCurrentUpdateState)
{
    TestPersistentStorageDelegate persistentStorage;
    DefaultOTARequestorStorage otaStorage;
    otaStorage.Init(persistentStorage);

    OTARequestorStorage::OTAUpdateStateEnum updateState = OTARequestorStorage::OTAUpdateStateEnum::kApplying;

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.StoreCurrentUpdateState(updateState));

    updateState = OTARequestorStorage::OTAUpdateStateEnum::kUnknown;

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.LoadCurrentUpdateState(updateState));
    EXPECT_TRUE(updateState == OTARequestorStorage::OTAUpdateStateEnum::kApplying);
    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.ClearCurrentUpdateState());
    EXPECT_TRUE(CHIP_NO_ERROR != otaStorage.LoadCurrentUpdateState(updateState));
}

TEST(OTA, TestTargetVersion)
{
    TestPersistentStorageDelegate persistentStorage;
    DefaultOTARequestorStorage otaStorage;
    otaStorage.Init(persistentStorage);

    uint32_t targetVersion = 2;

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.StoreTargetVersion(targetVersion));

    targetVersion = 0;

    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.LoadTargetVersion(targetVersion));
    EXPECT_TRUE(targetVersion == 2);
    EXPECT_TRUE(CHIP_NO_ERROR == otaStorage.ClearTargetVersion());
    EXPECT_TRUE(CHIP_NO_ERROR != otaStorage.LoadTargetVersion(targetVersion));
}

} // namespace