/*
 *
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

#include "app-common/zap-generated/ids/Attributes.h"
#include "app-common/zap-generated/ids/Clusters.h"
#include "app/ClusterStateCache.h"
#include "app/ConcreteAttributePath.h"
#include "protocols/interaction_model/Constants.h"
#include <app-common/zap-generated/cluster-objects.h>
#include <app/AppConfig.h>
#include <app/AttributeAccessInterface.h>
#include <app/BufferedReadCallback.h>
#include <app/CommandHandlerInterface.h>
#include <app/EventLogging.h>
#include <app/InteractionModelEngine.h>
#include <app/data-model/Decode.h>
#include <app/tests/AppTestContext.h>
#include <app/util/DataModelHandler.h>
#include <app/util/attribute-storage.h>
#include <controller/InvokeInteraction.h>
#include <gtest/gtest.h>
#include <lib/support/ErrorStr.h>
#include <lib/support/TimeUtils.h>
#include <lib/support/UnitTestContext.h>

#include <lib/support/UnitTestUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <messaging/tests/MessagingContext.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

static uint8_t gDebugEventBuffer[4096];
static uint8_t gInfoEventBuffer[4096];
static uint8_t gCritEventBuffer[4096];
static chip::app::CircularEventBuffer gCircularEventBuffer[3];

class TestContext : public chip::Test::AppContext
{
public:
    static int Initialize(void * context)
    {
        if (AppContext::Initialize(context) != SUCCESS)
            return FAILURE;

        auto * ctx = static_cast<TestContext *>(context);

        if (ctx->mEventCounter.Init(0) != CHIP_NO_ERROR)
        {
            return FAILURE;
        }

        chip::app::LogStorageResources logStorageResources[] = {
            { &gDebugEventBuffer[0], sizeof(gDebugEventBuffer), chip::app::PriorityLevel::Debug },
            { &gInfoEventBuffer[0], sizeof(gInfoEventBuffer), chip::app::PriorityLevel::Info },
            { &gCritEventBuffer[0], sizeof(gCritEventBuffer), chip::app::PriorityLevel::Critical },
        };

        chip::app::EventManagement::CreateEventManagement(&ctx->GetExchangeManager(),
                                                          sizeof(logStorageResources) / sizeof(logStorageResources[0]),
                                                          gCircularEventBuffer, logStorageResources, &ctx->mEventCounter);

        return SUCCESS;
    }

    static int Finalize(void * context)
    {
        chip::app::EventManagement::DestroyEventManagement();

        if (AppContext::Finalize(context) != SUCCESS)
            return FAILURE;

        return SUCCESS;
    }

private:
    MonotonicallyIncreasingCounter<EventNumber> mEventCounter;
};

//
// The generated endpoint_config for the controller app has Endpoint 1
// already used in the fixed endpoint set of size 1. Consequently, let's use the next
// number higher than that for our dynamic test endpoint.
//
constexpr EndpointId kTestEndpointId = 2;

class TestReadEvents : public ::testing::Test
{
public:
    TestReadEvents() {}
    static TestContext ctx;
    static void SetUpTestSuite() { TestContext::Initialize(&ctx); }
    static void TearDownTestSuite() { TestContext::Finalize(&ctx); }

    static void TestBasicCaching();

private:
};

TestContext TestReadEvents::ctx;

//clang-format off
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(testClusterAttrs)
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(testEndpointClusters)
DECLARE_DYNAMIC_CLUSTER(Clusters::UnitTesting::Id, testClusterAttrs, nullptr, nullptr), DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(testEndpoint, testEndpointClusters);

//clang-format on

class TestReadCallback : public app::ClusterStateCache::Callback
{
public:
    TestReadCallback() : mClusterCacheAdapter(*this) {}
    void OnDone(app::ReadClient *) override {}

    void OnEventData(const EventHeader & aEventHeader, TLV::TLVReader * apData, const StatusIB * apStatus) override
    {
        ++mEventsSeen;
    }

    app::ClusterStateCache mClusterCacheAdapter;

    size_t mEventsSeen = 0;
};

namespace {

void GenerateEvents(chip::EventNumber & firstEventNumber, chip::EventNumber & lastEventNumber)
{
    CHIP_ERROR err                 = CHIP_NO_ERROR;
    static uint8_t generationCount = 0;

    Clusters::UnitTesting::Events::TestEvent::Type content;

    for (int i = 0; i < 5; i++)
    {
        content.arg1 = static_cast<uint8_t>(generationCount++);
        EXPECT_TRUE((err = app::LogEvent(content, kTestEndpointId, lastEventNumber)) == CHIP_NO_ERROR);
        if (i == 0)
        {
            firstEventNumber = lastEventNumber;
        }
    }
}

} // namespace

#define STATIC_TEST(test_fixture, test_name)                                                                                       \
    TEST_F(test_fixture, test_name) { test_fixture::test_name(); }                                                                 \
    void test_fixture::test_name()

/*
 * This validates event caching by forcing a bunch of events to get generated, then reading them back
 * and upon completion of that operation, iterating over any events that have accumulated in the cache
 * and validating their contents.
 *
 * It then proceeds to do another round of generation and re-do the read, validating that the old and new
 * events are present in the cache.
 *
 */
STATIC_TEST(TestReadEvents, TestBasicCaching)
{
    auto sessionHandle                   = ctx.GetSessionBobToAlice();
    app::InteractionModelEngine * engine = app::InteractionModelEngine::GetInstance();

    // Initialize the ember side server logic
    InitDataModelHandler();

    // Register our fake dynamic endpoint.
    DataVersion dataVersionStorage[ArraySize(testEndpointClusters)];
    emberAfSetDynamicEndpoint(0, kTestEndpointId, &testEndpoint, Span<DataVersion>(dataVersionStorage));

    chip::EventNumber firstEventNumber;
    chip::EventNumber lastEventNumber;

    GenerateEvents(firstEventNumber, lastEventNumber);
    EXPECT_TRUE(lastEventNumber > firstEventNumber);

    app::EventPathParams eventPath;
    eventPath.mEndpointId = kTestEndpointId;
    eventPath.mClusterId  = app::Clusters::UnitTesting::Id;
    app::ReadPrepareParams readParams(sessionHandle);

    readParams.mpEventPathParamsList    = &eventPath;
    readParams.mEventPathParamsListSize = 1;
    readParams.mEventNumber.SetValue(firstEventNumber);

    TestReadCallback readCallback;

    {
        app::ReadClient readClient(engine, &ctx.GetExchangeManager(), readCallback.mClusterCacheAdapter.GetBufferedCallback(),
                                   app::ReadClient::InteractionType::Read);

        EXPECT_TRUE(readClient.SendRequest(readParams) == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        uint8_t generationCount = 0;
        readCallback.mClusterCacheAdapter.ForEachEventData(
            [&readCallback, &generationCount, firstEventNumber, lastEventNumber](const app::EventHeader & header) {
                EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
                EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
                EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);
                EXPECT_TRUE(header.mEventNumber >= firstEventNumber);
                EXPECT_TRUE(header.mEventNumber <= lastEventNumber);

                Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
                EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

                EXPECT_TRUE(eventData.arg1 == generationCount);
                generationCount++;
                return CHIP_NO_ERROR;
            });

        EXPECT_TRUE(generationCount == lastEventNumber - firstEventNumber + 1);

        Optional<EventNumber> highestEventNumber;
        readCallback.mClusterCacheAdapter.GetHighestReceivedEventNumber(highestEventNumber);
        EXPECT_TRUE(highestEventNumber.HasValue() && highestEventNumber.Value() == lastEventNumber);

        //
        // Re-run the iterator but pass in a path filter: EP*/TestCluster/EID*
        //
        generationCount = 0;
        readCallback.mClusterCacheAdapter.ForEachEventData(
            [&readCallback, &generationCount, firstEventNumber, lastEventNumber](const app::EventHeader & header) {
                EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
                EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
                EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);
                EXPECT_TRUE(header.mEventNumber >= firstEventNumber);
                EXPECT_TRUE(header.mEventNumber <= lastEventNumber);

                Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
                EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

                EXPECT_TRUE(eventData.arg1 == generationCount);
                generationCount++;
                return CHIP_NO_ERROR;
            },
            app::EventPathParams(kInvalidEndpointId, Clusters::UnitTesting::Id, kInvalidEventId));

        EXPECT_TRUE(generationCount == lastEventNumber - firstEventNumber + 1);

        //
        // Re-run the iterator but pass in a path filter: EP*/TestCluster/TestEvent
        //
        generationCount = 0;
        readCallback.mClusterCacheAdapter.ForEachEventData(
            [&readCallback, &generationCount, firstEventNumber, lastEventNumber](const app::EventHeader & header) {
                EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
                EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
                EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);
                EXPECT_TRUE(header.mEventNumber >= firstEventNumber);
                EXPECT_TRUE(header.mEventNumber <= lastEventNumber);

                Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
                EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

                EXPECT_TRUE(eventData.arg1 == generationCount);
                generationCount++;
                return CHIP_NO_ERROR;
            },
            app::EventPathParams(kInvalidEndpointId, Clusters::UnitTesting::Id, Clusters::UnitTesting::Events::TestEvent::Id));

        EXPECT_TRUE(generationCount == lastEventNumber - firstEventNumber + 1);

        //
        // Re-run the iterator but pass in a min event number filter
        // (EventNumber = firstEventNumber + 1). We should only receive 4 events.
        //
        generationCount = 1;
        readCallback.mClusterCacheAdapter.ForEachEventData(
            [&readCallback, &generationCount, firstEventNumber, lastEventNumber](const app::EventHeader & header) {
                EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
                EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
                EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);
                EXPECT_TRUE(header.mEventNumber >= firstEventNumber + 1);
                EXPECT_TRUE(header.mEventNumber <= lastEventNumber);

                Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
                EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

                EXPECT_TRUE(eventData.arg1 == generationCount);
                generationCount++;
                return CHIP_NO_ERROR;
            },
            app::EventPathParams(), firstEventNumber + 1);

        EXPECT_TRUE(generationCount == lastEventNumber - firstEventNumber + 1);

        //
        // Re-run the iterator but pass in a min event number filter
        // (EventNumber = firstEventNumber + 1) AND a path filter. We should only receive 4
        // events.
        //
        generationCount = 1;
        readCallback.mClusterCacheAdapter.ForEachEventData(
            [&readCallback, &generationCount, firstEventNumber, lastEventNumber](const app::EventHeader & header) {
                EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
                EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
                EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);
                EXPECT_TRUE(header.mEventNumber >= firstEventNumber + 1);
                EXPECT_TRUE(header.mEventNumber <= lastEventNumber);

                Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
                EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

                EXPECT_TRUE(eventData.arg1 == generationCount);
                generationCount++;
                return CHIP_NO_ERROR;
            },
            app::EventPathParams(kInvalidEndpointId, Clusters::UnitTesting::Id, kInvalidEventId), firstEventNumber + 1);

        EXPECT_TRUE(generationCount == lastEventNumber - firstEventNumber + 1);
    }

    //
    // Generate more events.
    //
    const EventNumber oldFirstEventNumber = firstEventNumber;
    GenerateEvents(firstEventNumber, lastEventNumber);

    {
        app::ReadClient readClient(engine, &ctx.GetExchangeManager(), readCallback.mClusterCacheAdapter.GetBufferedCallback(),
                                   app::ReadClient::InteractionType::Read);

        EXPECT_TRUE(readClient.SendRequest(readParams) == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        //
        // Validate that we still have all 5 of the old events we received, as well as the new ones that just got generated.
        // This also ensures that we don't receive duplicate events in the `ForEachEventData` call below.
        //
        uint8_t generationCount = 0;
        readCallback.mClusterCacheAdapter.ForEachEventData(
            [&readCallback, &generationCount, oldFirstEventNumber, lastEventNumber](const app::EventHeader & header) {
                EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
                EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
                EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);
                EXPECT_TRUE(header.mEventNumber >= oldFirstEventNumber);
                EXPECT_TRUE(header.mEventNumber <= lastEventNumber);

                Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
                EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

                EXPECT_TRUE(eventData.arg1 == generationCount);
                generationCount++;

                return CHIP_NO_ERROR;
            });

        EXPECT_TRUE(generationCount == lastEventNumber - oldFirstEventNumber + 1);

        Optional<EventNumber> highestEventNumber;
        readCallback.mClusterCacheAdapter.GetHighestReceivedEventNumber(highestEventNumber);
        EXPECT_TRUE(highestEventNumber.HasValue() && highestEventNumber.Value() == 9);

        readCallback.mClusterCacheAdapter.ClearEventCache();
        generationCount = 0;
        readCallback.mClusterCacheAdapter.ForEachEventData([&generationCount](const app::EventHeader & header) {
            generationCount++;
            return CHIP_NO_ERROR;
        });

        EXPECT_TRUE(generationCount == 0);
        readCallback.mClusterCacheAdapter.GetHighestReceivedEventNumber(highestEventNumber);
        EXPECT_TRUE(highestEventNumber.HasValue() && highestEventNumber.Value() == 9);
    }

    //
    // Clear out the event cache and set its highest received event number to a non zero value. Validate that
    // we don't receive events lower than that value.
    //
    {
        app::ReadClient readClient(engine, &ctx.GetExchangeManager(), readCallback.mClusterCacheAdapter.GetBufferedCallback(),
                                   app::ReadClient::InteractionType::Read);

        readCallback.mClusterCacheAdapter.ClearEventCache();
        constexpr EventNumber kLastSeenEventNumber = 3;
        EXPECT_TRUE(kLastSeenEventNumber < lastEventNumber);
        readCallback.mClusterCacheAdapter.SetHighestReceivedEventNumber(kLastSeenEventNumber);
        readParams.mEventNumber.ClearValue();

        readCallback.mEventsSeen = 0;

        EXPECT_TRUE(readClient.SendRequest(readParams) == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        // We should only get events with event numbers larger than kHighestEventNumberSeen.
        EXPECT_TRUE(readCallback.mEventsSeen == lastEventNumber - kLastSeenEventNumber);

        uint8_t generationCount = kLastSeenEventNumber + 1;
        readCallback.mClusterCacheAdapter.ForEachEventData(
            [&readCallback, &generationCount, lastEventNumber](const app::EventHeader & header) {
                EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
                EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
                EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);
                EXPECT_TRUE(header.mEventNumber > kLastSeenEventNumber);
                EXPECT_TRUE(header.mEventNumber <= lastEventNumber);

                Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
                EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

                EXPECT_TRUE(eventData.arg1 == generationCount);
                generationCount++;

                return CHIP_NO_ERROR;
            });

        EXPECT_TRUE(generationCount == lastEventNumber - oldFirstEventNumber + 1);
        Optional<EventNumber> highestEventNumber;
        readCallback.mClusterCacheAdapter.GetHighestReceivedEventNumber(highestEventNumber);
        EXPECT_TRUE(highestEventNumber.HasValue() && highestEventNumber.Value() == lastEventNumber);
    }

    //
    // Set user-provided event number, then read client would use user-provided event number and not use the cached one in read
    // client
    //

    {
        readParams.mEventNumber.SetValue(5);
        app::ReadClient readClient(engine, &ctx.GetExchangeManager(), readCallback.mClusterCacheAdapter.GetBufferedCallback(),
                                   app::ReadClient::InteractionType::Read);
        readCallback.mClusterCacheAdapter.ClearEventCache(true);
        EXPECT_TRUE(readClient.SendRequest(readParams) == CHIP_NO_ERROR);

        ctx.DrainAndServiceIO();

        //
        // Validate that we would receive 5 events
        //

        uint8_t generationCount = 5;
        readCallback.mClusterCacheAdapter.ForEachEventData([&readCallback, &generationCount](const app::EventHeader & header) {
            EXPECT_TRUE(header.mPath.mClusterId == Clusters::UnitTesting::Id);
            EXPECT_TRUE(header.mPath.mEventId == Clusters::UnitTesting::Events::TestEvent::Id);
            EXPECT_TRUE(header.mPath.mEndpointId == kTestEndpointId);

            Clusters::UnitTesting::Events::TestEvent::DecodableType eventData;
            EXPECT_TRUE(readCallback.mClusterCacheAdapter.Get(header.mEventNumber, eventData) == CHIP_NO_ERROR);

            EXPECT_TRUE(eventData.arg1 == generationCount);
            generationCount++;

            return CHIP_NO_ERROR;
        });

        EXPECT_TRUE(generationCount == 10);

        Optional<EventNumber> highestEventNumber;
        readCallback.mClusterCacheAdapter.GetHighestReceivedEventNumber(highestEventNumber);
        EXPECT_TRUE(highestEventNumber.HasValue() && highestEventNumber.Value() == 9);
    }

    EXPECT_TRUE(ctx.GetExchangeManager().GetNumActiveExchanges() == 0);

    emberAfClearDynamicEndpoint(0);
}

} // namespace
