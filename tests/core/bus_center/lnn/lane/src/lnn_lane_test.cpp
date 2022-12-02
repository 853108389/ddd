/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <securec.h>


#include "bus_center_info_key.h"
#include "lnn_lane_common.h"
#include "lnn_lane_deps_mock.h"
#include "lnn_lane_def.h"
#include "lnn_lane.h"
#include "lnn_lane_interface.h"
#include "lnn_lane_link.h"
#include "lnn_lane_link_proc.h"
#include "lnn_lane_model.h"
#include "lnn_lane_select.h"

#include "lnn_wifi_adpter_mock.h"
#include "message_handler.h"
#include "softbus_adapter_mem.h"
#include "softbus_adapter_thread.h"
#include "softbus_error_code.h"
#include "softbus_wifi_api_adapter.h"

namespace OHOS {
using namespace testing::ext;
using namespace testing;

constexpr char NODE_NETWORK_ID[] = "111122223333abcdef";
constexpr uint32_t WLAN_2PG_BAND = 1;
constexpr uint32_t WLAN_5G_BAND = 0;
constexpr uint32_t WLAN_2PG_FREQUENCY = 2642;
constexpr uint32_t WLAN_5G_FREQUENCY = 5188;
constexpr uint32_t DEFAULT_PID = 0;
constexpr uint32_t DEFAULT_SELECT_NUM = 4;
constexpr SoftBusCond *g_cond = nullptr;

static SoftBusMutex g_lock = 0;

class LNNLaneTestMock : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void LNNLaneTestMock::SetUpTestCase()
{
    int32_t ret = LooperInit();
    EXPECT_TRUE(ret == SOFTBUS_OK);
    ret = InitLane();
    EXPECT_TRUE(ret == SOFTBUS_OK);
    GTEST_LOG_(INFO) << "LNNLaneTestMock start";
}

void LNNLaneTestMock::TearDownTestCase()
{
    DeinitLane();
    LooperDeinit();
    GTEST_LOG_(INFO) << "LNNLaneTestMock end";
}

void LNNLaneTestMock::SetUp()
{
    (void)SoftBusCondInit(g_cond);
}

void LNNLaneTestMock::TearDown()
{
    (void)SoftBusCondDestroy(g_cond);
}

static void CondSignal(void)
{
    if (SoftBusMutexLock(&g_lock) != SOFTBUS_OK) {
        return;
    }
    if (SoftBusCondSignal(g_cond) != SOFTBUS_OK) {
        (void)SoftBusMutexUnlock(&g_lock);
        return;
    }
    (void)SoftBusMutexUnlock(&g_lock);
}

static void CondWait(void)
{
    if (SoftBusMutexLock(&g_lock) != SOFTBUS_OK) {
        return;
    }
    if (SoftBusCondWait(g_cond, &g_lock, nullptr) != SOFTBUS_OK) {
        (void)SoftBusMutexUnlock(&g_lock);
        return;
    }
    (void)SoftBusMutexUnlock(&g_lock);
}

static void OnLaneRequestSuccess(uint32_t laneId, const LaneConnInfo *info)
{
    int32_t ret = LnnFreeLane(laneId);
    EXPECT_TRUE(ret == SOFTBUS_OK);
    CondSignal();
}

static void OnLaneRequestFail(uint32_t laneId, LaneRequestFailReason reason)
{
    int32_t ret = LnnFreeLane(laneId);
    EXPECT_TRUE(ret == SOFTBUS_OK);
    CondSignal();
}

static void OnLaneStateChange(uint32_t laneId, LaneState state)
{
    int32_t ret = LnnFreeLane(laneId);
    EXPECT_TRUE(ret == SOFTBUS_OK);
    CondSignal();
}

// wlan 2.4G msg
HWTEST_F(LNNLaneTestMock, LANE_REQUEST_Test_001, TestSize.Level1)
{
    LaneType laneType = LANE_TYPE_TRANS;
    uint32_t laneId = ApplyLaneId(laneType);
    EXPECT_TRUE(laneId != INVALID_LANE_ID);

    LaneDepsInterfaceMock mock;
    mock.SetDefaultResult();
    EXPECT_CALL(mock, LnnGetLocalNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<1>(16), Return(SOFTBUS_OK)));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<2>(16), Return(SOFTBUS_OK)));

    auto linkedInfo = [](LnnWlanLinkedInfo *info) {
        info->band = WLAN_2PG_BAND;
        info->frequency = WLAN_2PG_FREQUENCY;
        info->isConnected = true;};
    EXPECT_CALL(mock, LnnGetWlanLinkedInfo)
        .WillRepeatedly(DoAll(WithArg<0>(linkedInfo), Return(SOFTBUS_OK)));
    
    auto setLinkedInfo = [](SoftBusWifiLinkedInfo *info) {
        info->band = WLAN_2PG_BAND;
        info->frequency = WLAN_2PG_FREQUENCY;
        info->connState = SOFTBUS_API_WIFI_CONNECTED;};

    LnnWifiAdpterInterfaceMock wifiMock;
    EXPECT_CALL(wifiMock, SoftBusGetLinkedInfo)
        .WillRepeatedly(DoAll(WithArg<0>(setLinkedInfo), Return(SOFTBUS_OK)));

    ILaneListener listener = {
        .OnLaneRequestSuccess = OnLaneRequestSuccess,
        .OnLaneRequestFail = OnLaneRequestFail,
        .OnLaneStateChange = OnLaneStateChange,
    };
    LaneRequestOption requestOption;
    (void)memset_s(&requestOption, sizeof(LaneRequestOption), 0, sizeof(LaneRequestOption));
    requestOption.type = laneType;
    (void)strncpy_s(requestOption.requestInfo.trans.networkId, NETWORK_ID_BUF_LEN,
        NODE_NETWORK_ID, strlen(NODE_NETWORK_ID));
    requestOption.requestInfo.trans.transType = LANE_T_MSG;
    requestOption.requestInfo.trans.expectedBw = 0;
    int32_t ret = LnnRequestLane(laneId, &requestOption, &listener);
    EXPECT_EQ(ret, SOFTBUS_OK);
    CondWait();
}

// wlan 5G byte
HWTEST_F(LNNLaneTestMock, LANE_REQUEST_Test_002, TestSize.Level1)
{
    LaneType laneType = LANE_TYPE_TRANS;
    uint32_t laneId = ApplyLaneId(laneType);
    EXPECT_TRUE(laneId != INVALID_LANE_ID);

    LaneDepsInterfaceMock mock;
    mock.SetDefaultResult();
    EXPECT_CALL(mock, LnnGetOnlineStateById).WillRepeatedly(Return(true));
    EXPECT_CALL(mock, LnnGetLocalNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<1>(32), Return(SOFTBUS_OK)));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<2>(32), Return(SOFTBUS_OK)));

    auto linkedInfo = [](LnnWlanLinkedInfo *info) {
        info->band = WLAN_5G_BAND;
        info->frequency = WLAN_5G_FREQUENCY;
        info->isConnected = true;};
    EXPECT_CALL(mock, LnnGetWlanLinkedInfo)
        .WillRepeatedly(DoAll(WithArg<0>(linkedInfo), Return(SOFTBUS_OK)));
    
    auto setLinkedInfo = [](SoftBusWifiLinkedInfo *info) {
        info->band = WLAN_5G_BAND;
        info->frequency = WLAN_5G_FREQUENCY;
        info->connState = SOFTBUS_API_WIFI_CONNECTED;};

    LnnWifiAdpterInterfaceMock wifiMock;
    EXPECT_CALL(wifiMock, SoftBusGetLinkedInfo)
        .WillRepeatedly(DoAll(WithArg<0>(setLinkedInfo), Return(SOFTBUS_OK)));

    ILaneListener listener = {
        .OnLaneRequestSuccess = OnLaneRequestSuccess,
        .OnLaneRequestFail = OnLaneRequestFail,
        .OnLaneStateChange = OnLaneStateChange,
    };
    LaneRequestOption requestOption;
    (void)memset_s(&requestOption, sizeof(LaneRequestOption), 0, sizeof(LaneRequestOption));
    requestOption.type = laneType;
    (void)strncpy_s(requestOption.requestInfo.trans.networkId, NETWORK_ID_BUF_LEN,
        NODE_NETWORK_ID, strlen(NODE_NETWORK_ID));
    requestOption.requestInfo.trans.transType = LANE_T_BYTE;
    requestOption.requestInfo.trans.expectedBw = 0;
    int32_t ret = LnnRequestLane(laneId, &requestOption, &listener);
    EXPECT_EQ(ret, SOFTBUS_OK);
    CondWait();
}

// P2P MSG
HWTEST_F(LNNLaneTestMock, LANE_REQUEST_Test_003, TestSize.Level1)
{
    LaneType laneType = LANE_TYPE_TRANS;
    uint32_t laneId = ApplyLaneId(laneType);
    EXPECT_TRUE(laneId != INVALID_LANE_ID);

    LaneDepsInterfaceMock mock;
    mock.SetDefaultResult();
    EXPECT_CALL(mock, LnnGetOnlineStateById).WillRepeatedly(Return(true));
    EXPECT_CALL(mock, LnnGetLocalNumInfo(NUM_KEY_NET_CAP, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(62), Return(SOFTBUS_OK)));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo(_, NUM_KEY_NET_CAP, _))
        .WillRepeatedly(DoAll(SetArgPointee<2>(62), Return(SOFTBUS_OK)));

    ILaneListener listener = {
        .OnLaneRequestSuccess = OnLaneRequestSuccess,
        .OnLaneRequestFail = OnLaneRequestFail,
        .OnLaneStateChange = OnLaneStateChange,
    };
    LaneRequestOption requestOption;
    (void)memset_s(&requestOption, sizeof(LaneRequestOption), 0, sizeof(LaneRequestOption));
    requestOption.type = laneType;
    (void)strncpy_s(requestOption.requestInfo.trans.networkId, NETWORK_ID_BUF_LEN,
        NODE_NETWORK_ID, strlen(NODE_NETWORK_ID));
    requestOption.requestInfo.trans.transType = LANE_T_MSG;
    requestOption.requestInfo.trans.expectedBw = 0;
    requestOption.requestInfo.trans.expectedLink.linkTypeNum = 1;
    requestOption.requestInfo.trans.expectedLink.linkType[0] = LANE_P2P;
    int32_t ret = LnnRequestLane(laneId, &requestOption, &listener);
    EXPECT_EQ(ret, SOFTBUS_ERR);
    CondWait();
}

// WLAN 5G RAW-STREAM
HWTEST_F(LNNLaneTestMock, LANE_REQUEST_Test_004, TestSize.Level1)
{
    LaneType laneType = LANE_TYPE_TRANS;
    uint32_t laneId = ApplyLaneId(laneType);
    EXPECT_TRUE(laneId != INVALID_LANE_ID);

    LaneDepsInterfaceMock mock;
    mock.SetDefaultResult();
    EXPECT_CALL(mock, LnnGetOnlineStateById).WillRepeatedly(Return(true));
    EXPECT_CALL(mock, LnnGetLocalNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<1>(32), Return(SOFTBUS_OK)));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<2>(32), Return(SOFTBUS_OK)));
    
    auto linkedInfo = [](LnnWlanLinkedInfo *info) {
        info->band = WLAN_5G_BAND;
        info->frequency = WLAN_5G_FREQUENCY;
        info->isConnected = true;};
    EXPECT_CALL(mock, LnnGetWlanLinkedInfo)
        .WillRepeatedly(DoAll(WithArg<0>(linkedInfo), Return(SOFTBUS_OK)));
    
    auto setLinkedInfo = [](SoftBusWifiLinkedInfo *info) {
        info->band = WLAN_5G_BAND;
        info->frequency = WLAN_5G_FREQUENCY;
        info->connState = SOFTBUS_API_WIFI_CONNECTED;};

    LnnWifiAdpterInterfaceMock wifiMock;
    EXPECT_CALL(wifiMock, SoftBusGetLinkedInfo)
        .WillRepeatedly(DoAll(WithArg<0>(setLinkedInfo), Return(SOFTBUS_OK)));

    ILaneListener listener = {
        .OnLaneRequestSuccess = OnLaneRequestSuccess,
        .OnLaneRequestFail = OnLaneRequestFail,
        .OnLaneStateChange = OnLaneStateChange,
    };
    LaneRequestOption requestOption;
    (void)memset_s(&requestOption, sizeof(LaneRequestOption), 0, sizeof(LaneRequestOption));
    requestOption.type = laneType;
    (void)strncpy_s(requestOption.requestInfo.trans.networkId, NETWORK_ID_BUF_LEN,
        NODE_NETWORK_ID, strlen(NODE_NETWORK_ID));
    requestOption.requestInfo.trans.transType = LANE_T_RAW_STREAM;
    requestOption.requestInfo.trans.expectedBw = 0;
    int32_t ret = LnnRequestLane(laneId, &requestOption, &listener);
    EXPECT_EQ(ret, SOFTBUS_OK);
    CondWait();
}

// P2P FILE
HWTEST_F(LNNLaneTestMock, LANE_REQUEST_Test_005, TestSize.Level1)
{
    LaneType laneType = LANE_TYPE_TRANS;
    uint32_t laneId = ApplyLaneId(laneType);
    EXPECT_TRUE(laneId != INVALID_LANE_ID);

    LaneDepsInterfaceMock mock;
    mock.SetDefaultResult();
    EXPECT_CALL(mock, LnnGetOnlineStateById).WillRepeatedly(Return(true));
    EXPECT_CALL(mock, LnnGetLocalNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<1>(62), Return(SOFTBUS_OK)));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<2>(62), Return(SOFTBUS_OK)));

    EXPECT_CALL(mock, AuthGetPreferConnInfo).WillRepeatedly(Return(SOFTBUS_OK));
    EXPECT_CALL(mock, AuthOpenConn).WillRepeatedly(Return(SOFTBUS_OK));

    ILaneListener listener = {
        .OnLaneRequestSuccess = OnLaneRequestSuccess,
        .OnLaneRequestFail = OnLaneRequestFail,
        .OnLaneStateChange = OnLaneStateChange,
    };
    LaneRequestOption requestOption;
    (void)memset_s(&requestOption, sizeof(LaneRequestOption), 0, sizeof(LaneRequestOption));
    requestOption.type = laneType;
    (void)strncpy_s(requestOption.requestInfo.trans.networkId, NETWORK_ID_BUF_LEN,
        NODE_NETWORK_ID, strlen(NODE_NETWORK_ID));
    requestOption.requestInfo.trans.transType = LANE_T_RAW_STREAM;
    requestOption.requestInfo.trans.pid = DEFAULT_PID;
    requestOption.requestInfo.trans.expectedBw = 0;
    requestOption.requestInfo.trans.expectedLink.linkTypeNum = 1;
    requestOption.requestInfo.trans.expectedLink.linkType[0] = LANE_P2P;
    int32_t ret = LnnRequestLane(laneId, &requestOption, &listener);
    EXPECT_EQ(ret, SOFTBUS_OK);
    CondWait();
}

// request failue
HWTEST_F(LNNLaneTestMock, LANE_REQUEST_Test_006, TestSize.Level1)
{
    LaneType laneType = LANE_TYPE_TRANS;
    uint32_t laneId = ApplyLaneId(laneType);
    EXPECT_TRUE(laneId != INVALID_LANE_ID);
    LaneRequestOption requestOption;
    (void)memset_s(&requestOption, sizeof(LaneRequestOption), 0, sizeof(LaneRequestOption));
    requestOption.type = LANE_TYPE_BUTT;
    ILaneListener listener = {
        .OnLaneRequestSuccess = OnLaneRequestSuccess,
        .OnLaneRequestFail = OnLaneRequestFail,
        .OnLaneStateChange = OnLaneStateChange,
    };
    int32_t ret = LnnRequestLane(laneId, &requestOption, nullptr);
    EXPECT_EQ(ret, SOFTBUS_ERR);
    ret = LnnRequestLane(laneId, nullptr, &listener);
    EXPECT_EQ(ret, SOFTBUS_ERR);
    laneId = 0xFFFFFFFF;
    ret = LnnRequestLane(laneId, &requestOption, &listener);
    EXPECT_EQ(ret, SOFTBUS_ERR);
    requestOption.type = LANE_TYPE_BUTT;
    ret = LnnRequestLane(laneId, &requestOption, &listener);
    EXPECT_EQ(ret, SOFTBUS_ERR);
}

//  Free Lane
HWTEST_F(LNNLaneTestMock, LANE_FREE_001, TestSize.Level1)
{
    LaneType laneType = LANE_TYPE_BUTT;
    uint32_t laneId = ApplyLaneId(laneType);
    int32_t ret = LnnFreeLane(laneId);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    laneType = LANE_TYPE_TRANS;
    laneId = ApplyLaneId(laneType);
    EXPECT_TRUE(laneId != INVALID_LANE_ID);
    ret = LnnFreeLane(laneId);
    EXPECT_EQ(ret, SOFTBUS_OK);
    ret = LnnFreeLane(laneId);
    EXPECT_EQ(ret, SOFTBUS_OK);
}

static void LaneIdEnabled(uint32_t laneId, uint32_t laneProfileId)
{
    return;
}

// register
HWTEST_F(LNNLaneTestMock, LANE_REGISTER_001, TestSize.Level1)
{
    RegisterLaneIdListener(nullptr);

    const ILaneIdStateListener cb = {
        .OnLaneIdEnabled = LaneIdEnabled,
        .OnLaneIdDisabled = nullptr,
    };
    RegisterLaneIdListener(&cb);

    const ILaneIdStateListener listener = {
        .OnLaneIdEnabled = nullptr,
        .OnLaneIdDisabled = LaneIdEnabled,
    };
    RegisterLaneIdListener(&listener);

    UnregisterLaneIdListener(nullptr);
}

// LaneInfoProcess BR
HWTEST_F(LNNLaneTestMock, LANE_INFO_001, TestSize.Level1)
{
    LaneLinkInfo info;
    (void)memset_s(&info, sizeof(LaneLinkInfo), 0, sizeof(LaneLinkInfo));
    info.type = LANE_BR;
    LaneConnInfo connInfo;
    (void)memset_s(&connInfo, sizeof(LaneConnInfo), 0, sizeof(LaneConnInfo));
    LaneProfile profile;
    (void)memset_s(&profile, sizeof(LaneProfile), 0, sizeof(LaneProfile));
    int32_t ret = LaneInfoProcess(&info, &connInfo, &profile);
    EXPECT_EQ(ret, SOFTBUS_OK);
}

// LaneInfoProcess BLE
HWTEST_F(LNNLaneTestMock, LANE_INFO_002, TestSize.Level1)
{
    LaneLinkInfo info;
    (void)memset_s(&info, sizeof(LaneLinkInfo), 0, sizeof(LaneLinkInfo));
    info.type = LANE_BLE;
    LaneConnInfo connInfo;
    (void)memset_s(&connInfo, sizeof(LaneConnInfo), 0, sizeof(LaneConnInfo));
    LaneProfile profile;
    (void)memset_s(&profile, sizeof(LaneProfile), 0, sizeof(LaneProfile));
    int32_t ret = LaneInfoProcess(&info, &connInfo, &profile);
    EXPECT_EQ(ret, SOFTBUS_OK);
}

// LaneInfoProcess P2P
HWTEST_F(LNNLaneTestMock, LANE_INFO_003, TestSize.Level1)
{
    LaneLinkInfo info;
    (void)memset_s(&info, sizeof(LaneLinkInfo), 0, sizeof(LaneLinkInfo));
    info.type = LANE_P2P;
    LaneConnInfo connInfo;
    (void)memset_s(&connInfo, sizeof(LaneConnInfo), 0, sizeof(LaneConnInfo));
    LaneProfile profile;
    (void)memset_s(&profile, sizeof(LaneProfile), 0, sizeof(LaneProfile));
    int32_t ret = LaneInfoProcess(&info, &connInfo, &profile);
    EXPECT_EQ(ret, SOFTBUS_OK);
}

// LaneInfoProcess fail
HWTEST_F(LNNLaneTestMock, LANE_INFO_004, TestSize.Level1)
{
    LaneLinkInfo info;
    (void)memset_s(&info, sizeof(LaneLinkInfo), 0, sizeof(LaneLinkInfo));
    info.type = LANE_LINK_TYPE_BUTT;
    LaneConnInfo *connInfo = nullptr;
    LaneProfile *profile = nullptr;
    int32_t ret = LaneInfoProcess(nullptr, connInfo, profile);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    ret = LaneInfoProcess(&info, nullptr, profile);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    ret = LaneInfoProcess(&info, connInfo, nullptr);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    ret = LaneInfoProcess(&info, connInfo, profile);
    EXPECT_EQ(ret, SOFTBUS_ERR);
}

// lnn data
HWTEST_F(LNNLaneTestMock, LNN_DATA_001, TestSize.Level1)
{
    int32_t ret = LnnCreateData(nullptr, 32, nullptr, 0);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    LnnDeleteData(nullptr, 32);
}

// get time
HWTEST_F(LNNLaneTestMock, LNN_GET_TIME_001, TestSize.Level1)
{
    LnnGetSysTimeMs();
}

// LaneProfile
HWTEST_F(LNNLaneTestMock, LNN_LANE_PROFILE_001, TestSize.Level1)
{
    uint32_t laneId = 0x10000001;
    int32_t ret = BindLaneIdToProfile(laneId, nullptr);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    LaneProfile profile;
    (void)memset_s(&profile, sizeof(LaneProfile), 0, sizeof(LaneProfile));
    ret = BindLaneIdToProfile(laneId, &profile);
    EXPECT_EQ(ret, SOFTBUS_OK);

    profile.linkType = LANE_P2P;
    profile.content = LANE_T_FILE;
    profile.priority = LANE_PRI_LOW;
    ret = BindLaneIdToProfile(laneId, &profile);
    EXPECT_EQ(ret, SOFTBUS_OK);

    LaneGenerateParam param;
    (void)memset_s(&param, sizeof(LaneGenerateParam), 0, sizeof(LaneGenerateParam));
    param.linkType = LANE_P2P;
    param.transType = LANE_T_FILE;
    param.priority = LANE_PRI_LOW;
    uint32_t profileId = GenerateLaneProfileId(&param);

    ret = GetLaneProfile(profileId, &profile);
    EXPECT_EQ(ret, SOFTBUS_OK);

    ret = GetLaneProfile(profileId, nullptr);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    uint32_t *laneIdList = nullptr;
    uint32_t listSize = 0;
    ret = GetLaneIdList(profileId, &laneIdList, &listSize);
    EXPECT_EQ(ret, SOFTBUS_OK);
    SoftBusFree(laneIdList);

    (void)GetActiveProfileNum();

    (void)UnbindLaneIdFromProfile(laneId, profileId);

    (void)UnbindLaneIdFromProfile(0, profileId);
}

// select lane
HWTEST_F(LNNLaneTestMock, LNN_SELECT_LANE_001, TestSize.Level1)
{
    LaneLinkType *recommendList = nullptr;
    uint32_t listNum = 0;
    LaneSelectParam selectParam;
    (void)memset_s(&selectParam, sizeof(LaneSelectParam), 0, sizeof(LaneSelectParam));
    selectParam.transType = LANE_T_FILE;
    selectParam.expectedBw = 0;
    selectParam.list.linkTypeNum = 2;
    selectParam.list.linkType[0] = LANE_WLAN_5G;
    selectParam.list.linkType[1] = LANE_LINK_TYPE_BUTT;
    int32_t ret = SelectLane(NODE_NETWORK_ID, nullptr, &recommendList, &listNum);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    LaneDepsInterfaceMock mock;
    mock.SetDefaultResult();

    EXPECT_CALL(mock, LnnGetLocalNumInfo).WillRepeatedly(Return(SOFTBUS_ERR));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo).WillRepeatedly(Return(SOFTBUS_ERR));
    EXPECT_CALL(mock, LnnGetWlanLinkedInfo).WillRepeatedly(Return(SOFTBUS_ERR));
    EXPECT_CALL(mock, LnnGetOnlineStateById).WillRepeatedly(Return(false));

    ret = SelectLane(NODE_NETWORK_ID, &selectParam, &recommendList, &listNum);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    EXPECT_CALL(mock, LnnGetLocalNumInfo).WillRepeatedly(Return(SOFTBUS_OK));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo).WillRepeatedly(Return(SOFTBUS_OK));
    EXPECT_CALL(mock, LnnGetWlanLinkedInfo).WillRepeatedly(Return(SOFTBUS_OK));
    EXPECT_CALL(mock, LnnGetOnlineStateById).WillRepeatedly(Return(true));
    ret = SelectLane(NODE_NETWORK_ID, &selectParam, &recommendList, &listNum);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    selectParam.transType = LANE_T_MIX;
    ret = SelectLane(NODE_NETWORK_ID, &selectParam, &recommendList, &listNum);
    EXPECT_EQ(ret, SOFTBUS_ERR);
    SoftBusFree(recommendList);
}

// select lane
HWTEST_F(LNNLaneTestMock, LNN_SELECT_LANE_002, TestSize.Level1)
{
    LaneLinkType *recommendList = nullptr;
    uint32_t listNum = 0;
    LaneSelectParam selectParam;
    (void)memset_s(&selectParam, sizeof(LaneSelectParam), 0, sizeof(LaneSelectParam));
    selectParam.transType = LANE_T_FILE;
    selectParam.expectedBw = 0;
    selectParam.list.linkTypeNum = DEFAULT_SELECT_NUM;
    selectParam.list.linkType[0] = LANE_BLE;
    selectParam.list.linkType[1] = LANE_WLAN_2P4G;
    selectParam.list.linkType[2] = LANE_WLAN_5G;
    selectParam.list.linkType[3] = LANE_BR;

    LaneDepsInterfaceMock mock;
    mock.SetDefaultResult();
    EXPECT_CALL(mock, LnnGetLocalNumInfo).WillRepeatedly(Return(SOFTBUS_ERR));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo).WillRepeatedly(Return(SOFTBUS_ERR));
    EXPECT_CALL(mock, LnnGetWlanLinkedInfo).WillRepeatedly(Return(SOFTBUS_ERR));
    int32_t ret = SelectLane(NODE_NETWORK_ID, &selectParam, &recommendList, &listNum);
    EXPECT_EQ(ret, SOFTBUS_ERR);

    EXPECT_CALL(mock, LnnGetLocalNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<1>(1), Return(SOFTBUS_OK)));
    EXPECT_CALL(mock, LnnGetRemoteNumInfo)
        .WillRepeatedly(DoAll(SetArgPointee<2>(1), Return(SOFTBUS_OK)));
    ret = SelectLane(NODE_NETWORK_ID, &selectParam, &recommendList, &listNum);
    EXPECT_EQ(ret, SOFTBUS_OK);
    SoftBusFree(recommendList);
}
} // namespace OHOS