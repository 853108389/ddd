/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "lnn_sync_item_info.h"

#include <securec.h>

#include "lnn_distributed_net_ledger.h"
#include "lnn_local_net_ledger.h"
#include "lnn_sync_info_manager.h"
#include "softbus_adapter_mem.h"
#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_wifi_api_adapter.h"
#include "softbus_adapter_socket.h"
#include "lnn_net_builder.h"
#include "lnn_connection_addr_utils.h"

#define CONN_CODE_SHIFT 16
#define DISCOVERY_TYPE_MASK 0x7FFF

static int32_t FillTargetWifiConfig(const unsigned char *targetBssid, const char *ssid,
                                    const SoftBusWifiDevConf *conWifiConf, SoftBusWifiDevConf *targetWifiConf)
{
    if (strcpy_s(targetWifiConf->ssid, sizeof(targetWifiConf->ssid), ssid) != EOK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "str copy ssid fail");
        return SOFTBUS_ERR;
    }

    if (memcpy_s(targetWifiConf->bssid, sizeof(targetWifiConf->bssid),
        targetBssid, sizeof(targetWifiConf->bssid)) != EOK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "mem copy bssid fail");
        return SOFTBUS_ERR;
    }

    if (strcpy_s(targetWifiConf->preSharedKey, sizeof(targetWifiConf->preSharedKey),
        conWifiConf->preSharedKey) != EOK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "str copy ssid fail");
        return SOFTBUS_ERR;
    }

    targetWifiConf->securityType = conWifiConf->securityType;
    targetWifiConf->isHiddenSsid = conWifiConf->isHiddenSsid;
    return SOFTBUS_OK;
}

static void ResultClean(SoftBusWifiDevConf *result)
{
    (void)memset_s(result, sizeof(SoftBusWifiDevConf) * WIFI_MAX_CONFIG_SIZE, 0,
                   sizeof(SoftBusWifiDevConf) * WIFI_MAX_CONFIG_SIZE);
    SoftBusFree(result);
}

static int32_t WifiConnectToTargetAp(const unsigned char *targetBssid, const char *ssid)
{
    SoftBusWifiDevConf *result = NULL;
    uint32_t wifiConfigSize;
    int32_t retVal;
    SoftBusWifiDevConf targetDeviceConf;
    uint32_t i;

    result = SoftBusMalloc(sizeof(SoftBusWifiDevConf) * WIFI_MAX_CONFIG_SIZE);
    if (result == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "malloc wifi device config fail");
        return SOFTBUS_ERR;
    }
    (void)memset_s(&targetDeviceConf, sizeof(SoftBusWifiDevConf), 0, sizeof(SoftBusWifiDevConf));
    (void)memset_s(result, sizeof(SoftBusWifiDevConf) * WIFI_MAX_CONFIG_SIZE, 0,
                   sizeof(SoftBusWifiDevConf) * WIFI_MAX_CONFIG_SIZE);
    retVal = SoftBusGetWifiDeviceConfig(result, &wifiConfigSize);
    if (retVal != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "Get wifi device config fail");
        ResultClean(result);
        return SOFTBUS_ERR;
    }

    if (wifiConfigSize > WIFI_MAX_CONFIG_SIZE) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "wifi device config size is invalid.");
        ResultClean(result);
        return SOFTBUS_ERR;
    }

    for (i = 0; i < wifiConfigSize; i++) {
        if (strcmp(ssid, (result + i)->ssid) != 0) {
            continue;
        }
        if (FillTargetWifiConfig(targetBssid, ssid, &targetDeviceConf, result + i) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "fill device config failed.");
            (void)memset_s(&targetDeviceConf, sizeof(SoftBusWifiDevConf), 0, sizeof(SoftBusWifiDevConf));
            ResultClean(result);
            return SOFTBUS_ERR;
        }
        break;
    }
    if (SoftBusDisconnectDevice() != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "dis connect device failed.");
        (void)memset_s(&targetDeviceConf, sizeof(SoftBusWifiDevConf), 0, sizeof(SoftBusWifiDevConf));
        ResultClean(result);
        return SOFTBUS_ERR;
    }

    if (SoftBusConnectToDevice(&targetDeviceConf) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "connect to target ap failed.");
        (void)memset_s(&targetDeviceConf, sizeof(SoftBusWifiDevConf), 0, sizeof(SoftBusWifiDevConf));
        ResultClean(result);
        return SOFTBUS_ERR;
    }
    (void)memset_s(&targetDeviceConf, sizeof(SoftBusWifiDevConf), 0, sizeof(SoftBusWifiDevConf));
    ResultClean(result);
    return SOFTBUS_OK;
}

void OnReceiveDeviceName(LnnSyncInfoType type, const char *networkId, const uint8_t *msg, uint32_t len)
{
    char udid[UDID_BUF_LEN];
    BssTransInfo *bssTranInfo = NULL;
    if (msg == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "%s: msg is null", __func__);
        return;
    }
    if (type != LNN_INFO_TYPE_DEVICE_NAME) {
        return;
    }
    if (LnnConvertDlId(networkId, CATEGORY_NETWORK_ID, CATEGORY_UDID, udid, UDID_BUF_LEN) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "convert networkId to udid fail");
        return;
    }
    bssTranInfo = (BssTransInfo *)msg;
    if (WifiConnectToTargetAp(bssTranInfo->targetBssid, bssTranInfo->ssid) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "wifi connect to target ap failed");
    }
}

void OnReceiveTransReqMsg(LnnSyncInfoType type, const char *networkId, const uint8_t *msg, uint32_t len)
{
    char udid[UDID_BUF_LEN];

    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "recv trans req msg: %d, len: %d", type, len);
    if (type != LNN_INFO_TYPE_BSS_TRANS) {
        return;
    }
    if (LnnConvertDlId(networkId, CATEGORY_NETWORK_ID, CATEGORY_UDID, udid, UDID_BUF_LEN) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "convert networkId to udid fail");
        return;
    }
    if (!LnnSetDLDeviceInfoName(udid, (char *)msg)) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "set peer device name fail");
    }
}

static void OnReceiveBrOffline(LnnSyncInfoType type, const char *networkId, const uint8_t *msg, uint32_t len)
{
    uint32_t combinedInt;
    char uuid[UUID_BUF_LEN];
    int16_t peerCode, code;
    DiscoveryType discType;

    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "Recv offline info, type:%d, len:%d", type, len);
    if (type != LNN_INFO_TYPE_OFFLINE) {
        return;
    }
    if (msg == NULL || len != sizeof(int32_t)) {
        return;
    }
    combinedInt = *(uint32_t *)msg;
    combinedInt = SoftBusNtoHl(combinedInt);
    peerCode = (int16_t)(combinedInt >> CONN_CODE_SHIFT);
    discType = (DiscoveryType)(combinedInt & DISCOVERY_TYPE_MASK);
    if (LnnConvertDlId(networkId, CATEGORY_NETWORK_ID, CATEGORY_UUID, uuid, UUID_BUF_LEN) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "covert networkId to uuid fail!");
        return;
    }
    code = LnnGetCnnCode(uuid, DISCOVERY_TYPE_BR);
    if (code == INVALID_CONNECTION_CODE_VALUE) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "uuid not exist!");
        return;
    }
    if (discType != DISCOVERY_TYPE_BR || code != peerCode) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "info error discType=%d, code=%d, peerCode=%d!",
            discType, code, peerCode);
        return;
    }
    if (LnnRequestLeaveSpecific(networkId, LnnDiscTypeToConnAddrType(discType)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "request leave specific fail!");
    }
}

int32_t LnnSendTransReq(const char *peerNetWorkId, const BssTransInfo *transInfo)
{
    if (peerNetWorkId == NULL || transInfo == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "para peerNetWorkId or tansInfo is null");
        return SOFTBUS_ERR;
    }

    if (LnnSetDLBssTransInfo(peerNetWorkId, transInfo) != SOFTBUS_OK) {
        LLOGE("save bssTransinfo fail");
        return SOFTBUS_ERR;
    }

    if (LnnSendSyncInfoMsg(LNN_INFO_TYPE_BSS_TRANS, peerNetWorkId,
        (const uint8_t *)transInfo, sizeof(BssTransInfo), NULL) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "send bss info fail");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

NO_SANITIZE("cfi") int32_t LnnInitOffline(void)
{
    return LnnRegSyncInfoHandler(LNN_INFO_TYPE_OFFLINE, OnReceiveBrOffline);
}

NO_SANITIZE("cfi") void LnnDeinitOffline(void)
{
    (void)LnnUnregSyncInfoHandler(LNN_INFO_TYPE_OFFLINE, OnReceiveBrOffline);
}
