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

#include <app/clusters/time-synchronization-server/TimeSyncDataProvider.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <lib/support/TestPersistentStorageDelegate.h>

#include <gtest/gtest.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters::TimeSynchronization;
using chip::TimeSyncDataProvider;

using TrustedTimeSource = app::Clusters::TimeSynchronization::Structs::TrustedTimeSourceStruct::Type;
using TimeZoneList      = Span<TimeSyncDataProvider::TimeSyncDataProvider::TimeZoneStore>;
using TimeZone          = app::Clusters::TimeSynchronization::Structs::TimeZoneStruct::Type;
using DSTOffsetList     = app::DataModel::List<app::Clusters::TimeSynchronization::Structs::DSTOffsetStruct::Type>;
using DSTOffset         = app::Clusters::TimeSynchronization::Structs::DSTOffsetStruct::Type;

namespace {
class TestTimeSyncDataProvider : public ::testing::Test
{
public:
    static void SetUpTestSuite() { VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR); }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

TEST_F(TestTimeSyncDataProvider, TestTrustedTimeSourceStoreLoad)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    TrustedTimeSource tts = { chip::FabricIndex(1), chip::NodeId(20), chip::EndpointId(0) };

    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.StoreTrustedTimeSource(tts));

    TrustedTimeSource retrievedTrustedTimeSource;
    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.LoadTrustedTimeSource(retrievedTrustedTimeSource));
    EXPECT_TRUE(retrievedTrustedTimeSource.fabricIndex == chip::FabricIndex(1));
    EXPECT_TRUE(retrievedTrustedTimeSource.nodeID == chip::NodeId(20));
    EXPECT_TRUE(retrievedTrustedTimeSource.endpoint == chip::EndpointId(0));
}

TEST_F(TestTimeSyncDataProvider, TestTrustedTimeSourceEmpty)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    TrustedTimeSource tts;

    EXPECT_TRUE(CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND == timeSyncDataProv.LoadTrustedTimeSource(tts));
}

TEST_F(TestTimeSyncDataProvider, TestDefaultNTPStoreLoad)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    char ntp[10] = "localhost";
    chip::CharSpan defaultNTP(ntp);

    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.StoreDefaultNtp(defaultNTP));

    char buf[5];
    chip::MutableCharSpan getDefaultNtp(buf);

    EXPECT_TRUE(CHIP_ERROR_BUFFER_TOO_SMALL == timeSyncDataProv.LoadDefaultNtp(getDefaultNtp));
    EXPECT_TRUE(getDefaultNtp.size() == 5);

    char buf1[20];
    chip::MutableCharSpan getDefaultNtp1(buf1);

    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.LoadDefaultNtp(getDefaultNtp1));
    EXPECT_TRUE(getDefaultNtp1.size() == 10);
}

TEST_F(TestTimeSyncDataProvider, TestDefaultNTPEmpty)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    chip::MutableCharSpan defaultNTP;

    EXPECT_TRUE(CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND == timeSyncDataProv.LoadDefaultNtp(defaultNTP));
}

TEST_F(TestTimeSyncDataProvider, TestTimeZoneStoreLoad)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    const auto makeTimeZone = [](int32_t offset = 0, uint64_t validAt = 0, char * name = nullptr, uint8_t len = 0) {
        TimeSyncDataProvider::TimeZoneStore tzS;
        tzS.timeZone.offset  = offset;
        tzS.timeZone.validAt = validAt;
        if (name != nullptr && len)
        {
            memcpy(tzS.name, name, len);
            tzS.timeZone.name.SetValue(chip::CharSpan(tzS.name, len));
        }
        return tzS;
    };
    char tzShort[]                             = "LA";
    char tzLong[]                              = "MunichOnTheLongRiverOfIsarInNiceSummerWeatherWithAugustinerBeer";
    char tzBerlin[]                            = "Berlin";
    TimeSyncDataProvider::TimeZoneStore tzS[3] = { makeTimeZone(1, 1, tzShort, sizeof(tzShort)),
                                                   makeTimeZone(2, 2, tzLong, sizeof(tzLong)),
                                                   makeTimeZone(3, 3, tzBerlin, sizeof(tzBerlin)) };
    TimeZoneList tzL(tzS);
    EXPECT_TRUE(tzL.size() == 3);
    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.StoreTimeZone(tzL));

    TimeSyncDataProvider::TimeZoneStore emptyTzS[3] = { makeTimeZone(), makeTimeZone(), makeTimeZone() };

    tzL = TimeZoneList(emptyTzS);
    TimeSyncDataProvider::TimeZoneObj tzObj{ tzL, 3 };
    EXPECT_TRUE(tzL.size() == 3);
    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.LoadTimeZone(tzObj));
    EXPECT_TRUE(tzObj.validSize == 3);

    EXPECT_TRUE(!tzL.empty());

    if (!tzL.empty())
    {
        auto & tz = tzL[0].timeZone;
        EXPECT_TRUE(tz.offset == 1);
        EXPECT_TRUE(tz.validAt == 1);
        EXPECT_TRUE(tz.name.HasValue());
        EXPECT_TRUE(tz.name.Value().size() == 3);

        tzL = tzL.SubSpan(1);
    }

    if (!tzL.empty())
    {
        auto & tz = tzL[0].timeZone;
        EXPECT_TRUE(tz.offset == 2);
        EXPECT_TRUE(tz.validAt == 2);
        EXPECT_TRUE(tz.name.HasValue());
        EXPECT_TRUE(tz.name.Value().size() == 64);

        tzL = tzL.SubSpan(1);
    }

    if (!tzL.empty())
    {
        auto & tz = tzL[0].timeZone;
        EXPECT_TRUE(tz.offset == 3);
        EXPECT_TRUE(tz.validAt == 3);
        EXPECT_TRUE(tz.name.HasValue());
        EXPECT_TRUE(tz.name.Value().size() == 7);

        tzL = tzL.SubSpan(1);
    }

    EXPECT_TRUE(tzL.empty());
}

TEST_F(TestTimeSyncDataProvider, TestTimeZoneEmpty)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    TimeSyncDataProvider::TimeZoneObj timeZoneObj;

    EXPECT_TRUE(CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND == timeSyncDataProv.LoadTimeZone(timeZoneObj));
    EXPECT_TRUE(!timeZoneObj.timeZoneList.begin());
    EXPECT_TRUE(timeZoneObj.validSize == 0);
}

TEST_F(TestTimeSyncDataProvider, TestDSTOffset)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    const auto makeDSTOffset = [](int32_t offset = 0, uint64_t validStarting = 0, uint64_t validUntil = 0) {
        DSTOffset dst;
        dst.offset        = offset;
        dst.validStarting = validStarting;
        if (validUntil)
            dst.validUntil.SetNonNull(validUntil);
        return dst;
    };
    DSTOffset dstS[3] = { makeDSTOffset(1, 1, 2), makeDSTOffset(2, 2, 3), makeDSTOffset(3, 3) };
    DSTOffsetList dstL(dstS);
    TimeSyncDataProvider::DSTOffsetObj dstObj{ dstL, 3 };
    EXPECT_TRUE(dstObj.validSize == 3);
    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.StoreDSTOffset(dstL));

    DSTOffset emtpyDstS[3] = { makeDSTOffset(), makeDSTOffset(), makeDSTOffset() };

    dstObj.dstOffsetList = DSTOffsetList(emtpyDstS);
    dstObj.validSize     = 0;
    EXPECT_TRUE(dstObj.dstOffsetList.size() == 3);
    EXPECT_TRUE(CHIP_NO_ERROR == timeSyncDataProv.LoadDSTOffset(dstObj));
    EXPECT_TRUE(dstObj.validSize == 3);

    EXPECT_TRUE(!dstObj.dstOffsetList.empty());

    if (!dstObj.dstOffsetList.empty())
    {
        auto & dst = dstObj.dstOffsetList.data()[0];
        EXPECT_TRUE(dst.offset == 1);
        EXPECT_TRUE(dst.validStarting == 1);
        EXPECT_TRUE(!dst.validUntil.IsNull());
        EXPECT_TRUE(dst.validUntil.Value() == 2);

        dstObj.dstOffsetList = dstObj.dstOffsetList.SubSpan(1);
    }

    if (!dstObj.dstOffsetList.empty())
    {
        auto & dst = dstObj.dstOffsetList.data()[0];
        EXPECT_TRUE(dst.offset == 2);
        EXPECT_TRUE(dst.validStarting == 2);
        EXPECT_TRUE(!dst.validUntil.IsNull());
        EXPECT_TRUE(dst.validUntil.Value() == 3);

        dstObj.dstOffsetList = dstObj.dstOffsetList.SubSpan(1);
    }

    if (!dstObj.dstOffsetList.empty())
    {
        auto & dst = dstObj.dstOffsetList.data()[0];
        EXPECT_TRUE(dst.offset == 3);
        EXPECT_TRUE(dst.validStarting == 3);
        EXPECT_TRUE(dst.validUntil.IsNull());

        dstObj.dstOffsetList = dstObj.dstOffsetList.SubSpan(1);
    }

    EXPECT_TRUE(dstObj.dstOffsetList.empty());
}

TEST_F(TestTimeSyncDataProvider, TestDSTOffsetEmpty)
{
    TestPersistentStorageDelegate persistentStorage;
    TimeSyncDataProvider timeSyncDataProv;
    timeSyncDataProv.Init(persistentStorage);

    TimeSyncDataProvider::DSTOffsetObj dstObj;

    EXPECT_TRUE(CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND == timeSyncDataProv.LoadDSTOffset(dstObj));
    EXPECT_TRUE(!dstObj.dstOffsetList.begin());
    EXPECT_TRUE(dstObj.validSize == 0);
}
} // namespace