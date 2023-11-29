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

#include "lnn_net_builder.h"

#include <securec.h>
#include <stdlib.h>
#include <inttypes.h>

#include "anonymizer.h"
#include "auth_interface.h"
#include "auth_request.h"
#include "auth_request.h"
#include "auth_hichain_adapter.h"
#include "bus_center_event.h"
#include "bus_center_manager.h"
#include "common_list.h"
#include "lnn_async_callback_utils.h"
#include "lnn_battery_info.h"
#include "lnn_cipherkey_manager.h"
#include "lnn_connection_addr_utils.h"
#include "lnn_connection_fsm.h"
#include "lnn_deviceinfo_to_profile.h"
#include "lnn_devicename_info.h"
#include "lnn_discovery_manager.h"
#include "lnn_distributed_net_ledger.h"
#include "lnn_event.h"
#include "lnn_fast_offline.h"
#include "lnn_heartbeat_utils.h"
#include "lnn_link_finder.h"
#include "lnn_local_net_ledger.h"
#include "lnn_log.h"
#include "lnn_network_id.h"
#include "lnn_network_info.h"
#include "lnn_network_manager.h"
#include "lnn_node_info.h"
#include "lnn_node_weight.h"
#include "lnn_p2p_info.h"
#include "lnn_physical_subnet_manager.h"
#include "lnn_sync_info_manager.h"
#include "lnn_sync_item_info.h"
#include "lnn_topo_manager.h"
#include "softbus_adapter_crypto.h"
#include "softbus_adapter_crypto.h"
#include "softbus_adapter_mem.h"
#include "softbus_errcode.h"
#include "softbus_feature_config.h"
#include "softbus_hisysevt_bus_center.h"
#include "softbus_json_utils.h"
#include "softbus_adapter_json.h"
#include "softbus_utils.h"

#define LNN_CONN_CAPABILITY_MSG_LEN 8
#define DEFAULT_MAX_LNN_CONNECTION_COUNT 10
#define JSON_KEY_MASTER_UDID "MasterUdid"
#define JSON_KEY_MASTER_WEIGHT "MasterWeight"
#define NOT_TRUSTED_DEVICE_MSG_DELAY 5000
#define SHORT_UDID_HASH_STR_LEN 16
#define DEFAULT_PKG_NAME "com.huawei.nearby"

typedef enum {
    MSG_TYPE_JOIN_LNN = 0,
    MSG_TYPE_DISCOVERY_DEVICE,
    MSG_TYPE_CLEAN_CONN_FSM,
    MSG_TYPE_VERIFY_RESULT,
    MSG_TYPE_DEVICE_VERIFY_PASS,
    MSG_TYPE_DEVICE_DISCONNECT = 5,
    MSG_TYPE_DEVICE_NOT_TRUSTED,
    MSG_TYPE_LEAVE_LNN,
    MSG_TYPE_SYNC_OFFLINE_FINISH,
    MSG_TYPE_NODE_STATE_CHANGED,
    MSG_TYPE_MASTER_ELECT = 10,
    MSG_TYPE_LEAVE_INVALID_CONN,
    MSG_TYPE_LEAVE_BY_ADDR_TYPE,
    MSG_TYPE_LEAVE_SPECIFIC,
    MSG_TYPE_JOIN_METANODE,
    MSG_TYPE_JOIN_METANODE_AUTH_PASS,
    MSG_TYPE_LEAVE_METANODE,
    MSG_TYPE_JOIN_METANODE_AUTH_FAIL,
    MSG_TYPE_MAX,
} NetBuilderMessageType;

typedef int32_t (*NetBuilderMessageProcess)(const void *para);

typedef struct {
    ListNode node;
    ConnectionAddr addr;
    bool needReportFailure;
} PendingJoinRequestNode;

typedef struct {
    NodeType nodeType;

    /* connection fsm list */
    ListNode fsmList;
    ListNode pendingList;
    ListNode metaNodeList;
    /* connection count */
    int32_t connCount;

    SoftBusLooper *looper;
    SoftBusHandler handler;

    int32_t maxConnCount;
    int32_t maxConcurrentCount;
    bool isInit;
} NetBuilder;

typedef struct {
    uint32_t requestId;
    int32_t retCode;
    int64_t authId;
    NodeInfo *nodeInfo;
} VerifyResultMsgPara;

typedef struct {
    ConnectionAddr addr;
    int64_t authId;
    NodeInfo *nodeInfo;
} DeviceVerifyPassMsgPara;

typedef struct {
    char networkId[NETWORK_ID_BUF_LEN];
    char masterUdid[UDID_BUF_LEN];
    int32_t masterWeight;
} ElectMsgPara;

typedef struct {
    char oldNetworkId[NETWORK_ID_BUF_LEN];
    ConnectionAddrType addrType;
    char newNetworkId[NETWORK_ID_BUF_LEN];
} LeaveInvalidConnMsgPara;

typedef struct {
    char networkId[NETWORK_ID_BUF_LEN];
    ConnectionAddrType addrType;
} SpecificLeaveMsgPara;

typedef struct {
    ConnectionAddr addr;
    CustomData customData;
    char pkgName[PKG_NAME_SIZE_MAX];
    int32_t callingPid;
} ConnectionAddrKey;

typedef struct {
    MetaJoinRequestNode *metaJoinNode;
    int32_t reason;
} MetaReason;

typedef struct {
    MetaJoinRequestNode *metaJoinNode;
    int64_t authMetaId;
    NodeInfo info;
} MetaAuthInfo;

typedef struct {
    char pkgName[PKG_NAME_SIZE_MAX];
    bool isNeedConnect;
    ConnectionAddr addr;
} JoinLnnMsgPara;

typedef struct {
    char pkgName[PKG_NAME_SIZE_MAX];
    char networkId[NETWORK_ID_BUF_LEN];
} LeaveLnnMsgPara;

static NetBuilder g_netBuilder;
static bool g_watchdogFlag = true;

void __attribute__((weak)) SfcSyncNodeAddrHandle(const char *networkId, int32_t code)
{
    (void)networkId;
    (void)code;
}

void SetWatchdogFlag(bool flag)
{
    g_watchdogFlag = flag;
}

bool GetWatchdogFlag(void)
{
    return g_watchdogFlag;
}

static void NetBuilderConfigInit(void)
{
    if (SoftbusGetConfig(SOFTBUS_INT_MAX_LNN_CONNECTION_CNT,
        (unsigned char *)&g_netBuilder.maxConnCount, sizeof(g_netBuilder.maxConnCount)) != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "get lnn max connection count fail, use default value");
        g_netBuilder.maxConnCount = DEFAULT_MAX_LNN_CONNECTION_COUNT;
    }
    if (SoftbusGetConfig(SOFTBUS_INT_LNN_MAX_CONCURRENT_NUM,
        (unsigned char *)&g_netBuilder.maxConcurrentCount, sizeof(g_netBuilder.maxConcurrentCount)) != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "get lnn max conncurent count fail, use default value");
        g_netBuilder.maxConcurrentCount = 0;
    }
    LNN_LOGD(LNN_INIT, "lnn config is %d,%d",
        g_netBuilder.maxConnCount, g_netBuilder.maxConcurrentCount);
}

static SoftBusMessage *CreateNetBuilderMessage(int32_t msgType, void *para)
{
    SoftBusMessage *msg = (SoftBusMessage *)SoftBusCalloc(sizeof(SoftBusMessage));
    if (msg == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc softbus message failed");
        return NULL;
    }
    msg->what = msgType;
    msg->obj = para;
    msg->handler = &g_netBuilder.handler;
    return msg;
}

static int32_t PostMessageToHandler(int32_t msgType, void *para)
{
    SoftBusMessage *msg = CreateNetBuilderMessage(msgType, para);
    if (msg == NULL) {
        LNN_LOGE(LNN_BUILDER, "create softbus message failed");
        return SOFTBUS_ERR;
    }
    g_netBuilder.looper->PostMessage(g_netBuilder.looper, msg);
    return SOFTBUS_OK;
}

static LnnConnectionFsm *FindConnectionFsmByAddr(const ConnectionAddr *addr, bool isShort)
{
    LnnConnectionFsm *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (LnnIsSameConnectionAddr(addr, &item->connInfo.addr, isShort)) {
            return item;
        }
    }
    return NULL;
}

static MetaJoinRequestNode *FindMetaNodeByAddr(const ConnectionAddr *addr)
{
    MetaJoinRequestNode *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.metaNodeList, MetaJoinRequestNode, node) {
        if (LnnIsSameConnectionAddr(addr, &item->addr, false)) {
            return item;
        }
    }
    return NULL;
}

static MetaJoinRequestNode *FindMetaNodeByRequestId(uint32_t requestId)
{
    MetaJoinRequestNode *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.metaNodeList, MetaJoinRequestNode, node) {
        if (item->requestId == requestId) {
            return item;
        }
    }
    return NULL;
}

static LnnConnectionFsm *FindConnectionFsmByRequestId(uint32_t requestId)
{
    LnnConnectionFsm *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (item->connInfo.requestId == requestId) {
            return item;
        }
    }
    return NULL;
}

static LnnConnectionFsm *FindConnectionFsmByAuthId(int64_t authId)
{
    LnnConnectionFsm *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (item->connInfo.authId == authId) {
            return item;
        }
    }
    return NULL;
}

static LnnConnectionFsm *FindConnectionFsmByNetworkId(const char *networkId)
{
    LnnConnectionFsm *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (strcmp(networkId, item->connInfo.peerNetworkId) == 0) {
            return item;
        }
    }
    return NULL;
}

static LnnConnectionFsm *FindConnectionFsmByConnFsmId(uint16_t connFsmId)
{
    LnnConnectionFsm *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (connFsmId == item->id) {
            return item;
        }
    }
    return NULL;
}

static void SetBeginJoinLnnTime(LnnConnectionFsm *connFsm)
{
    connFsm->statisticData.beginJoinLnnTime = LnnUpTimeMs();
}

static LnnConnectionFsm *StartNewConnectionFsm(const ConnectionAddr *addr, const char *pkgName, bool isNeedConnect)
{
    LnnConnectionFsm *connFsm = NULL;

    if (g_netBuilder.connCount >= g_netBuilder.maxConnCount) {
        LNN_LOGE(LNN_BUILDER, "current connection num exceeds max limit=%d",
            g_netBuilder.connCount);
        return NULL;
    }
    connFsm = LnnCreateConnectionFsm(addr, pkgName, isNeedConnect);
    if (connFsm == NULL) {
        LNN_LOGE(LNN_BUILDER, "create connection fsm failed");
        return NULL;
    }
    if (LnnStartConnectionFsm(connFsm) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "start connection fsmId=%u failed", connFsm->id);
        LnnDestroyConnectionFsm(connFsm);
        return NULL;
    }
    SetBeginJoinLnnTime(connFsm);
    ListAdd(&g_netBuilder.fsmList, &connFsm->node);
    ++g_netBuilder.connCount;
    return connFsm;
}

static bool IsNodeOnline(const char *networkId)
{
    return LnnGetOnlineStateById(networkId, CATEGORY_NETWORK_ID);
}

static void UpdateLocalMasterNode(bool isCurrentNode, const char *masterUdid, int32_t weight)
{
    if (LnnSetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, masterUdid) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "set local master udid failed");
        return;
    }
    if (LnnSetLocalNumInfo(NUM_KEY_MASTER_NODE_WEIGHT, weight) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "set local master weight failed");
    }
    LnnNotifyMasterNodeChanged(isCurrentNode, masterUdid, weight);
    LNN_LOGI(LNN_BUILDER, "update local master weight=%d", weight);
}

static bool IsNeedSyncElectMsg(const char *networkId)
{
    NodeInfo nodeInfo;
    (void)memset_s(&nodeInfo, sizeof(NodeInfo), 0, sizeof(NodeInfo));
    if (LnnGetRemoteNodeInfoById(networkId, CATEGORY_NETWORK_ID, &nodeInfo) != SOFTBUS_OK) {
        return false;
    }
    return LnnHasDiscoveryType(&nodeInfo, DISCOVERY_TYPE_WIFI);
}

static int32_t SyncElectMessage(const char *networkId)
{
    char masterUdid[UDID_BUF_LEN] = {0};
    int32_t masterWeight;
    char *data = NULL;
    cJSON *json = NULL;
    int32_t rc;

    if (!IsNeedSyncElectMsg(networkId)) {
        char *anonyNetworkId = NULL;
        Anonymize(networkId, &anonyNetworkId);
        LNN_LOGW(LNN_BUILDER, "no ip networking, dont sync elect msg, networkId=%s", anonyNetworkId);
        AnonymizeFree(anonyNetworkId);
        return SOFTBUS_OK;
    }
    if (LnnGetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, masterUdid, UDID_BUF_LEN) != SOFTBUS_OK ||
        LnnGetLocalNumInfo(NUM_KEY_MASTER_NODE_WEIGHT, &masterWeight) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local master node info failed");
        return SOFTBUS_INVALID_PARAM;
    }
    json = cJSON_CreateObject();
    if (json == NULL) {
        LNN_LOGE(LNN_BUILDER, "create elect json object failed");
        return SOFTBUS_CREATE_JSON_ERR;
    }
    if (!AddStringToJsonObject(json, JSON_KEY_MASTER_UDID, masterUdid) ||
        !AddNumberToJsonObject(json, JSON_KEY_MASTER_WEIGHT, masterWeight)) {
        LNN_LOGE(LNN_BUILDER, "add elect info to json failed");
        cJSON_Delete(json);
        return SOFTBUS_ERR;
    }
    data = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (data == NULL) {
        LNN_LOGE(LNN_BUILDER, "format elect packet fail");
        return SOFTBUS_ERR;
    }
    rc = LnnSendSyncInfoMsg(LNN_INFO_TYPE_MASTER_ELECT, networkId, (uint8_t *)data, strlen(data) + 1, NULL);
    cJSON_free(data);
    return rc;
}

static void SendElectMessageToAll(const char *skipNetworkId)
{
    LnnConnectionFsm *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (skipNetworkId != NULL && strcmp(item->connInfo.peerNetworkId, skipNetworkId) == 0) {
            continue;
        }
        if (!IsNodeOnline(item->connInfo.peerNetworkId)) {
            continue;
        }
        if (SyncElectMessage(item->connInfo.peerNetworkId) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "sync elect info to connFsm=%u failed", item->id);
        }
    }
}

static bool NeedPendingJoinRequest(void)
{
    int32_t count = 0;
    LnnConnectionFsm *item = NULL;

    if (g_netBuilder.maxConcurrentCount == 0) { // do not limit concurent
        return false;
    }
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (item->isDead) {
            continue;
        }
        if ((item->connInfo.flag & LNN_CONN_INFO_FLAG_ONLINE) != 0) {
            continue;
        }
        ++count;
        if (count >= g_netBuilder.maxConcurrentCount) {
            return true;
        }
    }
    return false;
}

static bool IsSamePendingRequest(const PendingJoinRequestNode *request)
{
    PendingJoinRequestNode *item = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.pendingList, PendingJoinRequestNode, node) {
        if (LnnIsSameConnectionAddr(&item->addr, &request->addr, false) &&
            item->needReportFailure == request->needReportFailure) {
            LNN_LOGD(LNN_BUILDER, "have the same pending join request");
            return true;
        }
    }
    return false;
}

static bool TryPendingJoinRequest(const JoinLnnMsgPara *para, bool needReportFailure)
{
    PendingJoinRequestNode *request = NULL;
    if (para == NULL || !para->isNeedConnect) {
        LNN_LOGI(LNN_BUILDER, "no connect online, no need pending");
        return false;
    }
    if (!NeedPendingJoinRequest()) {
        return false;
    }
    request = (PendingJoinRequestNode *)SoftBusCalloc(sizeof(PendingJoinRequestNode));
    if (request == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc pending join request fail, go on it");
        return false;
    }
    ListInit(&request->node);
    request->addr = para->addr;
    request->needReportFailure = needReportFailure;
    if (IsSamePendingRequest(request)) {
        SoftBusFree(request);
        return true;
    }
    ListTailInsert(&g_netBuilder.pendingList, &request->node);
    return true;
}

static MetaJoinRequestNode *TryJoinRequestMetaNode(const char *pkgName, const ConnectionAddr *addr,
    int32_t callingPid, bool needReportFailure)
{
    MetaJoinRequestNode *request = NULL;

    request = (MetaJoinRequestNode *)SoftBusCalloc(sizeof(MetaJoinRequestNode));
    if (request == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc MetaNode join request fail, go on it");
        return NULL;
    }
    ListInit(&request->node);
    request->addr = *addr;
    request->callingPid = callingPid;
    request->needReportFailure = needReportFailure;
    if (memcpy_s(request->pkgName, PKG_NAME_SIZE_MAX, pkgName, strlen(pkgName)) != EOK) {
        LNN_LOGE(LNN_BUILDER, "memcpy_s error");
        SoftBusFree(request);
        return NULL;
    }
    ListTailInsert(&g_netBuilder.metaNodeList, &request->node);
    return request;
}

static int32_t PostJoinRequestToConnFsm(LnnConnectionFsm *connFsm, const ConnectionAddr *addr,
    const char* pkgName, bool isNeedConnect, bool needReportFailure)
{
    int32_t rc = SOFTBUS_OK;
    bool isCreate = false;

    if (connFsm == NULL) {
        connFsm = FindConnectionFsmByAddr(addr, false);
    }
    if (connFsm == NULL || connFsm->isDead) {
        connFsm = StartNewConnectionFsm(addr, pkgName, isNeedConnect);
        isCreate = true;
    }
    if (connFsm == NULL || LnnSendJoinRequestToConnFsm(connFsm) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "process join lnn request failed");
        if (needReportFailure) {
            LnnNotifyJoinResult((ConnectionAddr *)addr, NULL, SOFTBUS_ERR);
        }
        if (connFsm != NULL && isCreate) {
            ListDelete(&connFsm->node);
            --g_netBuilder.connCount;
            LnnDestroyConnectionFsm(connFsm);
        }
        rc = SOFTBUS_ERR;
    }
    if (rc == SOFTBUS_OK) {
        connFsm->connInfo.flag |=
            (needReportFailure ? LNN_CONN_INFO_FLAG_JOIN_REQUEST : LNN_CONN_INFO_FLAG_JOIN_AUTO);
    }
    return rc;
}

static int32_t PostJoinRequestToMetaNode(MetaJoinRequestNode *metaJoinNode, const ConnectionAddr *addr,
    CustomData *customData, bool needReportFailure)
{
    int32_t rc = SOFTBUS_OK;
    if (OnJoinMetaNode(metaJoinNode, customData) != SOFTBUS_OK) {
        LNN_LOGE(LNN_META_NODE, "Post Request To MetaNode failed");
        rc = SOFTBUS_ERR;
        if (needReportFailure) {
            MetaBasicInfo metaInfo;
            (void)memset_s(&metaInfo, sizeof(MetaBasicInfo), 0, sizeof(MetaBasicInfo));
            MetaNodeNotifyJoinResult((ConnectionAddr *)addr, &metaInfo, SOFTBUS_ERR);
        }
    }
    return rc;
}

static void TryRemovePendingJoinRequest(void)
{
    PendingJoinRequestNode *item = NULL;
    PendingJoinRequestNode *next = NULL;

    LIST_FOR_EACH_ENTRY_SAFE(item, next, &g_netBuilder.pendingList, PendingJoinRequestNode, node) {
        if (NeedPendingJoinRequest()) {
            return;
        }
        ListDelete(&item->node);
        if (PostJoinRequestToConnFsm(NULL, &item->addr, DEFAULT_PKG_NAME, true, item->needReportFailure)
            != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "post pending join request failed");
        }
        LNN_LOGI(LNN_BUILDER, "remove a pending join request, peerAddr=%s", LnnPrintConnectionAddr(&item->addr));
        SoftBusFree(item);
        break;
    }
}

static void RemovePendingRequestByAddrType(const bool *addrType, uint32_t typeLen)
{
    PendingJoinRequestNode *item = NULL;
    PendingJoinRequestNode *next = NULL;

    if (addrType == NULL || typeLen != CONNECTION_ADDR_MAX) {
        LNN_LOGE(LNN_BUILDER, "invalid typeLen=%d", typeLen);
        return;
    }
    LIST_FOR_EACH_ENTRY_SAFE(item, next, &g_netBuilder.pendingList, PendingJoinRequestNode, node) {
        if (!addrType[item->addr.type]) {
            continue;
        }
        ListDelete(&item->node);
        LNN_LOGI(LNN_BUILDER, "clean a pending join request, peerAddr=%s", LnnPrintConnectionAddr(&item->addr));
        SoftBusFree(item);
    }
}

static int32_t TrySendJoinLNNRequest(const JoinLnnMsgPara *para, bool needReportFailure, bool isShort)
{
    LnnConnectionFsm *connFsm = NULL;
    int32_t ret = SOFTBUS_OK;
    if (para == NULL) {
        LNN_LOGW(LNN_BUILDER, "addr is null");
        return SOFTBUS_INVALID_PARAM;
    }
    if (!para->isNeedConnect) {
        isShort = true;
    }
    connFsm = FindConnectionFsmByAddr(&para->addr, isShort);
    if (connFsm == NULL || connFsm->isDead) {
        if (TryPendingJoinRequest(para, needReportFailure)) {
            LNN_LOGI(LNN_BUILDER, "join request is pending, peerAddr=%s", LnnPrintConnectionAddr(&para->addr));
            SoftBusFree((void *)para);
            return SOFTBUS_OK;
        }
        ret = PostJoinRequestToConnFsm(connFsm, &para->addr, para->pkgName, para->isNeedConnect, needReportFailure);
        SoftBusFree((void *)para);
        return ret;
    }
    connFsm->connInfo.flag |=
        (needReportFailure ? LNN_CONN_INFO_FLAG_JOIN_REQUEST : LNN_CONN_INFO_FLAG_JOIN_AUTO);
    if ((connFsm->connInfo.flag & LNN_CONN_INFO_FLAG_ONLINE) != 0) {
        if ((LnnSendJoinRequestToConnFsm(connFsm) != SOFTBUS_OK) && needReportFailure) {
            LNN_LOGE(LNN_BUILDER, "online status, process join lnn request failed");
            LnnNotifyJoinResult((ConnectionAddr *)&para->addr, NULL, SOFTBUS_ERR);
        }
    }
    LNN_LOGI(LNN_BUILDER, "addr same to before, peerAddr=%s", LnnPrintConnectionAddr(&para->addr));
    SoftBusFree((void *)para);
    return SOFTBUS_OK;
}

static int32_t TrySendJoinMetaNodeRequest(const ConnectionAddrKey *addrDataKey, bool needReportFailure)
{
    if (addrDataKey == NULL) {
        LNN_LOGW(LNN_META_NODE, "addrDataKey is NULL");
        return SOFTBUS_INVALID_PARAM;
    }
    const ConnectionAddr *addr = &addrDataKey->addr;
    CustomData customData = addrDataKey->customData;
    MetaJoinRequestNode *metaJoinNode = NULL;
    int32_t rc;
    metaJoinNode = FindMetaNodeByAddr(addr);
    if (metaJoinNode == NULL) {
        LNN_LOGI(LNN_META_NODE, "not find metaJoinNode");
        metaJoinNode = TryJoinRequestMetaNode(addrDataKey->pkgName, addr, addrDataKey->callingPid, needReportFailure);
        if (metaJoinNode == NULL) {
            LNN_LOGI(LNN_META_NODE, "join request is pending");
            SoftBusFree((void *)addrDataKey);
            return SOFTBUS_NETWORK_JOIN_REQUEST_ERR;
        }
    }
    rc = PostJoinRequestToMetaNode(metaJoinNode, addr, &customData, needReportFailure);
    SoftBusFree((void *)addrDataKey);
    return rc;
}

static int32_t ProcessJoinLNNRequest(const void *para)
{
    return TrySendJoinLNNRequest((const JoinLnnMsgPara *)para, true, false);
}

static int32_t ProcessJoinMetaNodeRequest(const void *para)
{
    return TrySendJoinMetaNodeRequest((const ConnectionAddrKey *)para, true);
}

static int32_t ProcessDevDiscoveryRequest(const void *para)
{
    return TrySendJoinLNNRequest((const JoinLnnMsgPara *)para, false, false);
}

static void InitiateNewNetworkOnline(ConnectionAddrType addrType, const char *networkId)
{
    LnnConnectionFsm *item = NULL;
    int32_t rc;

    // find target connfsm, then notify it online
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (strcmp(networkId, item->connInfo.peerNetworkId) != 0) {
            continue;
        }
        if (item->isDead) {
            continue;
        }
        if (addrType != CONNECTION_ADDR_MAX && addrType != item->connInfo.addr.type) {
            continue;
        }
        rc = LnnSendNewNetworkOnlineToConnFsm(item);
        LNN_LOGI(LNN_INIT,
            "initiate new network online to connection fsmId=%u, rc=%d", item->id, rc);
    }
}

static void TryInitiateNewNetworkOnline(const LnnConnectionFsm *connFsm)
{
    LnnConnectionFsm *item = NULL;
    LnnInvalidCleanInfo *cleanInfo = connFsm->connInfo.cleanInfo;

    if ((connFsm->connInfo.flag & LNN_CONN_INFO_FLAG_INITIATE_ONLINE) == 0) {
        LNN_LOGI(LNN_INIT, "fsmId=%u no need initiate new network online", connFsm->id);
        return;
    }
    // let last invalid connfsm notify new network online after it clean
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (strcmp(connFsm->connInfo.peerNetworkId, item->connInfo.peerNetworkId) != 0) {
            continue;
        }
        if ((item->connInfo.flag & LNN_CONN_INFO_FLAG_INITIATE_ONLINE) == 0) {
            continue;
        }
        LNN_LOGI(LNN_INIT, "fsmId=%u wait last connfsm clean, then initiate new network online",
            connFsm->id);
        return;
    }
    InitiateNewNetworkOnline(cleanInfo->addrType, cleanInfo->networkId);
}

static void TryDisconnectAllConnection(const LnnConnectionFsm *connFsm)
{
    LnnConnectionFsm *item = NULL;
    const ConnectionAddr *addr1 = &connFsm->connInfo.addr;
    const ConnectionAddr *addr2 = NULL;
    ConnectOption option;

    // Not really leaving lnn
    if ((connFsm->connInfo.flag & LNN_CONN_INFO_FLAG_ONLINE) == 0) {
        return;
    }
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        addr2 = &item->connInfo.addr;
        if (addr1->type != addr2->type) {
            continue;
        }
        if (addr1->type == CONNECTION_ADDR_BR || addr1->type == CONNECTION_ADDR_BLE) {
            if (strncmp(item->connInfo.addr.info.br.brMac, addr2->info.br.brMac, BT_MAC_LEN) == 0) {
                return;
            }
        } else if (addr1->type == CONNECTION_ADDR_WLAN || addr1->type == CONNECTION_ADDR_ETH) {
            if (strncmp(addr1->info.ip.ip, addr2->info.ip.ip, strlen(addr1->info.ip.ip)) == 0) {
                return;
            }
        }
    }
    LNN_LOGI(LNN_BUILDER, "fsmId=%u disconnect all connection for type=%d",
        connFsm->id, addr1->type);
    if (LnnConvertAddrToOption(addr1, &option)) {
        ConnDisconnectDeviceAllConn(&option);
    }
}

static void TryNotifyAllTypeOffline(const LnnConnectionFsm *connFsm)
{
    LnnConnectionFsm *item = NULL;
    const ConnectionAddr *addr1 = &connFsm->connInfo.addr;
    const ConnectionAddr *addr2 = NULL;

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        addr2 = &item->connInfo.addr;
        if (addr1->type == addr2->type) {
            return;
        }
    }
    LNN_LOGI(LNN_BUILDER, "fsmId=%u notify all connection offline for type=%d",
        connFsm->id, addr1->type);
    (void)LnnNotifyAllTypeOffline(addr1->type);
}

static void CleanConnectionFsm(LnnConnectionFsm *connFsm)
{
    if (connFsm == NULL) {
        LNN_LOGE(LNN_BUILDER, "connection fsm is null");
        return;
    }
    LNN_LOGI(LNN_BUILDER, "connection fsmId=%u is cleaned", connFsm->id);
    LnnDestroyConnectionFsm(connFsm);
}

static void StopConnectionFsm(LnnConnectionFsm *connFsm)
{
    if (LnnStopConnectionFsm(connFsm, CleanConnectionFsm) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "stop connection fsmId=%u failed", connFsm->id);
    }
    ListDelete(&connFsm->node);
    --g_netBuilder.connCount;
}

static int32_t ProcessCleanConnectionFsm(const void *para)
{
    uint16_t connFsmId;
    LnnConnectionFsm *connFsm = NULL;
    int32_t rc = SOFTBUS_ERR;

    if (para == NULL) {
        LNN_LOGW(LNN_BUILDER, "connFsmId is null");
        return SOFTBUS_INVALID_PARAM;
    }
    connFsmId = *(uint16_t *)para;
    do {
        connFsm = FindConnectionFsmByConnFsmId(connFsmId);
        if (connFsm == NULL) {
            LNN_LOGE(LNN_BUILDER, "can not find connection fsm");
            break;
        }
        StopConnectionFsm(connFsm);
        TryInitiateNewNetworkOnline(connFsm);
        TryDisconnectAllConnection(connFsm);
        TryNotifyAllTypeOffline(connFsm);
        TryRemovePendingJoinRequest();
        rc = SOFTBUS_OK;
    } while (false);
    SoftBusFree((void *)para);
    return rc;
}

static int32_t ProcessVerifyResult(const void *para)
{
    int32_t rc;
    LnnConnectionFsm *connFsm = NULL;
    const VerifyResultMsgPara *msgPara = (const VerifyResultMsgPara *)para;

    if (msgPara == NULL) {
        LNN_LOGW(LNN_BUILDER, "para is null");
        return SOFTBUS_INVALID_PARAM;
    }

    if (msgPara->retCode == SOFTBUS_OK && msgPara->nodeInfo == NULL) {
        LNN_LOGE(LNN_BUILDER, "msgPara node Info is null");
        SoftBusFree((void *)msgPara);
        return SOFTBUS_INVALID_PARAM;
    }

    do {
        connFsm = FindConnectionFsmByRequestId(msgPara->requestId);
        if (connFsm == NULL || connFsm->isDead) {
            LNN_LOGE(LNN_BUILDER,
                "can not find connection fsm by requestId=%u", msgPara->requestId);
            rc = SOFTBUS_NETWORK_NOT_FOUND;
            break;
        }
        LNN_LOGI(LNN_BUILDER, "fsmId=%u connection fsm auth done: authId=%" PRId64 ", retCode=%d",
            connFsm->id, msgPara->authId, msgPara->retCode);
        if (msgPara->retCode == SOFTBUS_OK) {
            connFsm->connInfo.authId = msgPara->authId;
            connFsm->connInfo.nodeInfo = msgPara->nodeInfo;
        }
        if (LnnSendAuthResultMsgToConnFsm(connFsm, msgPara->retCode) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "send auth result to connection fsmId=%u failed",
                connFsm->id);
            connFsm->connInfo.nodeInfo = NULL;
            rc = SOFTBUS_ERR;
            break;
        }
        rc = SOFTBUS_OK;
    } while (false);

    if (rc != SOFTBUS_OK && msgPara->nodeInfo != NULL) {
        SoftBusFree((void *)msgPara->nodeInfo);
    }
    SoftBusFree((void *)msgPara);
    return rc;
}

static int32_t CreatePassiveConnectionFsm(const DeviceVerifyPassMsgPara *msgPara)
{
    LnnConnectionFsm *connFsm = NULL;
    connFsm = StartNewConnectionFsm(&msgPara->addr, DEFAULT_PKG_NAME, true);
    if (connFsm == NULL) {
        LNN_LOGE(LNN_BUILDER,
            "start new connection fsm fail=%" PRId64, msgPara->authId);
        return SOFTBUS_ERR;
    }
    connFsm->connInfo.authId = msgPara->authId;
    connFsm->connInfo.nodeInfo = msgPara->nodeInfo;
    connFsm->connInfo.flag |= LNN_CONN_INFO_FLAG_JOIN_PASSIVE;
    LNN_LOGI(LNN_BUILDER,
        "fsmId=%u start a passive connection fsm, authId=%" PRId64, connFsm->id, msgPara->authId);
    if (LnnSendAuthResultMsgToConnFsm(connFsm, SOFTBUS_OK) != SOFTBUS_OK) {
        connFsm->connInfo.nodeInfo = NULL;
        StopConnectionFsm(connFsm);
        LNN_LOGE(LNN_BUILDER,
            "fsmId=%u post auth result to connection fsm fail=%" PRId64, connFsm->id, msgPara->authId);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

static int32_t ProcessDeviceVerifyPass(const void *para)
{
    int32_t rc;
    LnnConnectionFsm *connFsm = NULL;
    const DeviceVerifyPassMsgPara *msgPara = (const DeviceVerifyPassMsgPara *)para;

    if (msgPara == NULL) {
        LNN_LOGW(LNN_BUILDER, "para is null");
        return SOFTBUS_INVALID_PARAM;
    }
    if (msgPara->nodeInfo == NULL) {
        LNN_LOGE(LNN_BUILDER, "msgPara nodeInfo is null");
        SoftBusFree((void *)msgPara);
        return SOFTBUS_INVALID_PARAM;
    }

    do {
        connFsm = FindConnectionFsmByAuthId(msgPara->authId);
        if (connFsm == NULL || connFsm->isDead) {
            rc = CreatePassiveConnectionFsm(msgPara);
            break;
        }
        if (strcmp(connFsm->connInfo.peerNetworkId, msgPara->nodeInfo->networkId) != 0) {
            LNN_LOGI(LNN_BUILDER,
                "fsmId=%u networkId changed=%" PRId64, connFsm->id, msgPara->authId);
            rc = CreatePassiveConnectionFsm(msgPara);
            break;
        }
        msgPara->nodeInfo->discoveryType = 1 << (uint32_t)LnnConvAddrTypeToDiscType(msgPara->addr.type);
        if (LnnUpdateNodeInfo(msgPara->nodeInfo) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "LnnUpdateNodeInfo failed");
        }
        LNN_LOGI(LNN_BUILDER,
            "fsmId=%u connection fsm exist, ignore VerifyPass event=%" PRId64, connFsm->id, msgPara->authId);
        rc = SOFTBUS_ERR;
    } while (false);

    if (rc != SOFTBUS_OK && msgPara->nodeInfo != NULL) {
        SoftBusFree((void *)msgPara->nodeInfo);
    }
    SoftBusFree((void *)msgPara);
    return rc;
}

static int32_t ProcessDeviceDisconnect(const void *para)
{
    int32_t rc;
    LnnConnectionFsm *connFsm = NULL;
    const int64_t *authId = (const int64_t *)para;

    if (authId == NULL) {
        LNN_LOGW(LNN_BUILDER, "authId is null");
        return SOFTBUS_INVALID_PARAM;
    }

    do {
        connFsm = FindConnectionFsmByAuthId(*authId);
        if (connFsm == NULL || connFsm->isDead) {
            LNN_LOGE(LNN_BUILDER,
                "can not find connection fsm by authId=%" PRId64, *authId);
            rc = SOFTBUS_NETWORK_NOT_FOUND;
            break;
        }
        LNN_LOGI(LNN_BUILDER,
            "fsmId=%u device disconnect, authId=%" PRId64, connFsm->id, *authId);
        if (LnnSendDisconnectMsgToConnFsm(connFsm) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER,
                "send disconnect to connection fsmId=%u failed", connFsm->id);
            rc = SOFTBUS_ERR;
            break;
        }
        rc = SOFTBUS_OK;
    } while (false);
    SoftBusFree((void *)authId);
    return rc;
}

static int32_t ProcessDeviceNotTrusted(const void *para)
{
    int32_t rc;
    const char *udid = NULL;
    LnnConnectionFsm *item = NULL;
    const char *peerUdid = (const char *)para;

    if (peerUdid == NULL) {
        LNN_LOGW(LNN_BUILDER, "peer udid is null");
        return SOFTBUS_INVALID_PARAM;
    }

    do {
        char networkId[NETWORK_ID_BUF_LEN] = {0};
        if (LnnGetNetworkIdByUdid(peerUdid, networkId, sizeof(networkId)) == SOFTBUS_OK) {
            LnnRequestLeaveSpecific(networkId, CONNECTION_ADDR_MAX);
            break;
        }
        LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
            udid = LnnGetDeviceUdid(item->connInfo.nodeInfo);
            if (udid == NULL || strcmp(peerUdid, udid) != 0) {
                continue;
            }
            rc = LnnSendNotTrustedToConnFsm(item);
            LNN_LOGI(LNN_BUILDER,
                "fsmId=%u send not trusted msg to connection fsm result=%d", item->id, rc);
        }
    } while (false);
    SoftBusFree((void *)peerUdid);
    return SOFTBUS_OK;
}

static int32_t ProcessLeaveLNNRequest(const void *para)
{
    const char *networkId = (const char *)para;
    LnnConnectionFsm *item = NULL;
    int rc = SOFTBUS_ERR;

    if (networkId == NULL) {
        LNN_LOGW(LNN_BUILDER, "leave networkId is null");
        return SOFTBUS_INVALID_PARAM;
    }

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (strcmp(networkId, item->connInfo.peerNetworkId) != 0 || item->isDead) {
            continue;
        }
        if (LnnSendLeaveRequestToConnFsm(item) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "send leave LNN msg to connection fsmId=%u failed",
                item->id);
        } else {
            rc = SOFTBUS_OK;
            item->connInfo.flag |= LNN_CONN_INFO_FLAG_LEAVE_REQUEST;
            LNN_LOGI(LNN_BUILDER, "send leave LNN msg to connection fsmId=%u success",
                item->id);
        }
    }
    if (rc != SOFTBUS_OK) {
        LnnNotifyLeaveResult(networkId, SOFTBUS_ERR);
    }
    SoftBusFree((void *)networkId);
    return rc;
}

static void LeaveMetaInfoToLedger(const MetaJoinRequestNode *metaInfo, const char *networkId)
{
    NodeInfo info;
    (void)memset_s(&info, sizeof(NodeInfo), 0, sizeof(NodeInfo));
    const char *udid = NULL;
    if (LnnGetRemoteNodeInfoById(networkId, CATEGORY_NETWORK_ID, &info) != SOFTBUS_OK) {
        return;
    }
    udid = LnnGetDeviceUdid(&info);
    if (LnnDeleteMetaInfo(udid, metaInfo->addr.type) != SOFTBUS_OK) {
        LNN_LOGE(LNN_META_NODE, "LnnDeleteMetaInfo error");
    }
}

static int32_t ProcessLeaveMetaNodeRequest(const void *para)
{
    const char *networkId = (const char *)para;
    MetaJoinRequestNode *item = NULL;
    MetaJoinRequestNode *next = NULL;
    int rc = SOFTBUS_ERR;
    if (networkId == NULL) {
        LNN_LOGW(LNN_BUILDER, "leave networkId is null");
        return SOFTBUS_INVALID_PARAM;
    }
    LIST_FOR_EACH_ENTRY_SAFE(item, next, &g_netBuilder.metaNodeList, MetaJoinRequestNode, node) {
        if (strcmp(networkId, item->networkId) != 0) {
            continue;
        }
        LNN_LOGD(LNN_META_NODE, "find networkId");
        AuthMetaReleaseVerify(item->authId);
        LeaveMetaInfoToLedger(item, networkId);
        ListDelete(&item->node);
        SoftBusFree(item);
        rc = SOFTBUS_OK;
    }
    MetaNodeNotifyLeaveResult(networkId, rc);
    SoftBusFree((void *)networkId);
    return rc;
}

static int32_t ProcessSyncOfflineFinish(const void *para)
{
    const char *networkId = (const char *)para;
    LnnConnectionFsm *item = NULL;
    int rc = SOFTBUS_OK;

    if (networkId == NULL) {
        LNN_LOGW(LNN_BUILDER, "sync offline finish networkId is null");
        return SOFTBUS_INVALID_PARAM;
    }
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (strcmp(networkId, item->connInfo.peerNetworkId) != 0 || item->isDead) {
            continue;
        }
        rc = LnnSendSyncOfflineFinishToConnFsm(item);
        LNN_LOGI(LNN_BUILDER, "send sync offline msg to connection fsmId=%u result=%d",
            item->id, rc);
    }
    SoftBusFree((void *)networkId);
    return rc;
}

static bool IsInvalidConnectionFsm(const LnnConnectionFsm *connFsm, const LeaveInvalidConnMsgPara *msgPara)
{
    if (strcmp(msgPara->oldNetworkId, connFsm->connInfo.peerNetworkId) != 0) {
        return false;
    }
    if (connFsm->isDead) {
        LNN_LOGI(LNN_BUILDER, "fsmId=%u connection is dead", connFsm->id);
        return false;
    }
    if (msgPara->addrType != CONNECTION_ADDR_MAX && msgPara->addrType != connFsm->connInfo.addr.type) {
        LNN_LOGI(LNN_BUILDER, "fsmId=%u connection type not match %d,%d",
            connFsm->id, msgPara->addrType, connFsm->connInfo.addr.type);
        return false;
    }
    if ((connFsm->connInfo.flag & LNN_CONN_INFO_FLAG_ONLINE) == 0) {
        LNN_LOGI(LNN_BUILDER, "fsmId=%u connection is not online", connFsm->id);
        return false;
    }
    if ((connFsm->connInfo.flag & LNN_CONN_INFO_FLAG_INITIATE_ONLINE) != 0) {
        LNN_LOGI(LNN_BUILDER, "fsmId=%u connection is already in leaving", connFsm->id);
        return false;
    }
    return true;
}

static int32_t ProcessLeaveInvalidConn(const void *para)
{
    LnnConnectionFsm *item = NULL;
    int32_t rc = SOFTBUS_OK;
    int32_t count = 0;
    const LeaveInvalidConnMsgPara *msgPara = (const LeaveInvalidConnMsgPara *)para;

    if (msgPara == NULL) {
        LNN_LOGW(LNN_BUILDER, "leave invalid connection msg para is null");
        return SOFTBUS_INVALID_PARAM;
    }
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (!IsInvalidConnectionFsm(item, msgPara)) {
            continue;
        }
        // The new connFsm should timeout when following errors occur
        ++count;
        item->connInfo.cleanInfo = (LnnInvalidCleanInfo *)SoftBusMalloc(sizeof(LnnInvalidCleanInfo));
        if (item->connInfo.cleanInfo == NULL) {
            LNN_LOGI(LNN_BUILDER, "fsmId=%u malloc invalid clean info failed", item->id);
            continue;
        }
        item->connInfo.cleanInfo->addrType = msgPara->addrType;
        if (strncpy_s(item->connInfo.cleanInfo->networkId, NETWORK_ID_BUF_LEN,
            msgPara->newNetworkId, strlen(msgPara->newNetworkId)) != EOK) {
            LNN_LOGE(LNN_BUILDER, "fsmId=%u copy new networkId failed", item->id);
            rc = SOFTBUS_ERR;
            SoftBusFree(item->connInfo.cleanInfo);
            item->connInfo.cleanInfo = NULL;
            continue;
        }
        rc = LnnSendLeaveRequestToConnFsm(item);
        if (rc == SOFTBUS_OK) {
            item->connInfo.flag |= LNN_CONN_INFO_FLAG_INITIATE_ONLINE;
            item->connInfo.flag |= LNN_CONN_INFO_FLAG_LEAVE_AUTO;
        } else {
            SoftBusFree(item->connInfo.cleanInfo);
            item->connInfo.cleanInfo = NULL;
        }
        LNN_LOGI(LNN_BUILDER,
            "send leave LNN msg to invalid connection fsmId=%u result=%d", item->id, rc);
    }
    if (count == 0) {
        InitiateNewNetworkOnline(msgPara->addrType, msgPara->newNetworkId);
    }
    SoftBusFree((void *)msgPara);
    return rc;
}

static int32_t TryElectMasterNodeOnline(const LnnConnectionFsm *connFsm)
{
    char peerMasterUdid[UDID_BUF_LEN] = {0};
    char localMasterUdid[UDID_BUF_LEN] = {0};
    int32_t peerMasterWeight, localMasterWeight;
    int32_t rc;

    // get local master node info
    if (LnnGetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, localMasterUdid, UDID_BUF_LEN) != SOFTBUS_OK ||
        LnnGetLocalNumInfo(NUM_KEY_MASTER_NODE_WEIGHT, &localMasterWeight) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local master node info from ledger failed");
        return SOFTBUS_NETWORK_GET_NODE_INFO_ERR;
    }
    LNN_LOGI(LNN_BUILDER, "local master fsmId=%u weight=%d", connFsm->id, localMasterWeight);
    if (LnnGetRemoteStrInfo(connFsm->connInfo.peerNetworkId, STRING_KEY_MASTER_NODE_UDID,
        peerMasterUdid, UDID_BUF_LEN) != SOFTBUS_OK ||
        LnnGetRemoteNumInfo(connFsm->connInfo.peerNetworkId, NUM_KEY_MASTER_NODE_WEIGHT,
            &peerMasterWeight) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "peer node info fsmId=%u is not found", connFsm->id);
        return SOFTBUS_NETWORK_NOT_FOUND;
    }
    LNN_LOGI(LNN_BUILDER, "peer master fsmId=%u weight=%d", connFsm->id, peerMasterWeight);
    rc = LnnCompareNodeWeight(localMasterWeight, localMasterUdid, peerMasterWeight, peerMasterUdid);
    if (rc >= 0) {
        LNN_LOGI(LNN_BUILDER,
            "online node fsmId=%u weight less than current(compare result=%d), no need elect again",
            connFsm->id, rc);
        return SOFTBUS_OK;
    }
    UpdateLocalMasterNode(false, peerMasterUdid, peerMasterWeight);
    SendElectMessageToAll(connFsm->connInfo.peerNetworkId);
    return SOFTBUS_OK;
}

static int32_t TryElectMasterNodeOffline(const LnnConnectionFsm *connFsm)
{
    char localUdid[UDID_BUF_LEN] = {0};
    char localMasterUdid[UDID_BUF_LEN] = {0};

    if (LnnGetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, localMasterUdid, UDID_BUF_LEN) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local master node info from ledger failed");
        return SOFTBUS_NETWORK_GET_NODE_INFO_ERR;
    }
    LnnGetLocalStrInfo(STRING_KEY_DEV_UDID, localUdid, UDID_BUF_LEN);
    if (strcmp(localMasterUdid, localUdid) == 0) {
        LNN_LOGI(LNN_BUILDER, "local is master node fsmId=%u, no need elect again", connFsm->id);
    } else {
        LNN_LOGI(LNN_BUILDER, "maybe master node fsmId=%u offline, elect again", connFsm->id);
        UpdateLocalMasterNode(true, localUdid, LnnGetLocalWeight());
        SendElectMessageToAll(connFsm->connInfo.peerNetworkId);
    }
    return SOFTBUS_OK;
}

static bool IsSupportMasterNodeElect(SoftBusVersion version)
{
    LNN_LOGD(LNN_BUILDER, "SoftBusVersion=%d", version);
    return version >= SOFTBUS_NEW_V1;
}

static void TryElectAsMasterState(const char *networkId, bool isOnline)
{
    if (networkId == NULL) {
        LNN_LOGW(LNN_BUILDER, "invalid networkId");
        return;
    }
    if (isOnline) {
        LNN_LOGD(LNN_BUILDER, "restore master state ignore online process");
        return;
    }
    char masterUdid[UDID_BUF_LEN] = {0};
    char localUdid[UDID_BUF_LEN] = {0};
    if (LnnGetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, masterUdid, UDID_BUF_LEN) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local master node info from ledger failed");
        return;
    }
    const char *peerUdid = LnnConvertDLidToUdid(networkId, CATEGORY_NETWORK_ID);
    if (peerUdid == NULL) {
        char *anonyNetworkId = NULL;
        Anonymize(networkId, &anonyNetworkId);
        LNN_LOGE(LNN_BUILDER, "get invalid peerUdid, networkId=%s", anonyNetworkId);
        AnonymizeFree(anonyNetworkId);
        return;
    }
    if (strcmp(masterUdid, peerUdid) != 0) {
        char *anonyPeerUdid = NULL;
        char *anonyMasterUdid = NULL;
        Anonymize(peerUdid, &anonyPeerUdid);
        Anonymize(masterUdid, &anonyMasterUdid);
        LNN_LOGD(LNN_BUILDER, "offline node(peerUdid=%s) is not master node(masterUdid=%s)",
            anonyPeerUdid, anonyMasterUdid);
        AnonymizeFree(anonyPeerUdid);
        AnonymizeFree(anonyMasterUdid);
        return;
    }
    if (LnnGetLocalStrInfo(STRING_KEY_DEV_UDID, localUdid, UDID_BUF_LEN) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local udid failed");
        return;
    }
    UpdateLocalMasterNode(true, localUdid, LnnGetLocalWeight());
}

static int32_t ProcessNodeStateChanged(const void *para)
{
    const ConnectionAddr *addr = (const ConnectionAddr *)para;
    LnnConnectionFsm *connFsm = NULL;
    int32_t rc = SOFTBUS_ERR;
    bool isOnline = false;

    if (addr == NULL) {
        LNN_LOGW(LNN_BUILDER, "node state changed msg is null");
        return SOFTBUS_INVALID_PARAM;
    }
    do {
        connFsm = FindConnectionFsmByAddr(addr, false);
        if (connFsm == NULL) {
            LNN_LOGE(LNN_BUILDER,
                "can't find connection fsm when node online state changed");
            break;
        }
        isOnline = IsNodeOnline(connFsm->connInfo.peerNetworkId);
        TryElectAsMasterState(connFsm->connInfo.peerNetworkId, isOnline);
        if (!IsSupportMasterNodeElect(connFsm->connInfo.version)) {
            LNN_LOGI(LNN_BUILDER, "fsmId=%u peer not support master node elect", connFsm->id);
            rc = SOFTBUS_OK;
            break;
        }
        rc = isOnline ? TryElectMasterNodeOnline(connFsm) : TryElectMasterNodeOffline(connFsm);
    } while (false);
    SoftBusFree((void *)addr);
    if (isOnline) {
        TryRemovePendingJoinRequest();
    }
    return rc;
}

static int32_t ProcessMasterElect(const void *para)
{
    const ElectMsgPara *msgPara = (const ElectMsgPara *)para;
    LnnConnectionFsm *connFsm = NULL;
    char localMasterUdid[UDID_BUF_LEN] = {0};
    int32_t localMasterWeight, compareRet;
    int32_t rc = SOFTBUS_ERR;

    if (msgPara == NULL) {
        LNN_LOGW(LNN_BUILDER, "elect msg para is null");
        return SOFTBUS_INVALID_PARAM;
    }
    do {
        connFsm = FindConnectionFsmByNetworkId(msgPara->networkId);
        if (connFsm == NULL || connFsm->isDead) {
            LNN_LOGE(LNN_BUILDER, "can't find connection fsm when receive elect node");
            break;
        }
        if (!IsNodeOnline(connFsm->connInfo.peerNetworkId)) {
            LNN_LOGE(LNN_BUILDER, "peer node fsmId=%u is already offline", connFsm->id);
            break;
        }
        if (LnnGetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, localMasterUdid, UDID_BUF_LEN) != SOFTBUS_OK ||
            LnnGetLocalNumInfo(NUM_KEY_MASTER_NODE_WEIGHT, &localMasterWeight) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "get local master node fsmId=%u info from ledger failed",
                connFsm->id);
            break;
        }
        compareRet = LnnCompareNodeWeight(localMasterWeight, localMasterUdid,
            msgPara->masterWeight, msgPara->masterUdid);
        LNN_LOGI(LNN_BUILDER, "fsmId=%u weight compare result=%d", connFsm->id, compareRet);
        if (compareRet != 0) {
            if (compareRet < 0) {
                UpdateLocalMasterNode(false, msgPara->masterUdid, msgPara->masterWeight);
                SendElectMessageToAll(connFsm->connInfo.peerNetworkId);
            } else {
                rc = SyncElectMessage(connFsm->connInfo.peerNetworkId);
                LNN_LOGI(LNN_BUILDER, "sync elect info to connFsmId=%u result=%d",
                    connFsm->id, rc);
            }
        }
        rc = SOFTBUS_OK;
    } while (false);
    SoftBusFree((void *)msgPara);
    return rc;
}

static int32_t ProcessLeaveByAddrType(const void *para)
{
    bool *addrType = NULL;
    LnnConnectionFsm *item = NULL;
    int32_t rc;
    bool notify = true;

    if (para == NULL) {
        LNN_LOGW(LNN_BUILDER, "leave by addr type msg para is null");
        return SOFTBUS_INVALID_PARAM;
    }

    addrType = (bool *)para;
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (!addrType[item->connInfo.addr.type]) {
            continue;
        }
        // if there are any same addr type, let last one send notify
        notify = false;
        if (item->isDead) {
            continue;
        }
        rc = LnnSendLeaveRequestToConnFsm(item);
        LNN_LOGI(LNN_BUILDER, "leave conn fsmId=%u by addr type=%d rc=%d", item->id, item->connInfo.addr.type, rc);
        if (rc == SOFTBUS_OK) {
            item->connInfo.flag |= LNN_CONN_INFO_FLAG_LEAVE_AUTO;
        }
    }
    LNN_LOGD(LNN_BUILDER, "notify=%d, [eth=%d, wifi=%d]",
        notify, addrType[CONNECTION_ADDR_ETH], addrType[CONNECTION_ADDR_WLAN]);
    if (notify && (addrType[CONNECTION_ADDR_ETH] || addrType[CONNECTION_ADDR_WLAN])) {
        (void)LnnNotifyAllTypeOffline(CONNECTION_ADDR_MAX);
    }
    RemovePendingRequestByAddrType(addrType, CONNECTION_ADDR_MAX);
    SoftBusFree((void *)para);
    return SOFTBUS_OK;
}

static int32_t ProcessLeaveSpecific(const void *para)
{
    const SpecificLeaveMsgPara *msgPara = (const SpecificLeaveMsgPara *)para;
    LnnConnectionFsm *item = NULL;

    if (msgPara == NULL) {
        LNN_LOGW(LNN_BUILDER, "leave specific msg is null");
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t rc;
    bool deviceLeave = false;
    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (strcmp(item->connInfo.peerNetworkId, msgPara->networkId) != 0 ||
            (item->connInfo.addr.type != msgPara->addrType &&
            msgPara->addrType != CONNECTION_ADDR_MAX)) {
            continue;
        }
        deviceLeave = true;
        rc = LnnSendLeaveRequestToConnFsm(item);
        if (rc == SOFTBUS_OK) {
            item->connInfo.flag |= LNN_CONN_INFO_FLAG_LEAVE_AUTO;
        }
        LNN_LOGI(LNN_BUILDER,
            "send leave LNN msg to connection fsmId=%u result=%d", item->id, rc);
    }

    if (deviceLeave) {
        SoftBusFree((void*)msgPara);
        return SOFTBUS_OK;
    }

    do {
        NodeInfo nodeInfo;
        (void)memset_s(&nodeInfo, sizeof(NodeInfo), 0, sizeof(NodeInfo));
        if (LnnGetRemoteNodeInfoById(msgPara->networkId, CATEGORY_NETWORK_ID, &nodeInfo)) {
            break;
        }

        if (nodeInfo.deviceInfo.deviceTypeId != TYPE_PC_ID ||
            strcmp(nodeInfo.networkId, nodeInfo.deviceInfo.deviceUdid) != 0) {
            break;
        }

        (void)LnnClearDiscoveryType(&nodeInfo, LnnConvAddrTypeToDiscType(msgPara->addrType));
        if (nodeInfo.discoveryType != 0) {
            LNN_LOGI(LNN_BUILDER, "pc without softbus has another discovery type");
            break;
        }

        LNN_LOGI(LNN_BUILDER, "pc without softbus offline");
        DeleteFromProfile(nodeInfo.deviceInfo.deviceUdid);
        LnnRemoveNode(nodeInfo.deviceInfo.deviceUdid);
    } while (false);
    SoftBusFree((void *)msgPara);
    return SOFTBUS_OK;
}

static NodeInfo *DupNodeInfo(const NodeInfo *nodeInfo)
{
    NodeInfo *node = (NodeInfo *)SoftBusMalloc(sizeof(NodeInfo));
    if (node == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc NodeInfo fail");
        return NULL;
    }
    if (memcpy_s(node, sizeof(NodeInfo), nodeInfo, sizeof(NodeInfo)) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy NodeInfo fail");
        SoftBusFree(node);
        return NULL;
    }
    return node;
}

static int32_t FillNodeInfo(MetaJoinRequestNode *metaNode, NodeInfo *info)
{
    if (metaNode == NULL || info == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    bool isAuthServer = false;
    info->discoveryType = 1 << (uint32_t)LnnConvAddrTypeToDiscType(metaNode->addr.type);
    info->authSeqNum = metaNode->authId;
    (void)AuthGetServerSide(metaNode->authId, &isAuthServer);
    info->authChannelId[metaNode->addr.type][isAuthServer ? AUTH_AS_SERVER_SIDE : AUTH_AS_CLIENT_SIDE] =
        (int32_t)metaNode->authId;
    info->relation[metaNode->addr.type]++;
    if (AuthGetDeviceUuid(metaNode->authId, info->uuid, sizeof(info->uuid)) != SOFTBUS_OK ||
        info->uuid[0] == '\0') {
        LNN_LOGE(LNN_BUILDER, "fill uuid fail");
        return SOFTBUS_ERR;
    }
    if (metaNode->addr.type == CONNECTION_ADDR_ETH || metaNode->addr.type == CONNECTION_ADDR_WLAN) {
        if (strcpy_s(info->connectInfo.deviceIp, MAX_ADDR_LEN, metaNode->addr.info.ip.ip) != EOK) {
            LNN_LOGE(LNN_BUILDER, "fill deviceIp fail");
            return SOFTBUS_MEM_ERR;
        }
    }
    info->metaInfo.metaDiscType = 1 << (uint32_t)LnnConvAddrTypeToDiscType(metaNode->addr.type);
    info->metaInfo.isMetaNode = true;
    return SOFTBUS_OK;
}

static int32_t ProcessOnAuthMetaVerifyPassed(const void *para)
{
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "para is NULL");
        return SOFTBUS_INVALID_PARAM;
    }
    MetaAuthInfo *meta = (MetaAuthInfo *)para;
    MetaJoinRequestNode *metaNode = meta->metaJoinNode;
    int64_t authMetaId = meta->authMetaId;
    NodeInfo *info = &meta->info;
    int32_t ret = SOFTBUS_ERR;
    do {
        if (strcpy_s(metaNode->networkId, sizeof(metaNode->networkId), info->networkId) != EOK) {
            LNN_LOGE(LNN_BUILDER, "ProcessOnAuthMetaVerifyPassed copy networkId error");
            break;
        }
        metaNode->authId = authMetaId;
        NodeInfo *newInfo = DupNodeInfo(info);
        if (newInfo == NULL) {
            break;
        }
        if (FillNodeInfo(metaNode, newInfo) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "ProcessOnAuthMetaVerifyPassed FillNodeInfo error");
            SoftBusFree(newInfo);
            break;
        }
        ret = LnnAddMetaInfo(newInfo);
        SoftBusFree(newInfo);
    } while (0);
    MetaBasicInfo metaInfo;
    (void)memset_s(&metaInfo, sizeof(MetaBasicInfo), 0, sizeof(MetaBasicInfo));
    if (ret == SOFTBUS_OK) {
        if (strncpy_s(metaInfo.metaNodeId, NETWORK_ID_BUF_LEN, info->networkId, NETWORK_ID_BUF_LEN) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "copy meta node id fail");
            return SOFTBUS_ERR;
        }
        MetaNodeNotifyJoinResult(&metaNode->addr, &metaInfo, SOFTBUS_OK);
    } else {
        LNN_LOGE(LNN_BUILDER, "ProcessOnAuthMetaVerifyPassed error");
        MetaNodeNotifyJoinResult(&metaNode->addr, &metaInfo, SOFTBUS_ERR);
        ListDelete(&metaNode->node);
        SoftBusFree(metaNode);
    }
    SoftBusFree(meta);
    return ret;
}

static int32_t ProcessOnAuthMetaVerifyFailed(const void *para)
{
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "para is NULL");
        return SOFTBUS_INVALID_PARAM;
    }
    MetaReason *mataReason = (MetaReason *)para;
    MetaJoinRequestNode *metaNode = mataReason->metaJoinNode;
    MetaBasicInfo metaInfo;
    (void)memset_s(&metaInfo, sizeof(MetaBasicInfo), 0, sizeof(MetaBasicInfo));
    MetaNodeNotifyJoinResult(&metaNode->addr, &metaInfo, mataReason->reason);
    ListDelete(&metaNode->node);
    SoftBusFree(metaNode);
    SoftBusFree(mataReason);
    return SOFTBUS_OK;
}

static NetBuilderMessageProcess g_messageProcessor[MSG_TYPE_MAX] = {
    ProcessJoinLNNRequest,
    ProcessDevDiscoveryRequest,
    ProcessCleanConnectionFsm,
    ProcessVerifyResult,
    ProcessDeviceVerifyPass,
    ProcessDeviceDisconnect,
    ProcessDeviceNotTrusted,
    ProcessLeaveLNNRequest,
    ProcessSyncOfflineFinish,
    ProcessNodeStateChanged,
    ProcessMasterElect,
    ProcessLeaveInvalidConn,
    ProcessLeaveByAddrType,
    ProcessLeaveSpecific,
    ProcessJoinMetaNodeRequest,
    ProcessOnAuthMetaVerifyPassed,
    ProcessLeaveMetaNodeRequest,
    ProcessOnAuthMetaVerifyFailed,
};

static void NetBuilderMessageHandler(SoftBusMessage *msg)
{
    int32_t ret;

    if (msg == NULL) {
        LNN_LOGE(LNN_BUILDER, "msg is null in net builder handler");
        return;
    }
    LNN_LOGI(LNN_BUILDER, "net builder process msg=%d", msg->what);
    if (msg->what >= MSG_TYPE_MAX) {
        LNN_LOGE(LNN_BUILDER, "invalid msg type");
        return;
    }
    ret = g_messageProcessor[msg->what](msg->obj);
    LNN_LOGD(LNN_BUILDER, "net builder process msg=%d done, ret=%d", msg->what, ret);
}

static ConnectionAddrType GetCurrentConnectType(void)
{
    char ifCurrentName[NET_IF_NAME_LEN] = {0};
    ConnectionAddrType type = CONNECTION_ADDR_MAX;

    if (LnnGetLocalStrInfo(STRING_KEY_NET_IF_NAME, ifCurrentName, NET_IF_NAME_LEN) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "LnnGetLocalStrInfo getCurrentConnectType failed");
        return type;
    }
    if (LnnGetAddrTypeByIfName(ifCurrentName, &type) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "getCurrentConnectType unknown connect type");
    }
    return type;
}

static void OnDeviceVerifyPass(int64_t authId, const NodeInfo *info)
{
    AuthConnInfo connInfo;
    DeviceVerifyPassMsgPara *para = NULL;
    LNN_LOGI(LNN_BUILDER, "verify passed passively, authId=%" PRId64, authId);
    if (AuthGetConnInfo(authId, &connInfo) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get AuthConnInfo fail, authId=%" PRId64, authId);
        return;
    }
    para = (DeviceVerifyPassMsgPara *)SoftBusMalloc(sizeof(DeviceVerifyPassMsgPara));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc DeviceVerifyPassMsgPara fail");
        return;
    }
    if (!LnnConvertAuthConnInfoToAddr(&para->addr, &connInfo, GetCurrentConnectType())) {
        LNN_LOGE(LNN_BUILDER, "convert connInfo to addr fail");
        SoftBusFree(para);
        return;
    }
    para->authId = authId;
    para->nodeInfo = DupNodeInfo(info);
    if (para->nodeInfo == NULL) {
        LNN_LOGE(LNN_BUILDER, "dup NodeInfo fail");
        SoftBusFree(para);
        return;
    }
    if (PostMessageToHandler(MSG_TYPE_DEVICE_VERIFY_PASS, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post DEVICE_VERIFY_PASS msg fail");
        SoftBusFree(para->nodeInfo);
        SoftBusFree(para);
    }
    if (info != NULL) {
        LnnNotifyDeviceVerified(info->deviceInfo.deviceUdid);
    }
}

static void OnDeviceDisconnect(int64_t authId)
{
    int64_t *para = NULL;
    para = (int64_t *)SoftBusMalloc(sizeof(int64_t));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc DeviceDisconnect para fail");
        return;
    }
    LNN_LOGI(LNN_BUILDER, "auth device disconnect, authId=%" PRId64, authId);
    *para = authId;
    if (PostMessageToHandler(MSG_TYPE_DEVICE_DISCONNECT, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post DEVICE_DISCONNECT msg fail");
        SoftBusFree(para);
    }
}

static void OnLnnProcessNotTrustedMsgDelay(void *para)
{
    if (para == NULL) {
        LNN_LOGW(LNN_BUILDER, "invalid para");
        return;
    }
    int64_t authSeq[DISCOVERY_TYPE_COUNT] = {0};
    NotTrustedDelayInfo *info = (NotTrustedDelayInfo *)para;
    if (!LnnGetOnlineStateById(info->udid, CATEGORY_UDID)) {
        LNN_LOGI(LNN_BUILDER, "device is offline");
        SoftBusFree(info);
        return;
    }
    if (AuthGetLatestAuthSeqList(info->udid, authSeq, DISCOVERY_TYPE_COUNT) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get latest authSeq list fail");
        SoftBusFree(info);
        return;
    }
    char networkId[NETWORK_ID_BUF_LEN] = {0};
    if (LnnConvertDlId(info->udid, CATEGORY_UDID, CATEGORY_NETWORK_ID, networkId, NETWORK_ID_BUF_LEN) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "LnnConvertDlId fail");
        SoftBusFree(info);
        return;
    }
    uint32_t type;
    for (type = DISCOVERY_TYPE_WIFI; type < DISCOVERY_TYPE_P2P; type++) {
        LNN_LOGI(LNN_BUILDER, "after 5s, authSeq=%" PRId64 "-> %" PRId64, info->authSeq[type], authSeq[type]);
        if (authSeq[type] == info->authSeq[type] && authSeq[type] != 0 && info->authSeq[type] != 0) {
            char *anonyNetworkId = NULL;
            Anonymize(networkId, &anonyNetworkId);
            LNN_LOGI(LNN_BUILDER, "networkId=%s LnnRequestLeaveSpecific type=%d", anonyNetworkId, type);
            AnonymizeFree(anonyNetworkId);
            LnnRequestLeaveSpecific(networkId, LnnDiscTypeToConnAddrType((DiscoveryType)type));
            continue;
        }
    }
    SoftBusFree(info);
}

static void LnnProcessCompleteNotTrustedMsg(LnnSyncInfoType syncType, const char *networkId,
    const uint8_t *msg, uint32_t len)
{
    if (networkId == NULL || syncType != LNN_INFO_TYPE_NOT_TRUSTED || msg == NULL) {
        LNN_LOGW(LNN_BUILDER, "invalid param");
        return;
    }
    if (!LnnGetOnlineStateById(networkId, CATEGORY_NETWORK_ID)) {
        LNN_LOGI(LNN_BUILDER, "device is offline");
        return;
    }
    JsonObj *json = JSON_Parse((const char *)msg, len);
    if (json == NULL) {
        LNN_LOGE(LNN_BUILDER, "json parse fail");
        return;
    }
    int64_t authSeq[DISCOVERY_TYPE_COUNT] = {0};
    (void)JSON_GetInt64FromOject(json, NETWORK_TYPE_WIFI, &authSeq[DISCOVERY_TYPE_WIFI]);
    (void)JSON_GetInt64FromOject(json, NETWORK_TYPE_BLE, &authSeq[DISCOVERY_TYPE_BLE]);
    (void)JSON_GetInt64FromOject(json, NETWORK_TYPE_BR, &authSeq[DISCOVERY_TYPE_BR]);
    JSON_Delete(json);
    int64_t curAuthSeq[DISCOVERY_TYPE_COUNT] = {0};
    char udid[UDID_BUF_LEN] = {0};
    (void)LnnConvertDlId(networkId, CATEGORY_NETWORK_ID, CATEGORY_UDID, udid, UDID_BUF_LEN);
    if (AuthGetLatestAuthSeqList(udid, curAuthSeq, DISCOVERY_TYPE_COUNT) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get latest authSeq fail");
        return;
    }
    uint32_t type;
    for (type = DISCOVERY_TYPE_WIFI; type < DISCOVERY_TYPE_P2P; type++) {
        LNN_LOGI(LNN_BUILDER, "authSeq %" PRId64 "-> %" PRId64,  curAuthSeq[type], authSeq[type]);
        if (authSeq[type] == curAuthSeq[type] && authSeq[type] != 0 && curAuthSeq[type] != 0) {
            char *anonyNetworkId = NULL;
            Anonymize(networkId, &anonyNetworkId);
            LNN_LOGI(LNN_BUILDER, "networkId=%s LnnRequestLeaveSpecific type=%d", anonyNetworkId, type);
            AnonymizeFree(anonyNetworkId);
            LnnRequestLeaveSpecific(networkId, LnnDiscTypeToConnAddrType((DiscoveryType)type));
            continue;
        }
    }
}

static bool DeletePcNodeInfo(const char *peerUdid)
{
    NodeInfo *localNodeInfo = NULL;
    NodeInfo remoteNodeInfo;
    (void)memset_s(&remoteNodeInfo, sizeof(NodeInfo), 0, sizeof(NodeInfo));
    if (LnnGetRemoteNodeInfoById(peerUdid, CATEGORY_UDID, &remoteNodeInfo)) {
        LNN_LOGE(LNN_BUILDER, "get nodeInfo fail");
        return false;
    }
    if (strcmp(remoteNodeInfo.deviceInfo.deviceUdid, remoteNodeInfo.uuid) != 0) {
        LNN_LOGW(LNN_BUILDER, "isn't pc without softbus");
        return false;
    }
    localNodeInfo = (NodeInfo *)LnnGetLocalNodeInfo();
    if (localNodeInfo == NULL) {
        LNN_LOGE(LNN_BUILDER, "get localinfo fail");
        return false;
    }
    if (remoteNodeInfo.accountId == localNodeInfo->accountId) {
        LNN_LOGI(LNN_BUILDER, "exist sameAccount, don't handle offline");
        return false;
    }
    LNN_LOGI(LNN_BUILDER, "device not trust, delete pc online node");
    DeleteFromProfile(remoteNodeInfo.deviceInfo.deviceUdid);
    LnnRemoveNode(remoteNodeInfo.deviceInfo.deviceUdid);
    return true;
}

static const char *SelectUseUdid(const char *peerUdid, const char *lowerUdid)
{
    char *anonyPeerUdid = NULL;
    Anonymize(peerUdid, &anonyPeerUdid);
    if (LnnGetOnlineStateById(peerUdid, CATEGORY_UDID)) {
        LNN_LOGD(LNN_BUILDER, "not trusted device online! peerUdid=%s", anonyPeerUdid);
        AnonymizeFree(anonyPeerUdid);
        return peerUdid;
    } else if (LnnGetOnlineStateById(lowerUdid, CATEGORY_UDID)) {
        char *anonyLowerUdid = NULL;
        Anonymize(peerUdid, &anonyLowerUdid);
        LNN_LOGD(LNN_BUILDER, "not trusted device online! peerUdid=%s", anonyLowerUdid);
        AnonymizeFree(anonyLowerUdid);
        AnonymizeFree(anonyPeerUdid);
        return lowerUdid;
    } else {
        LNN_LOGW(LNN_BUILDER, "not trusted device not online! peerUdid=%s", anonyPeerUdid);
        AnonymizeFree(anonyPeerUdid);
        return NULL;
    }
}

static void LnnDeleteLinkFinderInfo(const char *peerUdid)
{
    char networkId[NETWORK_ID_BUF_LEN] = {0};
    if (LnnGetNetworkIdByUdid(peerUdid, networkId, sizeof(networkId)) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get networkId fail.");
        return;
    }

    if (LnnRemoveLinkFinderInfo(networkId) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "remove a rpa info fail.");
        return;
    }
}

static void OnDeviceNotTrusted(const char *peerUdid)
{
    if (peerUdid == NULL) {
        LNN_LOGE(LNN_BUILDER, "invalid udid");
        return;
    }
    uint32_t udidLen = strlen(peerUdid) + 1;
    if (udidLen > UDID_BUF_LEN) {
        LNN_LOGE(LNN_BUILDER, "udid is too long");
        return;
    }
    if (DeletePcNodeInfo(peerUdid)) {
        LNN_LOGI(LNN_BUILDER, "pc without softbus, handle offline");
        return;
    }
    const char *useUdid = NULL;
    char udid[UDID_BUF_LEN] =  {0};
    if (StringToLowerCase(peerUdid, udid, UDID_BUF_LEN) != SOFTBUS_OK) {
        return;
    }
    useUdid = SelectUseUdid(peerUdid, udid);
    if (useUdid == NULL) {
        return;
    }
    LnnDeleteLinkFinderInfo(peerUdid);
    NotTrustedDelayInfo *info  = (NotTrustedDelayInfo *)SoftBusCalloc(sizeof(NotTrustedDelayInfo));
    if (info == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc NotTrustedDelayInfo fail");
        return;
    }
    if (AuthGetLatestAuthSeqList(useUdid, info->authSeq, DISCOVERY_TYPE_COUNT) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get latest AuthSeq list fail");
        SoftBusFree(info);
        return;
    }
    if (strcpy_s(info->udid, UDID_BUF_LEN, useUdid) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy udid fail");
        SoftBusFree(info);
        return;
    }
    if (LnnSendNotTrustedInfo(info, DISCOVERY_TYPE_COUNT, LnnProcessCompleteNotTrustedMsg) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "send NotTrustedInfo fail");
        OnLnnProcessNotTrustedMsgDelay((void *)info);
        return;
    }
    if (LnnAsyncCallbackDelayHelper(GetLooper(LOOP_TYPE_DEFAULT), OnLnnProcessNotTrustedMsgDelay,
        (void *)info, NOT_TRUSTED_DEVICE_MSG_DELAY) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "async not trusted msg delay fail");
        SoftBusFree(info);
    }
}

static AuthVerifyListener g_verifyListener = {
    .onDeviceVerifyPass = OnDeviceVerifyPass,
    .onDeviceNotTrusted = OnDeviceNotTrusted,
    .onDeviceDisconnect = OnDeviceDisconnect,
};

static void PostVerifyResult(uint32_t requestId, int32_t retCode, int64_t authId, const NodeInfo *info)
{
    VerifyResultMsgPara *para = NULL;
    para = (VerifyResultMsgPara *)SoftBusCalloc(sizeof(VerifyResultMsgPara));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc verify result msg para fail");
        return;
    }
    para->requestId = requestId;
    para->retCode = retCode;
    if (retCode == SOFTBUS_OK) {
        para->nodeInfo = DupNodeInfo(info);
        if (para->nodeInfo == NULL) {
            LNN_LOGE(LNN_BUILDER, "dup NodeInfo fail");
            SoftBusFree(para);
            return;
        }
        para->authId = authId;
    }
    if (PostMessageToHandler(MSG_TYPE_VERIFY_RESULT, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post verify result message failed");
        SoftBusFree(para->nodeInfo);
        SoftBusFree(para);
    }
    if (info != NULL && retCode == SOFTBUS_OK) {
        LnnNotifyDeviceVerified(info->deviceInfo.deviceUdid);
    }
}

static void OnVerifyPassed(uint32_t requestId, int64_t authId, const NodeInfo *info)
{
    LNN_LOGI(LNN_BUILDER,
        "verify passed: requestId=%u, authId=%" PRId64, requestId, authId);
    if (info == NULL) {
        LNN_LOGE(LNN_BUILDER, "post verify result message failed");
        return;
    }
    PostVerifyResult(requestId, SOFTBUS_OK, authId, info);
}

static void OnVerifyFailed(uint32_t requestId, int32_t reason)
{
    LNN_LOGI(LNN_BUILDER,
        "verify failed: requestId=%u, reason=%d", requestId, reason);
    PostVerifyResult(requestId, reason, AUTH_INVALID_ID, NULL);
}

static AuthVerifyCallback g_verifyCallback = {
    .onVerifyPassed = OnVerifyPassed,
    .onVerifyFailed = OnVerifyFailed,
};

AuthVerifyCallback *LnnGetVerifyCallback(void)
{
    return &g_verifyCallback;
}

static void OnReAuthVerifyPassed(uint32_t requestId, int64_t authId, const NodeInfo *info)
{
    LNN_LOGI(LNN_BUILDER, "reAuth verify passed: requestId=%u, authId=%" PRId64, requestId, authId);
    if (info == NULL) {
        LNN_LOGE(LNN_BUILDER, "reAuth verify result error");
        return;
    }
    AuthRequest authRequest = {0};
    if (GetAuthRequest(requestId, &authRequest) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "auth request not found");
        return;
    }
    ConnectionAddr addr;
    (void)memset_s(&addr, sizeof(ConnectionAddr), 0, sizeof(ConnectionAddr));
    if (!LnnConvertAuthConnInfoToAddr(&addr, &authRequest.connInfo, GetCurrentConnectType())) {
        LNN_LOGE(LNN_BUILDER, "ConvertToConnectionAddr failed");
        return;
    }
    int32_t ret = SoftBusGenerateStrHash((unsigned char *)info->deviceInfo.deviceUdid,
        strlen(info->deviceInfo.deviceUdid), (unsigned char *)addr.info.ble.udidHash);
    if (ret != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "gen udidHash fail");
        return;
    }
    LnnConnectionFsm *connFsm = FindConnectionFsmByAddr(&addr, true);
    if (connFsm != NULL && ((connFsm->connInfo.flag & LNN_CONN_INFO_FLAG_JOIN_PASSIVE) == 0)) {
        if (info != NULL && LnnUpdateGroupType(info) == SOFTBUS_OK && LnnUpdateAccountInfo(info) == SOFTBUS_OK) {
            UpdateProfile(info);
            LNN_LOGI(LNN_BUILDER, "reauth finish and updateProfile");
        }
    } else {
        connFsm = StartNewConnectionFsm(&addr, DEFAULT_PKG_NAME, true);
        if (connFsm == NULL) {
            LNN_LOGE(LNN_BUILDER,
                "start new connection fsm fail=%" PRId64, authId);
            return;
        }
        connFsm->connInfo.authId = authId;
        connFsm->connInfo.nodeInfo = DupNodeInfo(info);
        connFsm->connInfo.flag |= LNN_CONN_INFO_FLAG_JOIN_AUTO;
        LNN_LOGI(LNN_BUILDER,
            "fsmId=%u start a connection fsm, authId=%" PRId64, connFsm->id, authId);
        if (LnnSendAuthResultMsgToConnFsm(connFsm, SOFTBUS_OK) != SOFTBUS_OK) {
            connFsm->connInfo.nodeInfo = NULL;
            StopConnectionFsm(connFsm);
            SoftBusFree(connFsm->connInfo.nodeInfo);
            LNN_LOGE(LNN_BUILDER,
                "fsmId=%u post auth result to connection fsm fail=%" PRId64, connFsm->id, authId);
        }
    }
}

static void OnReAuthVerifyFailed(uint32_t requestId, int32_t reason)
{
    LNN_LOGI(LNN_BUILDER,
        "verify failed: requestId=%u, reason=%d", requestId, reason);
    if (reason != SOFTBUS_AUTH_HICHAIN_AUTH_ERROR) {
        return;
    }
    PostVerifyResult(requestId, reason, AUTH_INVALID_ID, NULL);
}

static AuthVerifyCallback g_reAuthVerifyCallback = {
    .onVerifyPassed = OnReAuthVerifyPassed,
    .onVerifyFailed = OnReAuthVerifyFailed,
};

AuthVerifyCallback *LnnGetReAuthVerifyCallback(void)
{
    return &g_reAuthVerifyCallback;
}

void OnAuthMetaVerifyPassed(uint32_t requestId, int64_t authMetaId, const NodeInfo *info)
{
    if (info == NULL) {
        LNN_LOGE(LNN_META_NODE, "info is NULL");
        return;
    }
    MetaJoinRequestNode *metaNode = FindMetaNodeByRequestId(requestId);
    if (metaNode == NULL) {
        LNN_LOGE(LNN_META_NODE, "not find metaNode");
        return;
    }
    MetaAuthInfo *meta = (MetaAuthInfo *)SoftBusMalloc(sizeof(MetaAuthInfo));
    if (meta == NULL) {
        LNN_LOGE(LNN_META_NODE, "meta is NULL");
        return;
    }
    meta->authMetaId = authMetaId;
    meta->metaJoinNode = metaNode;
    meta->info = *info;
    if (PostMessageToHandler(MSG_TYPE_JOIN_METANODE_AUTH_PASS, meta) != SOFTBUS_OK) {
        LNN_LOGE(LNN_META_NODE, "post join metanode auth pass message failed");
        SoftBusFree(meta);
        return;
    }
}

void OnAuthMetaVerifyFailed(uint32_t requestId, int32_t reason)
{
    MetaJoinRequestNode *metaJoinNode = FindMetaNodeByRequestId(requestId);
    MetaReason *para = (MetaReason *)SoftBusMalloc(sizeof(MetaReason));
    if (para == NULL) {
        LNN_LOGE(LNN_META_NODE, "para is null");
        return;
    }
    para->metaJoinNode = metaJoinNode;
    para->reason = reason;
    LNN_LOGI(LNN_META_NODE, "can find metaNode");
    if (PostMessageToHandler(MSG_TYPE_JOIN_METANODE_AUTH_FAIL, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_META_NODE, "post join metanode authfail message failed");
        SoftBusFree(para);
        return;
    }
}

static AuthVerifyCallback g_metaVerifyCallback = {
    .onVerifyPassed = OnAuthMetaVerifyPassed,
    .onVerifyFailed = OnAuthMetaVerifyFailed,
};

AuthVerifyCallback *LnnGetMetaVerifyCallback(void)
{
    return &g_metaVerifyCallback;
}

NodeInfo *FindNodeInfoByRquestId(uint32_t requestId)
{
    LnnConnectionFsm *connFsm = FindConnectionFsmByRequestId(requestId);
    if (connFsm == NULL || connFsm->isDead) {
        LNN_LOGE(LNN_BUILDER,
            "can not find connection fsm by requestId=%u", requestId);
        return NULL;
    }
    LNN_LOGI(LNN_BUILDER, "find connFsm success");
    if (connFsm->connInfo.nodeInfo == NULL) {
        return NULL;
    }
    return connFsm->connInfo.nodeInfo;
}

int32_t FindRequestIdByAddr(ConnectionAddr *connetionAddr, uint32_t *requestId)
{
    if (requestId == NULL) {
        LNN_LOGE(LNN_BUILDER, "requestId is null");
    }
    LnnConnectionFsm *connFsm = FindConnectionFsmByAddr(connetionAddr, false);
    if (connFsm == NULL || connFsm->isDead) {
        LNN_LOGE(LNN_BUILDER,
            "can not find connection fsm by addr");
        return SOFTBUS_NETWORK_NOT_FOUND;
    }
    LNN_LOGD(LNN_BUILDER, "find connFsm success");
    *requestId = connFsm->connInfo.requestId;
    return SOFTBUS_OK;
}

static ConnectionAddr *CreateConnectionAddrMsgPara(const ConnectionAddr *addr)
{
    ConnectionAddr *para = NULL;

    if (addr == NULL) {
        LNN_LOGE(LNN_BUILDER, "addr is null");
        return NULL;
    }
    para = (ConnectionAddr *)SoftBusCalloc(sizeof(ConnectionAddr));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc connecton addr message fail");
        return NULL;
    }
    *para = *addr;
    return para;
}

static JoinLnnMsgPara *CreateJoinLnnMsgPara(const ConnectionAddr *addr, const char *pkgName, bool isNeedConnect)
{
    JoinLnnMsgPara *para = NULL;

    if (addr == NULL || pkgName == NULL) {
        LNN_LOGE(LNN_BUILDER, "create join lnn msg para is null");
        return NULL;
    }
    para = (JoinLnnMsgPara *)SoftBusCalloc(sizeof(JoinLnnMsgPara));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc connecton addr message fail");
        return NULL;
    }
    if (strcpy_s(para->pkgName, PKG_NAME_SIZE_MAX, pkgName) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy pkgName fail");
        SoftBusFree(para);
        return NULL;
    }
    para->isNeedConnect = isNeedConnect;
    para->addr = *addr;
    return para;
}

static void BuildLnnEvent(LnnEventExtra *lnnEventExtra, const ConnectionAddr *addr)
{
    if (lnnEventExtra == NULL || addr == NULL) {
        LNN_LOGW(LNN_STATE, "lnnEventExtra or addr is null");
        return;
    }
    switch (addr->type) {
        case CONNECTION_ADDR_BR:
            lnnEventExtra->peerBrMac = addr->info.br.brMac;
            break;
        case CONNECTION_ADDR_BLE:
            lnnEventExtra->peerBleMac = addr->info.ble.bleMac;
            break;
        case CONNECTION_ADDR_WLAN:
        case CONNECTION_ADDR_ETH:
            lnnEventExtra->peerIp = addr->info.ip.ip;
            break;
        default:
            LNN_LOGE(LNN_BUILDER, "unknow param type!");
            break;
    }
}

static ConnectionAddrKey *CreateConnectionAddrMsgParaKey(const ConnectionAddrKey *addrDataKey)
{
    ConnectionAddrKey *para = NULL;

    if (addrDataKey == NULL) {
        LNN_LOGE(LNN_BUILDER, "addrDataKey is null");
        return NULL;
    }
    para = (ConnectionAddrKey *)SoftBusCalloc(sizeof(ConnectionAddrKey));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc connecton addrKey message fail");
        return NULL;
    }
    *para = *addrDataKey;
    return para;
}

static char *CreateNetworkIdMsgPara(const char *networkId)
{
    char *para = NULL;

    if (networkId == NULL) {
        LNN_LOGE(LNN_BUILDER, "networkId is null");
        return NULL;
    }
    para = (char *)SoftBusMalloc(NETWORK_ID_BUF_LEN);
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc networkId message fail");
        return NULL;
    }
    if (strncpy_s(para, NETWORK_ID_BUF_LEN, networkId, strlen(networkId)) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy network id fail");
        SoftBusFree(para);
        return NULL;
    }
    return para;
}

static int32_t ConifgLocalLedger(void)
{
    char uuid[UUID_BUF_LEN] = {0};
    char networkId[NETWORK_ID_BUF_LEN] = {0};
    unsigned char irk[LFINDER_IRK_LEN] = {0};

    // set local networkId and uuid
    if (LnnGenLocalNetworkId(networkId, NETWORK_ID_BUF_LEN) != SOFTBUS_OK ||
        LnnGenLocalUuid(uuid, UUID_BUF_LEN) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local id fail");
        return SOFTBUS_NOT_FIND;
    }

    // irk fail should not cause softbus init fail
    if (LnnGenLocalIrk(irk, LFINDER_IRK_LEN) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local irk fail");
    }
    LnnSetLocalStrInfo(STRING_KEY_UUID, uuid);
    LnnSetLocalStrInfo(STRING_KEY_NETWORKID, networkId);
    LnnSetLocalByteInfo(BYTE_KEY_IRK, irk, LFINDER_IRK_LEN);

    // irk fail should not cause softbus init fail
    if (LnnUpdateLinkFinderInfo() != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "sync rpa info to linkfinder fail.");
    }
    return SOFTBUS_OK;
}

static void OnReceiveMasterElectMsg(LnnSyncInfoType type, const char *networkId, const uint8_t *msg, uint32_t len)
{
    JsonObj *json = NULL;
    ElectMsgPara *para = NULL;

    LNN_LOGI(LNN_BUILDER, "recv master elect msg, type=%d, len=%d", type, len);
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return;
    }
    if (type != LNN_INFO_TYPE_MASTER_ELECT) {
        return;
    }
    json = JSON_Parse((char *)msg, len);
    if (json == NULL) {
        LNN_LOGE(LNN_BUILDER, "parse elect msg json fail");
        return;
    }
    para = (ElectMsgPara *)SoftBusMalloc(sizeof(ElectMsgPara));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc elect msg para fail");
        JSON_Delete(json);
        return;
    }
    if (!JSON_GetInt32FromOject(json, JSON_KEY_MASTER_WEIGHT, &para->masterWeight) ||
        !JSON_GetStringFromOject(json, JSON_KEY_MASTER_UDID, para->masterUdid, UDID_BUF_LEN)) {
        LNN_LOGE(LNN_BUILDER, "parse master info json fail");
        JSON_Delete(json);
        SoftBusFree(para);
        return;
    }
    JSON_Delete(json);
    if (strcpy_s(para->networkId, NETWORK_ID_BUF_LEN, networkId) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy network id fail");
        SoftBusFree(para);
        return;
    }
    if (PostMessageToHandler(MSG_TYPE_MASTER_ELECT, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post elect message fail");
        SoftBusFree(para);
    }
}

static int32_t LnnUnpackNodeAddr(const uint8_t *data, uint32_t dataLen, LnnNodeAddr *addr)
{
    cJSON *json = cJSON_Parse((char *)data);
    if (json == NULL) {
        LNN_LOGE(LNN_BUILDER, "json parse failed");
        return SOFTBUS_PARSE_JSON_ERR;
    }
    if (!GetJsonObjectNumberItem(json, JSON_KEY_NODE_CODE, &addr->code) ||
        !GetJsonObjectStringItem(json, JSON_KEY_NODE_ADDR, addr->nodeAddr, SHORT_ADDRESS_MAX_LEN) ||
        !GetJsonObjectNumberItem(json, JSON_KEY_NODE_PROXY_PORT, &addr->proxyPort) ||
        !GetJsonObjectNumberItem(json, JSON_KEY_NODE_SESSION_PORT, &addr->sessionPort)) {
        LNN_LOGE(LNN_BUILDER, "parse addr info failed");
        cJSON_Delete(json);
        return SOFTBUS_PARSE_JSON_ERR;
    }

    cJSON_Delete(json);
    return SOFTBUS_OK;
}

static void OnReceiveNodeAddrChangedMsg(LnnSyncInfoType type, const char *networkId,
    const uint8_t *msg, uint32_t size)
{
    if (type != LNN_INFO_TYPE_NODE_ADDR) {
        return;
    }
    size_t addrLen = strnlen((const char *)msg, size);
    if (addrLen != size - 1 || addrLen == 0) {
        return;
    }
    char *anonyNetworkId = NULL;
    Anonymize(networkId, &anonyNetworkId);
    LNN_LOGI(LNN_BUILDER, "networkId=%s", anonyNetworkId);
    AnonymizeFree(anonyNetworkId);

    LnnNodeAddr addr;
    (void)memset_s(&addr, sizeof(LnnNodeAddr), 0, sizeof(LnnNodeAddr));
    if (LnnUnpackNodeAddr(msg, size, &addr) != SOFTBUS_OK) {
        return;
    }

    SfcSyncNodeAddrHandle(networkId, addr.code);

    if (LnnSetDLNodeAddr(networkId, CATEGORY_NETWORK_ID, addr.nodeAddr) != SOFTBUS_OK) {
        return;
    }

    if (addr.proxyPort > 0) {
        (void)LnnSetDLProxyPort(networkId, CATEGORY_NETWORK_ID, addr.proxyPort);
    }

    if (addr.sessionPort > 0) {
        (void)LnnSetDLSessionPort(networkId, CATEGORY_NETWORK_ID, addr.sessionPort);
    }

    if (addr.authPort > 0) {
        (void)LnnSetDLAuthPort(networkId, CATEGORY_NETWORK_ID, addr.authPort);
    }

    LnnNotifyNodeAddressChanged(addr.nodeAddr, networkId, false);
}

int32_t LnnUpdateNodeAddr(const char *addr)
{
    if (addr == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t ret = LnnSetLocalStrInfo(STRING_KEY_NODE_ADDR, addr);
    if (ret != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "set local node addr failed");
        return ret;
    }

    char localNetworkId[NETWORK_ID_BUF_LEN] = {0};
    ret = LnnGetLocalStrInfo(STRING_KEY_NETWORKID, localNetworkId, sizeof(localNetworkId));
    if (ret != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "get local network id failed");
        return SOFTBUS_NETWORK_NOT_FOUND;
    }
    LnnNotifyNodeAddressChanged(addr, localNetworkId, true);

    return SOFTBUS_OK;
}

static int32_t InitNodeInfoSync(void)
{
    if (LnnInitP2p() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "init lnn p2p fail");
        return SOFTBUS_ERR;
    }
    if (LnnInitNetworkInfo() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "LnnInitNetworkInfo fail");
        return SOFTBUS_ERR;
    }
    if (LnnInitDevicename() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "LnnInitDeviceName fail");
        return SOFTBUS_ERR;
    }
    if (LnnInitOffline() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "LnnInitOffline fail");
        return SOFTBUS_ERR;
    }
    if (LnnInitBatteryInfo() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "LnnInitBatteryInfo fail");
        return SOFTBUS_ERR;
    }
    if (LnnInitCipherKeyManager() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "LnnInitCipherKeyManager fail");
        return SOFTBUS_ERR;
    }
    if (LnnInitWifiDirect() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "init lnn wifidirect addr fail");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

static void DeinitNodeInfoSync(void)
{
    LnnDeinitP2p();
    LnnDeinitNetworkInfo();
    LnnDeinitDevicename();
    LnnDeinitOffline();
    LnnDeinitBatteryInfo();
    LnnDeinitWifiDirect();
}

static void UpdatePCInfoWithoutSoftbus(void)
{
    int32_t onlineNum = 0;
    NodeBasicInfo *info = NULL;
    if (LnnGetAllOnlineNodeInfo(&info, &onlineNum) != 0) {
        LNN_LOGE(LNN_BUILDER, "LnnGetAllOnlineNodeInfo failed!");
        return;
    }
    if (info == NULL || onlineNum == 0) {
        LNN_LOGW(LNN_BUILDER, "not online node");
        return;
    }
    // mark-- remove pc offline
    SoftBusFree(info);
}

static void AccountStateChangeHandler(const LnnEventBasicInfo *info)
{
    if (info == NULL || info->event != LNN_EVENT_ACCOUNT_CHANGED) {
        LNN_LOGW(LNN_BUILDER, "invalid param");
        return;
    }
    const LnnMonitorHbStateChangedEvent *event = (const LnnMonitorHbStateChangedEvent *)info;
    SoftBusAccountState accountState = (SoftBusAccountState)event->status;
    switch (accountState) {
        case SOFTBUS_ACCOUNT_LOG_IN:
            LNN_LOGI(LNN_BUILDER, "ignore SOFTBUS_ACCOUNT_LOG_IN");
            break;
        case SOFTBUS_ACCOUNT_LOG_OUT:
            LNN_LOGI(LNN_BUILDER, "handle SOFTBUS_ACCOUNT_LOG_OUT");
            UpdatePCInfoWithoutSoftbus();
            break;
        default:
            return;
    }
}

int32_t LnnInitNetBuilder(void)
{
    if (g_netBuilder.isInit == true) {
        LNN_LOGI(LNN_INIT, "init net builder repeatly");
        return SOFTBUS_OK;
    }
    if (LnnInitSyncInfoManager() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "init sync info manager fail");
        return SOFTBUS_ERR;
    }
    LnnInitTopoManager();
    InitNodeInfoSync();
    NetBuilderConfigInit();
    // link finder init fail will not cause softbus init fail
    if (LnnLinkFinderInit() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "link finder init fail");
    }
    if (RegAuthVerifyListener(&g_verifyListener) != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "register auth verify listener fail");
        return SOFTBUS_ERR;
    }
    if (LnnRegSyncInfoHandler(LNN_INFO_TYPE_MASTER_ELECT, OnReceiveMasterElectMsg) != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "register sync master elect msg fail");
        return SOFTBUS_ERR;
    }
    if (LnnRegSyncInfoHandler(LNN_INFO_TYPE_NODE_ADDR, OnReceiveNodeAddrChangedMsg) != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "register node addr changed msg fail");
        return SOFTBUS_ERR;
    }
    if (ConifgLocalLedger() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "config local ledger fail");
        return SOFTBUS_ERR;
    }
    if (LnnRegisterEventHandler(LNN_EVENT_ACCOUNT_CHANGED, AccountStateChangeHandler) != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "regist account change evt handler fail!");
        return SOFTBUS_ERR;
    }
    ListInit(&g_netBuilder.fsmList);
    ListInit(&g_netBuilder.pendingList);
    ListInit(&g_netBuilder.metaNodeList);
    g_netBuilder.nodeType = NODE_TYPE_L;
    g_netBuilder.looper = GetLooper(LOOP_TYPE_DEFAULT);
    if (g_netBuilder.looper == NULL) {
        LNN_LOGE(LNN_INIT, "get default looper fail");
        return SOFTBUS_ERR;
    }
    g_netBuilder.handler.name = (char *)"NetBuilderHandler";
    g_netBuilder.handler.looper = g_netBuilder.looper;
    g_netBuilder.handler.HandleMessage = NetBuilderMessageHandler;
    g_netBuilder.isInit = true;
    LNN_LOGI(LNN_INIT, "init net builder success");
    return SOFTBUS_OK;
}

int32_t LnnInitNetBuilderDelay(void)
{
    char udid[UDID_BUF_LEN] = {0};
    // set master weight and master udid
    int32_t ret = LnnGetLocalStrInfo(STRING_KEY_DEV_UDID, udid, UDID_BUF_LEN);
    if (ret != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "get local udid error!");
        return ret;
    }
    LnnSetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, udid);
    LnnSetLocalNumInfo(NUM_KEY_MASTER_NODE_WEIGHT, LnnGetLocalWeight());
    if (LnnInitFastOffline() != SOFTBUS_OK) {
        LNN_LOGE(LNN_INIT, "fast offline init fail!");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

void LnnDeinitNetBuilder(void)
{
    LnnConnectionFsm *item = NULL;
    LnnConnectionFsm *nextItem = NULL;

    if (!g_netBuilder.isInit) {
        return;
    }
    LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        StopConnectionFsm(item);
    }
    LnnUnregSyncInfoHandler(LNN_INFO_TYPE_MASTER_ELECT, OnReceiveMasterElectMsg);
    LnnUnregSyncInfoHandler(LNN_INFO_TYPE_NODE_ADDR, OnReceiveNodeAddrChangedMsg);
    LnnUnregisterEventHandler(LNN_EVENT_ACCOUNT_CHANGED, AccountStateChangeHandler);
    UnregAuthVerifyListener();
    LnnDeinitTopoManager();
    DeinitNodeInfoSync();
    LnnDeinitFastOffline();
    LnnDeinitSyncInfoManager();
    g_netBuilder.isInit = false;
}

int32_t LnnServerJoin(ConnectionAddr *addr, const char *pkgName)
{
    JoinLnnMsgPara *para = NULL;

    LNN_LOGI(LNN_BUILDER, "enter!");
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = CreateJoinLnnMsgPara(addr, pkgName, true);
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "prepare join lnn message fail");
        return SOFTBUS_MALLOC_ERR;
    }
    LnnEventExtra lnnEventExtra = { .callerPkg = pkgName };
    BuildLnnEvent(&lnnEventExtra, addr);
    LNN_EVENT(EVENT_SCENE_JOIN_LNN, EVENT_STAGE_JOIN_LNN_START, lnnEventExtra);
    if (PostMessageToHandler(MSG_TYPE_JOIN_LNN, para) != SOFTBUS_OK) {
        lnnEventExtra.result = EVENT_STAGE_RESULT_FAILED;
        lnnEventExtra.errcode = SOFTBUS_NETWORK_JOIN_LNN_START_ERR;
        LNN_EVENT(EVENT_SCENE_JOIN_LNN, EVENT_STAGE_JOIN_LNN_START, lnnEventExtra);
        LNN_LOGE(LNN_BUILDER, "post join lnn message fail");
        SoftBusFree(para);
        return SOFTBUS_NETWORK_LOOPER_ERR;
    }
    return SOFTBUS_OK;
}

int32_t MetaNodeServerJoin(const char *pkgName, int32_t callingPid,
    ConnectionAddr *addr, CustomData *customData)
{
    ConnectionAddrKey addrDataKey = {
        .addr = *addr,
        .customData = *customData,
        .callingPid = callingPid,
    };
    if (memcpy_s(addrDataKey.pkgName, PKG_NAME_SIZE_MAX, pkgName, strlen(pkgName)) != EOK) {
        LNN_LOGE(LNN_META_NODE, "memcpy_s error");
        return SOFTBUS_MEM_ERR;
    }
    ConnectionAddrKey *para = NULL;
    LNN_LOGD(LNN_META_NODE, "MetaNodeServerJoin enter!");
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_META_NODE, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = CreateConnectionAddrMsgParaKey(&addrDataKey);
    if (para == NULL) {
        LNN_LOGE(LNN_META_NODE, "prepare join lnn message fail");
        return SOFTBUS_MALLOC_ERR;
    }
    if (PostMessageToHandler(MSG_TYPE_JOIN_METANODE, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_META_NODE, "post join lnn message failed");
        SoftBusFree(para);
        return SOFTBUS_NETWORK_LOOPER_ERR;
    }
    return SOFTBUS_OK;
}

int32_t LnnServerLeave(const char *networkId, const char *pkgName)
{
    (void)pkgName;
    char *para = NULL;

    LNN_LOGI(LNN_BUILDER, "enter");
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = CreateNetworkIdMsgPara(networkId);
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "prepare leave lnn message fail");
        return SOFTBUS_MALLOC_ERR;
    }
    LnnEventExtra lnnEventExtra = { .callerPkg = pkgName };
    LNN_EVENT(EVENT_SCENE_LEAVE_LNN, EVENT_STAGE_LEAVE_LNN_START, lnnEventExtra);
    if (PostMessageToHandler(MSG_TYPE_LEAVE_LNN, para) != SOFTBUS_OK) {
        lnnEventExtra.result = EVENT_STAGE_RESULT_FAILED;
        lnnEventExtra.errcode = SOFTBUS_NETWORK_LEAVE_LNN_START_ERR;
        LNN_EVENT(EVENT_SCENE_JOIN_LNN, EVENT_STAGE_LEAVE_LNN_START, lnnEventExtra);
        LNN_LOGE(LNN_BUILDER, "post leave lnn message fail");
        SoftBusFree(para);
        return SOFTBUS_NETWORK_LOOPER_ERR;
    }
    return SOFTBUS_OK;
}

int32_t MetaNodeServerLeave(const char *networkId)
{
    char *para = NULL;

    LNN_LOGI(LNN_META_NODE, "MetaNodeServerLeave enter!");
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_META_NODE, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = CreateNetworkIdMsgPara(networkId);
    if (para == NULL) {
        LNN_LOGE(LNN_META_NODE, "prepare leave lnn message fail");
        return SOFTBUS_MALLOC_ERR;
    }
    if (PostMessageToHandler(MSG_TYPE_LEAVE_METANODE, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_META_NODE, "post leave lnn message failed");
        SoftBusFree(para);
        return SOFTBUS_NETWORK_LOOPER_ERR;
    }
    return SOFTBUS_OK;
}

int32_t LnnNotifyDiscoveryDevice(const ConnectionAddr *addr, bool isNeedConnect)
{
    JoinLnnMsgPara *para = NULL;

    LnnEventExtra lnnEventExtra = {0};
    BuildLnnEvent(&lnnEventExtra, addr);
    LNN_EVENT(EVENT_SCENE_JOIN_LNN, EVENT_STAGE_JOIN_LNN_START, lnnEventExtra);
    LNN_LOGI(LNN_BUILDER, "notify discovery device enter! isNeedConnect = %d", isNeedConnect);
    if (g_netBuilder.isInit == false) {
        lnnEventExtra.result = EVENT_STAGE_RESULT_FAILED;
        lnnEventExtra.errcode = SOFTBUS_NETWORK_JOIN_LNN_START_ERR;
        LNN_EVENT(EVENT_SCENE_JOIN_LNN, EVENT_STAGE_JOIN_LNN_START, lnnEventExtra);
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = CreateJoinLnnMsgPara(addr, DEFAULT_PKG_NAME, isNeedConnect);
    if (para == NULL) {
        lnnEventExtra.result = EVENT_STAGE_RESULT_FAILED;
        lnnEventExtra.errcode = SOFTBUS_NETWORK_JOIN_LNN_START_ERR;
        LNN_EVENT(EVENT_SCENE_JOIN_LNN, EVENT_STAGE_JOIN_LNN_START, lnnEventExtra);
        LNN_LOGE(LNN_BUILDER, "malloc discovery device message fail");
        return SOFTBUS_MALLOC_ERR;
    }
    if (PostMessageToHandler(MSG_TYPE_DISCOVERY_DEVICE, para) != SOFTBUS_OK) {
        lnnEventExtra.result = EVENT_STAGE_RESULT_FAILED;
        lnnEventExtra.errcode = SOFTBUS_NETWORK_JOIN_LNN_START_ERR;
        LNN_EVENT(EVENT_SCENE_JOIN_LNN, EVENT_STAGE_JOIN_LNN_START, lnnEventExtra);
        LNN_LOGE(LNN_BUILDER, "post notify discovery device message failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t LnnRequestLeaveInvalidConn(const char *oldNetworkId, ConnectionAddrType addrType,
    const char *newNetworkId)
{
    LeaveInvalidConnMsgPara *para = NULL;

    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = (LeaveInvalidConnMsgPara *)SoftBusMalloc(sizeof(LeaveInvalidConnMsgPara));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "prepare leave invalid connection message fail");
        return SOFTBUS_MALLOC_ERR;
    }
    if (strncpy_s(para->oldNetworkId, NETWORK_ID_BUF_LEN, oldNetworkId, strlen(oldNetworkId)) != EOK ||
        strncpy_s(para->newNetworkId, NETWORK_ID_BUF_LEN, newNetworkId, strlen(newNetworkId)) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy old networkId or new networkId fail");
        SoftBusFree(para);
        return SOFTBUS_MALLOC_ERR;
    }
    para->addrType = addrType;
    if (PostMessageToHandler(MSG_TYPE_LEAVE_INVALID_CONN, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post leave invalid connection message failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t LnnRequestCleanConnFsm(uint16_t connFsmId)
{
    uint16_t *para = NULL;

    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_ERR;
    }
    para = (uint16_t *)SoftBusMalloc(sizeof(uint16_t));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc clean connection fsm msg failed");
        return SOFTBUS_MALLOC_ERR;
    }
    *para = connFsmId;
    if (PostMessageToHandler(MSG_TYPE_CLEAN_CONN_FSM, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post request clean connectionlnn message failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

void LnnSyncOfflineComplete(LnnSyncInfoType type, const char *networkId, const uint8_t *msg, uint32_t len)
{
    char *para = NULL;

    (void)type;
    (void)msg;
    (void)len;
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return;
    }
    para = CreateNetworkIdMsgPara(networkId);
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "prepare notify sync offline message fail");
        return;
    }
    LnnEventExtra lnnEventExtra = {0};
    LNN_EVENT(EVENT_SCENE_LEAVE_LNN, EVENT_STAGE_LEAVE_LNN_START, lnnEventExtra);
    if (PostMessageToHandler(MSG_TYPE_SYNC_OFFLINE_FINISH, para) != SOFTBUS_OK) {
        lnnEventExtra.result = EVENT_STAGE_RESULT_FAILED;
        lnnEventExtra.errcode = SOFTBUS_NETWORK_LEAVE_LNN_START_ERR;
        LNN_EVENT(EVENT_SCENE_LEAVE_LNN, EVENT_STAGE_LEAVE_LNN_START, lnnEventExtra);
        LNN_LOGE(LNN_BUILDER, "post sync offline finish message failed");
        SoftBusFree(para);
    }
}

int32_t LnnNotifyNodeStateChanged(const ConnectionAddr *addr)
{
    ConnectionAddr *para = NULL;

    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = CreateConnectionAddrMsgPara(addr);
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "create node state changed msg failed");
        return SOFTBUS_MALLOC_ERR;
    }
    if (PostMessageToHandler(MSG_TYPE_NODE_STATE_CHANGED, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post node state changed message failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t LnnNotifyMasterElect(const char *networkId, const char *masterUdid, int32_t masterWeight)
{
    ElectMsgPara *para = NULL;

    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    if (networkId == NULL || masterUdid == NULL) {
        LNN_LOGE(LNN_BUILDER, "invalid elect msg para");
        return SOFTBUS_INVALID_PARAM;
    }
    para = (ElectMsgPara *)SoftBusMalloc(sizeof(ElectMsgPara));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc elect msg para failed");
        return SOFTBUS_MEM_ERR;
    }
    if (strncpy_s(para->networkId, NETWORK_ID_BUF_LEN, networkId, strlen(networkId)) != EOK ||
        strncpy_s(para->masterUdid, UDID_BUF_LEN, masterUdid, strlen(masterUdid)) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy udid and maser udid failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    para->masterWeight = masterWeight;
    if (PostMessageToHandler(MSG_TYPE_MASTER_ELECT, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post elect message failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

/* Note: must called in connection fsm. */
int32_t LnnNotifyAuthHandleLeaveLNN(int64_t authId)
{
    LnnConnectionFsm *item = NULL;

    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }

    LIST_FOR_EACH_ENTRY(item, &g_netBuilder.fsmList, LnnConnectionFsm, node) {
        if (item->isDead) {
            continue;
        }
        if (item->connInfo.authId == authId) {
            LNN_LOGI(LNN_BUILDER,
                "fsmId=%u connection fsm already use authId=%" PRId64, item->id, authId);
            return SOFTBUS_OK;
        }
    }
    AuthHandleLeaveLNN(authId);
    return SOFTBUS_OK;
}

int32_t LnnRequestLeaveByAddrType(const bool *type, uint32_t typeLen)
{
    if (type == NULL) {
        LNN_LOGE(LNN_BUILDER, "para is null");
        return SOFTBUS_INVALID_PARAM;
    }
    bool *para = NULL;
    if (typeLen != CONNECTION_ADDR_MAX) {
        LNN_LOGE(LNN_BUILDER, "invalid typeLen");
        return SOFTBUS_ERR;
    }
    LNN_LOGD(LNN_BUILDER, "[wlan:%d][br:%d][ble:%d][eth:%d]",
        type[CONNECTION_ADDR_WLAN], type[CONNECTION_ADDR_BR], type[CONNECTION_ADDR_BLE], type[CONNECTION_ADDR_ETH]);
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = (bool *)SoftBusMalloc(sizeof(bool) * typeLen);
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc leave by addr type msg para failed");
        return SOFTBUS_MALLOC_ERR;
    }
    if (memcpy_s(para, sizeof(bool) * typeLen, type, sizeof(bool) * typeLen) != EOK) {
        LNN_LOGE(LNN_BUILDER, "memcopy para fail");
        SoftBusFree(para);
        return SOFTBUS_MEM_ERR;
    }
    if (PostMessageToHandler(MSG_TYPE_LEAVE_BY_ADDR_TYPE, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post leave by addr type message failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t LnnRequestLeaveSpecific(const char *networkId, ConnectionAddrType addrType)
{
    SpecificLeaveMsgPara *para = NULL;

    if (networkId == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    if (g_netBuilder.isInit == false) {
        LNN_LOGE(LNN_BUILDER, "no init");
        return SOFTBUS_NO_INIT;
    }
    para = (SpecificLeaveMsgPara *)SoftBusCalloc(sizeof(SpecificLeaveMsgPara));
    if (para == NULL) {
        LNN_LOGE(LNN_BUILDER, "malloc specific msg fail");
        return SOFTBUS_MALLOC_ERR;
    }
    if (strcpy_s(para->networkId, NETWORK_ID_BUF_LEN, networkId) != EOK) {
        LNN_LOGE(LNN_BUILDER, "copy networkId fail");
        SoftBusFree(para);
        return SOFTBUS_STRCPY_ERR;
    }
    para->addrType = addrType;
    if (PostMessageToHandler(MSG_TYPE_LEAVE_SPECIFIC, para) != SOFTBUS_OK) {
        LNN_LOGE(LNN_BUILDER, "post leave specific msg failed");
        SoftBusFree(para);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

void LnnRequestLeaveAllOnlineNodes(void)
{
    int32_t onlineNum;
    NodeBasicInfo *info;
    char *para = NULL;
    if (LnnGetAllOnlineNodeInfo(&info, &onlineNum) != 0) {
        LNN_LOGE(LNN_BUILDER, "LnnGetAllOnlineNodeInfo failed");
        return;
    }
    if (info == NULL || onlineNum == 0) {
        LNN_LOGW(LNN_BUILDER, "none online node");
        return;
    }
    for (int32_t i = 0; i < onlineNum; i++) {
        para = CreateNetworkIdMsgPara(info[i].networkId);
        if (para == NULL) {
            LNN_LOGE(LNN_BUILDER, "prepare leave lnn message fail");
            break;
        }
        if (PostMessageToHandler(MSG_TYPE_LEAVE_LNN, para) != SOFTBUS_OK) {
            LNN_LOGE(LNN_BUILDER, "post leave lnn message failed");
            SoftBusFree(para);
            break;
        }
    }
    SoftBusFree(info);
}