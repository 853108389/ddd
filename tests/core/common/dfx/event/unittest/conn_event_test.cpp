/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "conn_event.h"
#include "conn_hisysevent_matcher.h"
#include "hisysevent_mock.h"
#include "softbus_hisysevent_matcher.h"
#include "gtest/gtest.h"

using namespace std;
using namespace testing;
using namespace testing::ext;

namespace OHOS {
class ConnEventTest : public testing::Test { };

/**
 * @tc.name: ConnEventTest001
 * @tc.desc: Test conn event form size
 * @tc.type: FUNC
 * @tc.require: I8HA59
 */
HWTEST_F(ConnEventTest, ConnEventTest001, TestSize.Level0)
{
    ConnEventExtra extra = {
        .requestId = 0,      // invalid
        .peerNetworkId = -1, // invalid
        .result = 1,
        .errcode = 2233,
        .peerPort = "9000",
    };
    constexpr int32_t VALID_EXTRA_SIZE = 3;

    HiSysEventMock mock;
    EXPECT_CALL(mock,
        HiSysEvent_Write(_, _, StrEq(SOFTBUS_EVENT_DOMAIN), StrEq(CONN_EVENT_NAME), Eq(SOFTBUS_EVENT_TYPE_BEHAVIOR), _,
            ParamArraySizeMatcher(VALID_EXTRA_SIZE)))
        .Times(1);
    CONN_EVENT(SCENE_OPEN_CHANNEL, STAGE_START_CONNECT, extra);
}

/**
 * @tc.name: ConnEventTest002
 * @tc.desc: Test all valid conn event form items
 * @tc.type: FUNC
 * @tc.require: I8HA59
 */
HWTEST_F(ConnEventTest, ConnEventTest002, TestSize.Level0)
{
    ConnEventExtra validExtra = {
        .requestId = 1,
        .linkType = 2,
        .expectRole = 3,
        .authType = 4,
        .authId = 5,
        .connectionId = 6,
        .peerNetworkId = 7,
        .rssi = 8,
        .load = 9,
        .frequency = 10,
        .costTime = 11,
        .result = 12,
        .errcode = 13,
        .peerBrMac = "testPeerBrMac",
        .peerBleMac = "testPeerBleMac",
        .peerWifiMac = "testPeerWifiMac",
        .peerIp = "testPeerIp",
        .peerPort = "testPeerPort",
    };
    constexpr int32_t VALID_EXTRA_SIZE = CONN_ASSIGNER_SIZE;

    HiSysEventMock mock;
    EXPECT_CALL(mock,
        HiSysEvent_Write(_, _, StrEq(SOFTBUS_EVENT_DOMAIN), StrEq(CONN_EVENT_NAME), Eq(SOFTBUS_EVENT_TYPE_BEHAVIOR),
            ConnValidParamArrayMatcher(validExtra, VALID_EXTRA_SIZE), ParamArraySizeMatcher(VALID_EXTRA_SIZE)))
        .Times(1);
    CONN_EVENT(SCENE_OPEN_CHANNEL, STAGE_CONNECT_INVOKE_PROTOCOL, validExtra);
}

/**
 * @tc.name: ConnEventTest003
 * @tc.desc: Test all invalid conn event form items
 * @tc.type: FUNC
 * @tc.require: I8HA59
 */
HWTEST_F(ConnEventTest, ConnEventTest003, TestSize.Level0)
{
    ConnEventExtra invalidExtra = {
        .requestId = -1,
        .linkType = -2,
        .expectRole = -3,
        .authType = -4,
        .authId = -5,
        .connectionId = -6,
        .peerNetworkId = -7,
        .rssi = -8,
        .load = -9,
        .frequency = -10,
        .costTime = -11,
        .result = -12,
        .errcode = -13, // valid
        .peerBrMac = nullptr,
        .peerBleMac = "\0",
        .peerWifiMac = "",
        .peerIp = nullptr,
        .peerPort = nullptr,
    };
    constexpr int32_t VALID_EXTRA_SIZE = 1; // errcode is valid

    HiSysEventMock mock;
    EXPECT_CALL(mock,
        HiSysEvent_Write(_, _, StrEq(SOFTBUS_EVENT_DOMAIN), StrEq(CONN_EVENT_NAME), Eq(SOFTBUS_EVENT_TYPE_BEHAVIOR),
            ConnInvalidParamArrayMatcher(invalidExtra, VALID_EXTRA_SIZE), ParamArraySizeMatcher(VALID_EXTRA_SIZE)))
        .Times(1);
    CONN_EVENT(SCENE_CONNECT, STAGE_CONNECT_END, invalidExtra);
}

/**
 * @tc.name: ConnEventTest004
 * @tc.desc: Test empty conn event form
 * @tc.type: FUNC
 * @tc.require: I8HA59
 */
HWTEST_F(ConnEventTest, ConnEventTest004, TestSize.Level0)
{
    ConnEventExtra emptyExtra = { 0 };
    constexpr int32_t VALID_EXTRA_SIZE = 1; // errcode is valid

    HiSysEventMock mock;
    EXPECT_CALL(mock,
        HiSysEvent_Write(_, _, StrEq(SOFTBUS_EVENT_DOMAIN), StrEq(CONN_EVENT_NAME), Eq(SOFTBUS_EVENT_TYPE_BEHAVIOR),
            ConnInvalidParamArrayMatcher(emptyExtra, VALID_EXTRA_SIZE), ParamArraySizeMatcher(VALID_EXTRA_SIZE)))
        .Times(1);
    CONN_EVENT(SCENE_CONNECT, STAGE_CONNECT_START, emptyExtra);
}
} // namespace OHOS
