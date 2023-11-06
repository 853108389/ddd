/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "lnn_lane_common.h"

#include <securec.h>

#include "lnn_lane_def.h"
#include "lnn_lane_interface.h"
#include "lnn_lane_link.h"
#include "lnn_lane_model.h"
#include "lnn_map.h"
#include "message_handler.h"
#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_log.h"

#define UINT_TO_STR_MAX_LEN 11

typedef int32_t (*LinkInfoProc)(const LaneLinkInfo *, LaneConnInfo *, LaneProfile *);

static int32_t BrInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_BR;
    if (memcpy_s(connInfo->connInfo.br.brMac, BT_MAC_LEN,
        linkInfo->linkInfo.br.brMac, BT_MAC_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    profile->linkType = LANE_BR;
    return SOFTBUS_OK;
}

static int32_t BleInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_BLE;
    if (memcpy_s(connInfo->connInfo.ble.bleMac, BT_MAC_LEN,
        linkInfo->linkInfo.ble.bleMac, BT_MAC_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    if (memcpy_s(connInfo->connInfo.ble.deviceIdHash, UDID_HASH_LEN,
        linkInfo->linkInfo.ble.deviceIdHash, UDID_HASH_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    connInfo->connInfo.ble.protoType = linkInfo->linkInfo.ble.protoType;
    connInfo->connInfo.ble.psm = linkInfo->linkInfo.ble.psm;
    profile->linkType = LANE_BLE;
    return SOFTBUS_OK;
}

static int32_t P2pInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_P2P;
    if (memcpy_s(&connInfo->connInfo.p2p, sizeof(P2pConnInfo),
        &linkInfo->linkInfo.p2p.connInfo, sizeof(P2pConnInfo)) != EOK) {
        return SOFTBUS_ERR;
    }
    profile->linkType = LANE_P2P;
    profile->bw = linkInfo->linkInfo.p2p.bw;
    profile->phyChannel = linkInfo->linkInfo.p2p.channel;
    return SOFTBUS_OK;
}

static int32_t HmlInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_HML;
    if (memcpy_s(&connInfo->connInfo.p2p, sizeof(P2pConnInfo),
        &linkInfo->linkInfo.p2p.connInfo, sizeof(P2pConnInfo)) != EOK) {
        return SOFTBUS_ERR;
    }
    profile->linkType = LANE_HML;
    profile->bw = linkInfo->linkInfo.p2p.bw;
    profile->phyChannel = linkInfo->linkInfo.p2p.channel;
    return SOFTBUS_OK;
}

static int32_t P2pReuseInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_P2P_REUSE;
    connInfo->connInfo.wlan = linkInfo->linkInfo.wlan.connInfo;
    profile->linkType = LANE_P2P_REUSE;
    return SOFTBUS_OK;
}

static int32_t Wlan2P4GInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_WLAN_2P4G;
    if (memcpy_s(&connInfo->connInfo.wlan, sizeof(WlanConnInfo),
        &linkInfo->linkInfo.wlan.connInfo, sizeof(WlanConnInfo)) != EOK) {
        return SOFTBUS_ERR;
    }
    profile->linkType = LANE_WLAN_2P4G;
    profile->bw = linkInfo->linkInfo.wlan.bw;
    profile->phyChannel = linkInfo->linkInfo.wlan.channel;
    return SOFTBUS_OK;
}

static int32_t Wlan5GInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_WLAN_5G;
    if (memcpy_s(&connInfo->connInfo.wlan, sizeof(WlanConnInfo),
        &linkInfo->linkInfo.wlan.connInfo, sizeof(WlanConnInfo)) != EOK) {
        return SOFTBUS_ERR;
    }
    profile->linkType = LANE_WLAN_5G;
    profile->bw = linkInfo->linkInfo.wlan.bw;
    profile->phyChannel = linkInfo->linkInfo.wlan.channel;
    return SOFTBUS_OK;
}

static int32_t BleDirectInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    if (memcpy_s(connInfo->connInfo.bleDirect.nodeIdHash, NODEID_SHORT_HASH_LEN,
        linkInfo->linkInfo.bleDirect.nodeIdHash, NODEID_SHORT_HASH_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    if (memcpy_s(connInfo->connInfo.bleDirect.localUdidHash, UDID_SHORT_HASH_LEN,
        linkInfo->linkInfo.bleDirect.localUdidHash, UDID_SHORT_HASH_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    if (memcpy_s(connInfo->connInfo.bleDirect.peerUdidHash, SHA_256_HASH_LEN,
        linkInfo->linkInfo.bleDirect.peerUdidHash, SHA_256_HASH_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    connInfo->type = LANE_BLE_DIRECT;
    connInfo->connInfo.bleDirect.protoType = linkInfo->linkInfo.bleDirect.protoType;
    profile->linkType = LANE_BLE_DIRECT;
    return SOFTBUS_OK;
}

static int32_t CocInfoProc(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    connInfo->type = LANE_COC;
    if (memcpy_s(connInfo->connInfo.ble.bleMac, BT_MAC_LEN,
        linkInfo->linkInfo.ble.bleMac, BT_MAC_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    if (memcpy_s(connInfo->connInfo.ble.deviceIdHash, UDID_HASH_LEN,
        linkInfo->linkInfo.ble.deviceIdHash, UDID_HASH_LEN) != EOK) {
        return SOFTBUS_ERR;
    }
    connInfo->connInfo.ble.psm = linkInfo->linkInfo.ble.psm;
    profile->linkType = LANE_COC;
    return SOFTBUS_OK;
}

static LinkInfoProc g_funcList[LANE_LINK_TYPE_BUTT] = {
    [LANE_BR] = BrInfoProc,
    [LANE_BLE] = BleInfoProc,
    [LANE_P2P] = P2pInfoProc,
    [LANE_HML] = HmlInfoProc,
    [LANE_WLAN_2P4G] = Wlan2P4GInfoProc,
    [LANE_WLAN_5G] = Wlan5GInfoProc,
    [LANE_P2P_REUSE] = P2pReuseInfoProc,
    [LANE_BLE_DIRECT] = BleDirectInfoProc,
    [LANE_COC] = CocInfoProc,
    // CoC reuse gatt direct
    [LANE_COC_DIRECT] = BleDirectInfoProc,
};

int32_t LaneInfoProcess(const LaneLinkInfo *linkInfo, LaneConnInfo *connInfo, LaneProfile *profile)
{
    if ((linkInfo == NULL) || (connInfo == NULL) || (profile == NULL)) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "laneInfoProcess param invalid");
        return SOFTBUS_ERR;
    }
    if ((linkInfo->type >= LANE_LINK_TYPE_BUTT) || (g_funcList[linkInfo->type] == NULL)) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "unsupport linkType[%d]", linkInfo->type);
        return SOFTBUS_ERR;
    }
    return g_funcList[linkInfo->type](linkInfo, connInfo, profile);
}

int32_t LnnCreateData(Map *map, uint32_t key, const void *value, uint32_t valueSize)
{
    char keyStr[UINT_TO_STR_MAX_LEN] = {0};
    if (sprintf_s(keyStr, UINT_TO_STR_MAX_LEN, "%u", key) < 0) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "[create]convert dataType fail");
        return SOFTBUS_ERR;
    }
    if (LnnMapSet(map, (const char *)keyStr, value, valueSize) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "save data fail");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

void *LnnReadData(const Map *map, uint32_t key)
{
    char keyStr[UINT_TO_STR_MAX_LEN] = {0};
    if (sprintf_s(keyStr, UINT_TO_STR_MAX_LEN, "%u", key) < 0) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "[read]convert dataType fail");
        return NULL;
    }
    void *data = LnnMapGet(map, (const char *)keyStr);
    return data;
}

void LnnDeleteData(Map *map, uint32_t key)
{
    char keyStr[UINT_TO_STR_MAX_LEN] = {0};
    if (sprintf_s(keyStr, UINT_TO_STR_MAX_LEN, "%u", key) < 0) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "[delete]convert dataType fail");
        return;
    }
    if (LnnMapErase(map, (const char *)keyStr) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "delete data fail");
        return;
    }
}

uint64_t LnnGetSysTimeMs(void)
{
    return SoftBusGetSysTimeMs();
}

int32_t LnnInitLaneLooper(void)
{
    SoftBusLooper *looper = CreateNewLooper("Lane-looper");
    if (!looper) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "init laneLooper fail");
        return SOFTBUS_ERR;
    }
    SetLooper(LOOP_TYPE_LANE, looper);
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "init laneLooper success");
    return SOFTBUS_OK;
}

void LnnDeinitLaneLooper(void)
{
    SoftBusLooper *looper = GetLooper(LOOP_TYPE_LANE);
    if (looper != NULL) {
        DestroyLooper(looper);
        SetLooper(LOOP_TYPE_LANE, NULL);
    }
}
