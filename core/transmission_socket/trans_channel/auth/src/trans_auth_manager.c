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

#include "trans_auth_manager.h"

#include "auth_channel.h"
#include "bus_center_manager.h"
#include "common_list.h"
#include "lnn_connection_addr_utils.h"
#include "lnn_net_builder.h"
#include "securec.h"
#include "softbus_adapter_mem.h"
#include "softbus_adapter_thread.h"
#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_feature_config.h"
#include "softbus_log.h"
#include "softbus_utils.h"
#include "trans_auth_message.h"
#include "trans_channel_limit.h"
#include "trans_session_manager.h"
#include "trans_channel_manager.h"

#define AUTH_CHANNEL_REQ 0
#define AUTH_CHANNEL_REPLY 1

#define AUTH_GROUP_ID "auth group id"
#define AUTH_SESSION_KEY "auth session key"

typedef struct {
    ListNode node;
    AppInfo appInfo;
    int32_t authId;
    ConnectOption connOpt;
    bool isClient;
} AuthChannelInfo;

typedef struct {
    int32_t channelType;
    int32_t businessType;
    ConfigType configType;
} ConfigTypeMap;

static SoftBusList *g_authChannelList = NULL;
static IServerChannelCallBack *g_cb = NULL;

static void TransPostAuthChannelErrMsg(int32_t authId, int32_t errcode, const char *errMsg);
static int32_t TransPostAuthChannelMsg(const AppInfo *appInfo, int32_t authId, int32_t flag);
static AuthChannelInfo *CreateAuthChannelInfo(const char *sessionName, bool isClient);
static int32_t AddAuthChannelInfo(AuthChannelInfo *info);
static void DelAuthChannelInfoByChanId(int32_t channelId);
static void DelAuthChannelInfoByAuthId(int32_t authId);

static int32_t GetAuthChannelInfoByChanId(int32_t channelId, AuthChannelInfo *dstInfo)
{
    if (dstInfo == NULL || g_authChannelList == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }

    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return SOFTBUS_LOCK_ERR;
    }
    AuthChannelInfo *info = NULL;
    LIST_FOR_EACH_ENTRY(info, &g_authChannelList->list, AuthChannelInfo, node) {
        if (info->appInfo.myData.channelId == channelId) {
            if (memcpy_s(dstInfo, sizeof(AuthChannelInfo), info, sizeof(AuthChannelInfo)) != EOK) {
                (void)SoftBusMutexUnlock(&g_authChannelList->lock);
                return SOFTBUS_MEM_ERR;
            }
            (void)SoftBusMutexUnlock(&g_authChannelList->lock);
            return SOFTBUS_OK;
        }
    }
    SoftBusMutexUnlock(&g_authChannelList->lock);
    return SOFTBUS_ERR;
}

static int32_t GetAuthIdByChannelId(int32_t channelId)
{
    if (g_authChannelList == NULL) {
        return SOFTBUS_ERR;
    }

    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return SOFTBUS_LOCK_ERR;
    }
    int32_t authId = -1;
    AuthChannelInfo *info = NULL;
    LIST_FOR_EACH_ENTRY(info, &g_authChannelList->list, AuthChannelInfo, node) {
        if (info->appInfo.myData.channelId == channelId) {
            authId = info->authId;
            (void)SoftBusMutexUnlock(&g_authChannelList->lock);
            return authId;
        }
    }
    SoftBusMutexUnlock(&g_authChannelList->lock);
    return authId;
}

static int32_t GetChannelInfoByAuthId(int32_t authId, AuthChannelInfo *dstInfo)
{
    if (dstInfo == NULL || g_authChannelList == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }

    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return SOFTBUS_LOCK_ERR;
    }
    AuthChannelInfo *info = NULL;
    LIST_FOR_EACH_ENTRY(info, &g_authChannelList->list, AuthChannelInfo, node) {
        if (info->authId == authId) {
            if (memcpy_s(dstInfo, sizeof(AuthChannelInfo), info, sizeof(AuthChannelInfo)) != EOK) {
                (void)SoftBusMutexUnlock(&g_authChannelList->lock);
                return SOFTBUS_MEM_ERR;
            }
            (void)SoftBusMutexUnlock(&g_authChannelList->lock);
            return SOFTBUS_OK;
        }
    }
    SoftBusMutexUnlock(&g_authChannelList->lock);
    return SOFTBUS_ERR;
}

static int32_t NotifyOpenAuthChannelSuccess(const AppInfo *appInfo, bool isServer)
{
    ChannelInfo channelInfo = {0};
    channelInfo.channelType = CHANNEL_TYPE_AUTH;
    channelInfo.isServer = isServer;
    channelInfo.isEnabled = true;
    channelInfo.channelId = appInfo->myData.channelId;
    channelInfo.peerDeviceId = (char *)appInfo->peerData.deviceId;
    channelInfo.peerSessionName = (char *)appInfo->peerData.sessionName;
    channelInfo.businessType = BUSINESS_TYPE_NOT_CARE;
    channelInfo.groupId = (char *)AUTH_GROUP_ID;
    channelInfo.isEncrypt = false;
    channelInfo.sessionKey = (char *)AUTH_SESSION_KEY;
    channelInfo.keyLen = strlen(channelInfo.sessionKey) + 1;
    channelInfo.autoCloseTime = appInfo->autoCloseTime;
    channelInfo.reqId = (char*)appInfo->reqId;
    channelInfo.dataConfig = appInfo->myData.dataConfig;
    return g_cb->OnChannelOpened(appInfo->myData.pkgName, appInfo->myData.pid,
        appInfo->myData.sessionName, &channelInfo);
}

static int32_t NotifyOpenAuthChannelFailed(const char *pkgName, int32_t pid, int32_t channelId,
    int32_t errCode)
{
    return g_cb->OnChannelOpenFailed(pkgName, pid, channelId, CHANNEL_TYPE_AUTH, errCode);
}

static int32_t NofifyCloseAuthChannel(const char *pkgName, int32_t pid, int32_t channelId)
{
    return g_cb->OnChannelClosed(pkgName, pid, channelId, CHANNEL_TYPE_AUTH);
}

static int32_t AuthGetUidAndPidBySessionName(const char *sessionName, int32_t *uid, int32_t *pid)
{
    return g_cb->GetUidAndPidBySessionName(sessionName, uid, pid);
}

static int32_t NotifyOnDataReceived(int32_t authId, const void *data, uint32_t len)
{
    AuthChannelInfo channel;
    if (GetChannelInfoByAuthId(authId, &channel) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    TransReceiveData receiveData;
    receiveData.data = (void*)data;
    receiveData.dataLen = len;
    receiveData.dataType = TRANS_SESSION_BYTES;

    return g_cb->OnDataReceived(channel.appInfo.myData.pkgName, channel.appInfo.myData.pid,
        channel.appInfo.myData.channelId, CHANNEL_TYPE_AUTH, &receiveData);
}

static int32_t CopyPeerAppInfo(AppInfo *recvAppInfo, AppInfo *channelAppInfo)
{
    if (recvAppInfo == NULL || channelAppInfo == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    if (memcpy_s(channelAppInfo->peerData.deviceId, DEVICE_ID_SIZE_MAX,
            recvAppInfo->peerData.deviceId, DEVICE_ID_SIZE_MAX) != EOK ||
        memcpy_s(recvAppInfo->myData.deviceId, DEVICE_ID_SIZE_MAX,
            channelAppInfo->myData.deviceId, DEVICE_ID_SIZE_MAX) != EOK ||
        memcpy_s(channelAppInfo->peerData.pkgName, PKG_NAME_SIZE_MAX,
            recvAppInfo->peerData.pkgName, PKG_NAME_SIZE_MAX) != EOK ||
        memcpy_s(recvAppInfo->myData.pkgName, PKG_NAME_SIZE_MAX,
            channelAppInfo->myData.pkgName, PKG_NAME_SIZE_MAX) != EOK ||
        memcpy_s(channelAppInfo->peerData.sessionName, SESSION_NAME_SIZE_MAX,
            recvAppInfo->peerData.sessionName, SESSION_NAME_SIZE_MAX) != EOK) {
        return SOFTBUS_MEM_ERR;
    }
    return SOFTBUS_OK;
}

static int32_t OnRequsetUpdateAuthChannel(int32_t authId, AppInfo *appInfo)
{
    if (appInfo == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    AuthChannelInfo *item = NULL;
    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return SOFTBUS_LOCK_ERR;
    }
    bool exists = false;
    LIST_FOR_EACH_ENTRY(item, &g_authChannelList->list, AuthChannelInfo, node) {
        if (item->authId == authId) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        item = CreateAuthChannelInfo(appInfo->myData.sessionName, false);
        if (item == NULL) {
            SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "CreateAuthChannelInfo failed");
            SoftBusMutexUnlock(&g_authChannelList->lock);
            return SOFTBUS_ERR;
        }
        item->authId = authId;
        appInfo->myData.channelId = item->appInfo.myData.channelId;
        appInfo->myData.dataConfig = item->appInfo.myData.dataConfig;
        if (AddAuthChannelInfo(item) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "AddAuthChannelInfo failed");
            SoftBusFree(item);
            SoftBusMutexUnlock(&g_authChannelList->lock);
            return SOFTBUS_ERR;
        }
    }
    if (CopyPeerAppInfo(appInfo, &item->appInfo) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "CopyPeerAppInfo failed");
        SoftBusFree(item);
        SoftBusMutexUnlock(&g_authChannelList->lock);
        return SOFTBUS_MEM_ERR;
    }
    SoftBusMutexUnlock(&g_authChannelList->lock);
    return SOFTBUS_OK;
}

static const ConfigTypeMap CONFIG_TYPE_MAP[] = {
    {CHANNEL_TYPE_AUTH, BUSINESS_TYPE_BYTE, SOFTBUS_INT_AUTH_MAX_BYTES_LENGTH},
    {CHANNEL_TYPE_AUTH, BUSINESS_TYPE_MESSAGE, SOFTBUS_INT_AUTH_MAX_MESSAGE_LENGTH},
};

static int32_t FindConfigType(int32_t channelType, int32_t businessType)
{
    for (uint32_t i = 0; i < sizeof(CONFIG_TYPE_MAP) / sizeof(ConfigTypeMap); i++) {
        if ((CONFIG_TYPE_MAP[i].channelType == channelType) && (CONFIG_TYPE_MAP[i].businessType == businessType)) {
            return CONFIG_TYPE_MAP[i].configType;
        }
    }
    return SOFTBUS_CONFIG_TYPE_MAX;
}

static int TransGetLocalConfig(int32_t channelType, int32_t businessType, uint32_t *len)
{
    ConfigType configType = (ConfigType)FindConfigType(channelType, businessType);
    if (configType == SOFTBUS_CONFIG_TYPE_MAX) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "Invalid channelType[%d] businessType[%d]",
            channelType, businessType);
        return SOFTBUS_INVALID_PARAM;
    }
    uint32_t maxLen;
    if (SoftbusGetConfig(configType, (unsigned char *)&maxLen, sizeof(maxLen)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "get fail configType[%d]", configType);
        return SOFTBUS_GET_CONFIG_VAL_ERR;
    }
    *len = maxLen;
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "get appinfo local config[%d]", *len);
    return SOFTBUS_OK;
}

static int32_t TransAuthFillDataConfig(AppInfo *appInfo)
{
    if (appInfo == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "appInfo is null");
        return SOFTBUS_ERR;
    }
    appInfo->businessType = BUSINESS_TYPE_BYTE;
    if (appInfo->peerData.dataConfig != 0) {
        uint32_t localDataConfig = 0;
        if (TransGetLocalConfig(CHANNEL_TYPE_AUTH, appInfo->businessType, &localDataConfig) != SOFTBUS_OK) {
            return SOFTBUS_ERR;
        }
        appInfo->myData.dataConfig = MIN(localDataConfig, appInfo->peerData.dataConfig);
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "fill dataConfig[%u] succ", appInfo->myData.dataConfig);
        return SOFTBUS_OK;
    }
    ConfigType configType = appInfo->businessType == BUSINESS_TYPE_BYTE ?
        SOFTBUS_INT_AUTH_MAX_BYTES_LENGTH : SOFTBUS_INT_AUTH_MAX_MESSAGE_LENGTH;
    if (SoftbusGetConfig(configType, (unsigned char *)&appInfo->myData.dataConfig,
        sizeof(appInfo->myData.dataConfig)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "get config failed, configType[%d]", configType);
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "fill data config value[%d]", appInfo->myData.dataConfig);
    return SOFTBUS_OK;
}

static void OnRecvAuthChannelRequest(int32_t authId, const char *data, int32_t len)
{
    if (data == NULL || len <= 0) {
        return;
    }

    AppInfo appInfo;
    int32_t ret = TransAuthChannelMsgUnpack(data, &appInfo, len);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "unpackRequest failed");
        TransPostAuthChannelErrMsg(authId, ret, "unpackRequest");
        goto EXIT_ERR;
    }
    if (!CheckSessionNameValidOnAuthChannel(appInfo.myData.sessionName)) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "check auth channel pkginfo invalid.");
        TransPostAuthChannelErrMsg(authId, ret, "check msginfo failed");
        goto EXIT_ERR;
    }
    ret = AuthGetUidAndPidBySessionName(appInfo.myData.sessionName, &appInfo.myData.uid, &appInfo.myData.pid);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "AuthGetUidAndPidBySessionName failed");
        goto EXIT_ERR;
    }
    ret = TransAuthFillDataConfig(&appInfo);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "TransAuthFillDataConfig failed");
        goto EXIT_ERR;
    }
    ret = OnRequsetUpdateAuthChannel(authId, &appInfo);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "update auth channel failed");
        TransPostAuthChannelErrMsg(authId, ret, "unpackRequest");
        goto EXIT_ERR;
    }
    ret = NotifyOpenAuthChannelSuccess(&appInfo, true);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "NotifyOpenAuthChannelSuccess failed");
        TransPostAuthChannelErrMsg(authId, ret, "NotifyOpenAuthChannelSuccess failed");
        goto EXIT_ERR;
    }
    ret = TransPostAuthChannelMsg(&appInfo, authId, AUTH_CHANNEL_REPLY);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "send reply failed");
        TransPostAuthChannelErrMsg(authId, ret, "send reply failed");
        goto EXIT_ERR;
    }
    return;
EXIT_ERR:
    DelAuthChannelInfoByAuthId(authId);
    AuthCloseChannel(authId);
}

static int32_t TransAuthProcessDataConfig(AppInfo *appInfo)
{
    if (appInfo == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "appInfo is null");
        return SOFTBUS_ERR;
    }
    if (appInfo->businessType != BUSINESS_TYPE_MESSAGE && appInfo->businessType != BUSINESS_TYPE_BYTE) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "invalid businessType[%d]", appInfo->businessType);
        return SOFTBUS_OK;
    }
    if (appInfo->peerData.dataConfig != 0) {
        appInfo->myData.dataConfig = MIN(appInfo->myData.dataConfig, appInfo->peerData.dataConfig);
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "process dataConfig[%u] succ", appInfo->myData.dataConfig);
        return SOFTBUS_OK;
    }
    ConfigType configType = appInfo->businessType == BUSINESS_TYPE_BYTE ?
        SOFTBUS_INT_AUTH_MAX_BYTES_LENGTH : SOFTBUS_INT_AUTH_MAX_MESSAGE_LENGTH;
    if (SoftbusGetConfig(configType, (unsigned char *)&appInfo->myData.dataConfig,
        sizeof(appInfo->myData.dataConfig)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "get config failed, configType[%d]", configType);
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "process data config value[%d]", appInfo->myData.dataConfig);
    return SOFTBUS_OK;
}

static void OnRecvAuthChannelReply(int32_t authId, const char *data, int32_t len)
{
    if (data == NULL || len <= 0) {
        return;
    }
    AuthChannelInfo info;
    if (GetChannelInfoByAuthId(authId, &info) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "can not find channel info by auth id");
        return;
    }
    int32_t ret = TransAuthChannelMsgUnpack(data, &info.appInfo, len);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "unpackReply failed");
        goto EXIT_ERR;
    }
    ret = TransAuthProcessDataConfig(&info.appInfo);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "ProcessDataConfig failed");
        goto EXIT_ERR;
    }
    ret = NotifyOpenAuthChannelSuccess(&info.appInfo, false);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "NotifyOpenAuthChannelSuccess failed");
        goto EXIT_ERR;
    }
    return;
EXIT_ERR:
    AuthCloseChannel(authId);
    DelAuthChannelInfoByChanId(info.appInfo.myData.channelId);
    (void)NotifyOpenAuthChannelFailed(info.appInfo.myData.pkgName, info.appInfo.myData.pid,
        info.appInfo.myData.channelId, ret);
}

static void OnAuthChannelDataRecv(int32_t authId, const AuthChannelData *data)
{
    if (data == NULL || data->data == NULL || data->len < 1) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "invalid param.");
        return;
    }

    if (data->flag == AUTH_CHANNEL_REQ) {
        OnRecvAuthChannelRequest(authId, (const char *)data->data, data->len);
    } else if (data->flag == AUTH_CHANNEL_REPLY) {
        OnRecvAuthChannelReply(authId, (const char *)data->data, data->len);
    } else {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth channel flags err, authId=%d", authId);
    }
}

static void OnAuthMsgDataRecv(int32_t authId, const AuthChannelData *data)
{
    if (data == NULL || data->data == NULL) {
        return;
    }
    if (NotifyOnDataReceived(authId, data->data, data->len) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth=%d recv MODULE_AUTH_MSG err", authId);
    }
}

static void OnDisconnect(int32_t authId)
{
    AuthChannelInfo dstInfo;
    if (GetChannelInfoByAuthId(authId, &dstInfo) != EOK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth=%d channel already removed", authId);
        return;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "recv auth=%d channel disconnect event.", authId);
    DelAuthChannelInfoByChanId(dstInfo.appInfo.myData.channelId);
    (void)NofifyCloseAuthChannel(dstInfo.appInfo.myData.pkgName, dstInfo.appInfo.myData.pid,
        dstInfo.appInfo.myData.channelId);
}

static int32_t GetAppInfo(const char *sessionName, int32_t channelId, AppInfo *appInfo, bool isClient)
{
    if (sessionName == NULL || appInfo == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    appInfo->appType = APP_TYPE_NOT_CARE;
    appInfo->businessType = BUSINESS_TYPE_BYTE;
    appInfo->channelType = CHANNEL_TYPE_AUTH;
    appInfo->myData.channelId = channelId;
    appInfo->myData.apiVersion = API_V2;
    appInfo->peerData.apiVersion = API_V2;
    appInfo->autoCloseTime = 0;
    if ((!IsNoPkgNameSession(sessionName)) || (isClient)) {
        if (TransGetUidAndPid(sessionName, &appInfo->myData.uid, &appInfo->myData.pid) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "TransGetUidAndPid failed");
            return SOFTBUS_ERR;
        }
        if (TransGetPkgNameBySessionName(sessionName, appInfo->myData.pkgName, PKG_NAME_SIZE_MAX) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "TransGetPkgNameBySessionName failed");
            return SOFTBUS_ERR;
        }
    }
    if (LnnGetLocalStrInfo(STRING_KEY_DEV_UDID, appInfo->myData.deviceId,
        sizeof(appInfo->myData.deviceId)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "LnnGetLocalStrInfo failed");
        return SOFTBUS_ERR;
    }
    if (strcpy_s(appInfo->myData.sessionName, sizeof(appInfo->myData.sessionName), sessionName) != EOK) {
        return SOFTBUS_ERR;
    }
    appInfo->peerData.apiVersion = API_V2;
    if (strcpy_s(appInfo->peerData.sessionName, sizeof(appInfo->peerData.sessionName), sessionName) != 0) {
        return SOFTBUS_ERR;
    }
    if (TransGetLocalConfig(appInfo->channelType, appInfo->businessType, &appInfo->myData.dataConfig) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

static int32_t AddAuthChannelInfo(AuthChannelInfo *info)
{
    if (g_authChannelList == NULL || info == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return SOFTBUS_LOCK_ERR;
    }
    AuthChannelInfo *item = NULL;
    LIST_FOR_EACH_ENTRY(item, &g_authChannelList->list, AuthChannelInfo, node) {
        if (item->appInfo.myData.channelId == info->appInfo.myData.channelId) {
            (void)SoftBusMutexUnlock(&g_authChannelList->lock);
            return SOFTBUS_ERR;
        }
    }
    ListAdd(&g_authChannelList->list, &info->node);
    g_authChannelList->cnt++;
    (void)SoftBusMutexUnlock(&g_authChannelList->lock);
    return SOFTBUS_OK;
}

static void DelAuthChannelInfoByChanId(int32_t channelId)
{
    if (g_authChannelList == NULL) {
        return;
    }
    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return;
    }
    AuthChannelInfo *item = NULL;
    AuthChannelInfo *tmp = NULL;
    LIST_FOR_EACH_ENTRY_SAFE(item, tmp, &g_authChannelList->list, AuthChannelInfo, node) {
        if (item->appInfo.myData.channelId == channelId) {
            ListDelete(&item->node);
            SoftBusFree(item);
            g_authChannelList->cnt--;
            break;
        }
    }
    (void)SoftBusMutexUnlock(&g_authChannelList->lock);
}

static void DelAuthChannelInfoByAuthId(int32_t authId)
{
    if (g_authChannelList == NULL) {
        return;
    }
    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return;
    }
    AuthChannelInfo *item = NULL;
    AuthChannelInfo *tmp = NULL;
    LIST_FOR_EACH_ENTRY_SAFE(item, tmp, &g_authChannelList->list, AuthChannelInfo, node) {
        if (item->authId == authId) {
            ListDelete(&item->node);
            SoftBusFree(item);
            g_authChannelList->cnt--;
            break;
        }
    }
    (void)SoftBusMutexUnlock(&g_authChannelList->lock);
}

int32_t TransAuthGetNameByChanId(int32_t chanId, char *pkgName, char *sessionName,
    uint16_t pkgLen, uint16_t sessionLen)
{
    if (pkgName == NULL || sessionName == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }

    AuthChannelInfo info;
    if (GetAuthChannelInfoByChanId(chanId, &info) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }

    if (memcpy_s(pkgName, pkgLen, info.appInfo.myData.pkgName, PKG_NAME_SIZE_MAX) != EOK ||
        memcpy_s(sessionName, sessionLen, info.appInfo.myData.sessionName, SESSION_NAME_SIZE_MAX) != EOK) {
        return SOFTBUS_MEM_ERR;
    }
    return SOFTBUS_OK;
}

int32_t TransAuthInit(IServerChannelCallBack *cb)
{
    if (cb == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    AuthChannelListener channelListener = {
        .onDataReceived = OnAuthChannelDataRecv,
        .onDisconnected = OnDisconnect,
    };
    AuthChannelListener msgListener = {
        .onDataReceived = OnAuthMsgDataRecv,
        .onDisconnected = OnDisconnect,
    };
    if (RegAuthChannelListener(MODULE_AUTH_CHANNEL, &channelListener) != SOFTBUS_OK ||
        RegAuthChannelListener(MODULE_AUTH_MSG, &msgListener) != SOFTBUS_OK) {
        UnregAuthChannelListener(MODULE_AUTH_CHANNEL);
        UnregAuthChannelListener(MODULE_AUTH_MSG);
        return SOFTBUS_ERR;
    }
    if (g_authChannelList == NULL) {
        g_authChannelList = CreateSoftBusList();
    }
    if (g_authChannelList == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    if (g_cb == NULL) {
        g_cb = cb;
    }
    return SOFTBUS_OK;
}

void TransAuthDeinit(void)
{
    UnregAuthChannelListener(MODULE_AUTH_CHANNEL);
    UnregAuthChannelListener(MODULE_AUTH_MSG);
    g_cb = NULL;
}

static int32_t TransPostAuthChannelMsg(const AppInfo *appInfo, int32_t authId, int32_t flag)
{
    if (appInfo == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "TransPostAuthChannelMsg invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    cJSON *msg = cJSON_CreateObject();
    if (msg == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "json failed");
        return SOFTBUS_MALLOC_ERR;
    }
    if (TransAuthChannelMsgPack(msg, appInfo) != SOFTBUS_OK) {
        cJSON_Delete(msg);
        return SOFTBUS_ERR;
    }
    char *data = cJSON_PrintUnformatted(msg);
    if (data == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "json failed");
        cJSON_Delete(msg);
        return SOFTBUS_PARSE_JSON_ERR;
    }
    cJSON_Delete(msg);

    AuthChannelData channelData = {
        .module = MODULE_AUTH_CHANNEL,
        .flag = flag,
        .seq = 0,
        .len = strlen(data) + 1,
        .data = (const uint8_t *)data,
    };
    if (AuthPostChannelData(authId, &channelData) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth post channel data fail");
        cJSON_free(data);
        return SOFTBUS_ERR;
    }
    cJSON_free(data);
    return SOFTBUS_OK;
}

static void TransPostAuthChannelErrMsg(int32_t authId, int32_t errcode, const char *errMsg)
{
    if (errMsg == NULL) {
        return;
    }
    char cJsonStr[ERR_MSG_MAX_LEN] = {0};
    int32_t ret = TransAuthChannelErrorPack(errcode, errMsg, cJsonStr, ERR_MSG_MAX_LEN);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "TransAuthChannelErrorPack failed");
        return;
    }
    AuthChannelData channelData = {
        .module = MODULE_AUTH_CHANNEL,
        .flag = AUTH_CHANNEL_REPLY,
        .seq = 0,
        .len = strlen(cJsonStr) + 1,
        .data = (const uint8_t *)cJsonStr,
    };
    if (AuthPostChannelData(authId, &channelData) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth post channel data fail");
    }
}

static AuthChannelInfo *CreateAuthChannelInfo(const char *sessionName, bool isClient)
{
    AuthChannelInfo *info = (AuthChannelInfo *)SoftBusCalloc(sizeof(AuthChannelInfo));
    if (info == NULL) {
        return NULL;
    }
    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        goto EXIT_ERR;
    }
    info->appInfo.myData.channelId = GenerateChannelId(true);
    SoftBusMutexUnlock(&g_authChannelList->lock);
    if (GetAppInfo(sessionName, info->appInfo.myData.channelId, &info->appInfo, isClient) != SOFTBUS_OK) {
        goto EXIT_ERR;
    }
    info->isClient = isClient;
    return info;
EXIT_ERR:
    SoftBusFree(info);
    return NULL;
}

int32_t TransOpenAuthMsgChannel(const char *sessionName, const ConnectOption *connOpt,
    int32_t *channelId, const char *reqId)
{
    if (connOpt == NULL || channelId == NULL || connOpt->type != CONNECT_TCP) {
        return SOFTBUS_INVALID_PARAM;
    }
    AuthChannelInfo *channel = CreateAuthChannelInfo(sessionName, true);
    if (channel == NULL) {
        return SOFTBUS_ERR;
    }
    if (strcpy_s(channel->appInfo.reqId, REQ_ID_SIZE_MAX, reqId) != EOK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "TransOpenAuthMsgChannel strcpy_s reqId failed");
        return SOFTBUS_ERR;
    }
    if (memcpy_s(&channel->connOpt, sizeof(ConnectOption), connOpt, sizeof(ConnectOption)) != EOK) {
        SoftBusFree(channel);
        return SOFTBUS_MEM_ERR;
    }
    *channelId = channel->appInfo.myData.channelId;
    int32_t authId = AuthOpenChannel(connOpt->socketOption.addr, connOpt->socketOption.port);
    if (authId < 0) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "AuthOpenChannel failed");
        SoftBusFree(channel);
        return SOFTBUS_ERR;
    }
    channel->authId = authId;
    if (AddAuthChannelInfo(channel) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "AddAuthChannelInfo failed");
        AuthCloseChannel(channel->authId);
        SoftBusFree(channel);
        return SOFTBUS_ERR;
    }
    if (TransPostAuthChannelMsg(&channel->appInfo, authId, AUTH_CHANNEL_REQ) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "TransPostAuthRequest failed");
        AuthCloseChannel(channel->authId);
        DelAuthChannelInfoByChanId(*channelId);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t TransCloseAuthChannel(int32_t channelId)
{
    AuthChannelInfo *channel = NULL;
    AuthChannelInfo *tmp = NULL;
    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return SOFTBUS_LOCK_ERR;
    }
    LIST_FOR_EACH_ENTRY_SAFE(channel, tmp, &g_authChannelList->list, AuthChannelInfo, node) {
        if (channel->appInfo.myData.channelId != channelId) {
            continue;
        }
        ListDelete(&channel->node);
        g_authChannelList->cnt--;
        AuthCloseChannel(channel->authId);
        NofifyCloseAuthChannel(channel->appInfo.myData.pkgName, channel->appInfo.myData.pid, channelId);
        SoftBusFree(channel);
        SoftBusMutexUnlock(&g_authChannelList->lock);
        return SOFTBUS_OK;
    }
    SoftBusMutexUnlock(&g_authChannelList->lock);
    return SOFTBUS_ERR;
}

int32_t TransSendAuthMsg(int32_t channelId, const char *msg, int32_t len)
{
    if (msg == NULL || len <= 0) {
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t authId = GetAuthIdByChannelId(channelId);
    if (authId < 0) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "Get AuthId failed");
        return SOFTBUS_TRANS_AUTH_CHANNEL_NOT_FOUND;
    }

    AuthChannelData channelData = {
        .module = MODULE_AUTH_MSG,
        .flag = 0,
        .seq = 0,
        .len = (uint32_t)len,
        .data = (const uint8_t *)msg,
    };
    if (AuthPostChannelData(authId, &channelData) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth post channel data fail");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t TransAuthGetConnOptionByChanId(int32_t channelId, ConnectOption *connOpt)
{
    AuthChannelInfo chanInfo;
    if (GetAuthChannelInfoByChanId(channelId, &chanInfo) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "get auth channel info by chanid=%d fail", channelId);
        return SOFTBUS_ERR;
    }

    if (!chanInfo.isClient) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth channel of conn opt invalid");
        return SOFTBUS_ERR;
    }

    if (memcpy_s(connOpt, sizeof(ConnectOption), &(chanInfo.connOpt), sizeof(ConnectOption)) != EOK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth channel connopt memcpy fail");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t TransNotifyAuthDataSuccess(int32_t channelId, const ConnectOption *connOpt)
{
    if (connOpt == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "%s invalid param.", __func__);
        return SOFTBUS_ERR;
    }
    ConnectionAddr addr;
    (void)memset_s(&addr, sizeof(ConnectionAddr), 0, sizeof(ConnectionAddr));
    if (!LnnConvertOptionToAddr(&addr, connOpt, CONNECTION_ADDR_WLAN)) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "channel=%d convert addr fail.", channelId);
        return SOFTBUS_ERR;
    }
    return LnnNotifyDiscoveryDevice(&addr, true);
}

int32_t TransAuthGetAppInfoByChanId(int32_t channelId, AppInfo *appInfo)
{
    if (appInfo == NULL || g_authChannelList == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }

    if (SoftBusMutexLock(&g_authChannelList->lock) != 0) {
        return SOFTBUS_LOCK_ERR;
    }
    AuthChannelInfo *info = NULL;
    LIST_FOR_EACH_ENTRY(info, &g_authChannelList->list, AuthChannelInfo, node) {
        if (info->appInfo.myData.channelId == channelId) {
            if (memcpy_s(appInfo, sizeof(AppInfo), &info->appInfo, sizeof(AppInfo)) != EOK) {
                (void)SoftBusMutexUnlock(&g_authChannelList->lock);
                SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "auth channel appinfo memcpy fail");
                return SOFTBUS_ERR;
            }
            (void)SoftBusMutexUnlock(&g_authChannelList->lock);
            return SOFTBUS_OK;
        }
    }
    SoftBusMutexUnlock(&g_authChannelList->lock);
    return SOFTBUS_ERR;
}
