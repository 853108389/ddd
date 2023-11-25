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

#include "client_trans_session_service.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>

#include "anonymizer.h"
#include "client_qos_manager.h"
#include "client_trans_channel_manager.h"
#include "client_trans_file_listener.h"
#include "client_trans_session_adapter.h"
#include "client_trans_session_manager.h"
#include "dfs_session.h"
#include "inner_session.h"
#include "securec.h"
#include "softbus_adapter_mem.h"
#include "softbus_adapter_timer.h"
#include "softbus_client_frame_manager.h"
#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_feature_config.h"
#include "softbus_json_utils.h"
#include "softbus_trans_def.h"
#include "softbus_utils.h"
#include "trans_log.h"
#include "trans_server_proxy.h"

typedef int (*SessionOptionRead)(int32_t channelId, int32_t type, void* value, uint32_t valueSize);
typedef int (*SessionOptionWrite)(int32_t channelId, int32_t type, void* value, uint32_t valueSize);

typedef struct {
    bool canRead;
    SessionOptionRead readFunc;
} SessionOptionItem;

typedef struct {
    int32_t channelType;
    int32_t businessType;
    ConfigType configType;
} ConfigTypeMap;

static bool IsValidSessionId(int sessionId)
{
    if (sessionId <= 0) {
        TRANS_LOGE(TRANS_SDK, "invalid sessionId=%d", sessionId);
        return false;
    }
    return true;
}

static bool IsValidListener(const ISessionListener *listener)
{
    if ((listener != NULL) &&
        (listener->OnSessionOpened != NULL) &&
        (listener->OnSessionClosed != NULL)) {
        return true;
    }
    TRANS_LOGE(TRANS_SDK, "invalid ISessionListener");
    return false;
}

static int32_t OpenSessionWithExistSession(int32_t sessionId, bool isEnabled)
{
    if (!isEnabled) {
        TRANS_LOGI(TRANS_SDK, "the channel is opening");
        return sessionId;
    }

    ISessionListener listener = {0};
    if (ClientGetSessionCallbackById(sessionId, &listener) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get session listener failed");
        CloseSession(sessionId);
        return INVALID_SESSION_ID;
    }

    if (listener.OnSessionOpened(sessionId, SOFTBUS_OK) != 0) {
        TRANS_LOGE(TRANS_SDK, "session callback OnSessionOpened failed");
        CloseSession(sessionId);
        return INVALID_SESSION_ID;
    }
    return sessionId;
}

int CreateSessionServer(const char *pkgName, const char *sessionName, const ISessionListener *listener)
{
    if (!IsValidString(pkgName, PKG_NAME_SIZE_MAX - 1) || !IsValidString(sessionName, SESSION_NAME_SIZE_MAX - 1) ||
        !IsValidListener(listener)) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    char *tmpName = NULL;
    Anonymize(sessionName, &tmpName);
    TRANS_LOGI(TRANS_SDK, "pkgName=%s, sessionName=%s",
        pkgName, tmpName);
    AnonymizeFree(tmpName);
    if (InitSoftBus(pkgName) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "init softbus err");
        return SOFTBUS_TRANS_SESSION_ADDPKG_FAILED;
    }

    if (CheckPackageName(pkgName) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "invalid pkg name");
        return SOFTBUS_INVALID_PKGNAME;
    }

    int ret = ClientAddSessionServer(SEC_TYPE_CIPHERTEXT, pkgName, sessionName, listener);
    if (ret == SOFTBUS_SERVER_NAME_REPEATED) {
        TRANS_LOGI(TRANS_SDK, "SessionServer is already created in client");
    } else if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "add session server err, ret=%d.", ret);
        return ret;
    }

    ret = ServerIpcCreateSessionServer(pkgName, sessionName);
    if (ret == SOFTBUS_SERVER_NAME_REPEATED) {
        TRANS_LOGI(TRANS_SDK, "SessionServer is already created in server");
        ret = SOFTBUS_OK;
    } else if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "Server createSessionServer failed");
        (void)ClientDeleteSessionServer(SEC_TYPE_CIPHERTEXT, sessionName);
    }
    TRANS_LOGI(TRANS_SDK, "ok: ret=%d", ret);
    return ret;
}

int RemoveSessionServer(const char *pkgName, const char *sessionName)
{
    if (!IsValidString(pkgName, PKG_NAME_SIZE_MAX - 1) || !IsValidString(sessionName, SESSION_NAME_SIZE_MAX - 1)) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    char *tmpName = NULL;
    Anonymize(sessionName, &tmpName);
    TRANS_LOGW(TRANS_SDK, "pkgName=%s, sessionName=%s",
        pkgName, tmpName);

    int32_t ret = ServerIpcRemoveSessionServer(pkgName, sessionName);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "remove in server failed, ret=%d.", ret);
        AnonymizeFree(tmpName);
        return ret;
    }

    ret = ClientDeleteSessionServer(SEC_TYPE_CIPHERTEXT, sessionName);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "delete sessionName=%s failed, ret=%d.", tmpName, ret);
        DeleteFileListener(sessionName);
        AnonymizeFree(tmpName);
        return ret;
    }
    DeleteFileListener(sessionName);
    AnonymizeFree(tmpName);
    TRANS_LOGI(TRANS_SDK, "ok: ret=%d", ret);
    return ret;
}

static int32_t CheckParamIsValid(const char *mySessionName, const char *peerSessionName,
    const char *peerNetworkId, const char *groupId, const SessionAttribute *attr)
{
    if (!IsValidString(mySessionName, SESSION_NAME_SIZE_MAX) ||
        !IsValidString(peerSessionName, SESSION_NAME_SIZE_MAX) ||
        !IsValidString(peerNetworkId, DEVICE_ID_SIZE_MAX) ||
        (attr == NULL) ||
        (attr->dataType >= TYPE_BUTT)) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }

    if (groupId == NULL || strlen(groupId) >= GROUP_ID_SIZE_MAX) {
        return SOFTBUS_INVALID_PARAM;
    }

    return SOFTBUS_OK;
}

static void PrintSessionName(const char *mySessionName, const char *peerSessionName)
{
    char *tmpMyName = NULL;
    char *tmpPeerName = NULL;
    Anonymize(mySessionName, &tmpMyName);
    Anonymize(peerSessionName, &tmpPeerName);
    TRANS_LOGI(TRANS_SDK, "OpenSession: mySessionName=%s, peerSessionName=%s",
        tmpMyName, tmpPeerName);
    AnonymizeFree(tmpMyName);
    AnonymizeFree(tmpPeerName);
}

int OpenSession(const char *mySessionName, const char *peerSessionName, const char *peerNetworkId,
    const char *groupId, const SessionAttribute *attr)
{
    int ret = CheckParamIsValid(mySessionName, peerSessionName, peerNetworkId, groupId, attr);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGW(TRANS_SDK, "invalid param, CheckParamIsValid ret=%d.", ret);
        return SOFTBUS_INVALID_PARAM;
    }
    PrintSessionName(mySessionName, peerSessionName);
    TransInfo transInfo;
    SessionAttribute *tmpAttr = (SessionAttribute *)SoftBusCalloc(sizeof(SessionAttribute));
    if (tmpAttr == NULL) {
        TRANS_LOGE(TRANS_SDK, "SoftBusCalloc SessionAttribute failed");
        return SOFTBUS_ERR;
    }
    if (memcpy_s(tmpAttr, sizeof(SessionAttribute), attr, sizeof(SessionAttribute)) != EOK) {
        TRANS_LOGE(TRANS_SDK, "memcpy_s SessionAttribute failed");
        SoftBusFree(tmpAttr);
        return SOFTBUS_ERR;
    }
    tmpAttr->fastTransData = NULL;
    tmpAttr->fastTransDataSize = 0;
    SessionParam param = {
        .sessionName = mySessionName,
        .peerSessionName = peerSessionName,
        .peerDeviceId = peerNetworkId,
        .groupId = groupId,
        .attr = tmpAttr,
        .isQosLane = false,
    };

    int32_t sessionId = INVALID_SESSION_ID;
    bool isEnabled = false;

    ret = ClientAddSession(&param, &sessionId, &isEnabled);
    if (ret != SOFTBUS_OK) {
        SoftBusFree(tmpAttr);
        if (ret == SOFTBUS_TRANS_SESSION_REPEATED) {
            TRANS_LOGI(TRANS_SDK, "session already opened");
            return OpenSessionWithExistSession(sessionId, isEnabled);
        }
        TRANS_LOGE(TRANS_SDK, "add session err: ret=%d", ret);
        return ret;
    }

    ret = ServerIpcOpenSession(&param, &transInfo);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "open session ipc err: ret=%d", ret);
        SoftBusFree(tmpAttr);
        (void)ClientDeleteSession(sessionId);
        return ret;
    }

    ret = ClientSetChannelBySessionId(sessionId, &transInfo);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "open session failed");
        SoftBusFree(tmpAttr);
        (void)ClientDeleteSession(sessionId);
        return INVALID_SESSION_ID;
    }
    TRANS_LOGI(TRANS_SDK, "ok: sessionId=%d, channelId=%d, channelType=%d",
        sessionId, transInfo.channelId, transInfo.channelType);
    SoftBusFree(tmpAttr);
    return sessionId;
}

static int32_t ConvertAddrStr(const char *addrStr, ConnectionAddr *addrInfo)
{
    if (addrStr == NULL || addrInfo == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }

    cJSON *obj = cJSON_Parse(addrStr);
    if (obj == NULL) {
        return SOFTBUS_PARSE_JSON_ERR;
    }
    if (memset_s(addrInfo, sizeof(ConnectionAddr), 0x0, sizeof(ConnectionAddr)) != EOK) {
        cJSON_Delete(obj);
        return SOFTBUS_MEM_ERR;
    }
    int port;
    if (GetJsonObjectStringItem(obj, "ETH_IP", addrInfo->info.ip.ip, IP_STR_MAX_LEN) &&
        GetJsonObjectNumberItem(obj, "ETH_PORT", &port)) {
        addrInfo->info.ip.port = (uint16_t)port;
        if (IsValidString(addrInfo->info.ip.ip, IP_STR_MAX_LEN) && addrInfo->info.ip.port > 0) {
            cJSON_Delete(obj);
            addrInfo->type = CONNECTION_ADDR_ETH;
            return SOFTBUS_OK;
        }
    }
    if (GetJsonObjectStringItem(obj, "WIFI_IP", addrInfo->info.ip.ip, IP_STR_MAX_LEN) &&
        GetJsonObjectNumberItem(obj, "WIFI_PORT", &port)) {
        addrInfo->info.ip.port = (uint16_t)port;
        if (IsValidString(addrInfo->info.ip.ip, IP_STR_MAX_LEN) && addrInfo->info.ip.port > 0) {
            cJSON_Delete(obj);
            addrInfo->type = CONNECTION_ADDR_WLAN;
            return SOFTBUS_OK;
        }
    }
    if (GetJsonObjectStringItem(obj, "BR_MAC", addrInfo->info.br.brMac, BT_MAC_LEN)) {
        cJSON_Delete(obj);
        addrInfo->type = CONNECTION_ADDR_BR;
        return SOFTBUS_OK;
    }
    if (GetJsonObjectStringItem(obj, "BLE_MAC", addrInfo->info.ble.bleMac, BT_MAC_LEN)) {
        cJSON_Delete(obj);
        addrInfo->type = CONNECTION_ADDR_BLE;
        return SOFTBUS_OK;
    }
    cJSON_Delete(obj);
    return SOFTBUS_ERR;
}

static int IsValidAddrInfoArr(const ConnectionAddr *addrInfo, int num)
{
    int32_t addrIndex = -1;
    if (addrInfo == NULL || num <= 0) {
        return addrIndex;
    }
    int32_t wifiIndex = -1;
    int32_t brIndex = -1;
    int32_t bleIndex = -1;
    for (int32_t index = 0; index < num; index++) {
        if ((addrInfo[index].type == CONNECTION_ADDR_ETH || addrInfo[index].type == CONNECTION_ADDR_WLAN) &&
            wifiIndex < 0) {
            wifiIndex = index;
        }
        if (addrInfo[index].type == CONNECTION_ADDR_BR && brIndex < 0) {
            brIndex = index;
        }
        if (addrInfo[index].type == CONNECTION_ADDR_BLE && bleIndex < 0) {
            bleIndex = index;
        }
    }
    addrIndex = (wifiIndex >= 0) ? wifiIndex : addrIndex;
    addrIndex = (addrIndex < 0) ? brIndex : addrIndex;
    addrIndex = (addrIndex < 0) ? bleIndex : addrIndex;
    return addrIndex;
}

int OpenAuthSession(const char *sessionName, const ConnectionAddr *addrInfo, int num, const char *mixAddr)
{
    if (!IsValidString(sessionName, SESSION_NAME_SIZE_MAX - 1)) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    TransInfo transInfo;
    int32_t addrIndex = IsValidAddrInfoArr(addrInfo, num);
    ConnectionAddr *addr = NULL;
    ConnectionAddr mix;
    if (addrIndex < 0) {
        if (ConvertAddrStr(mixAddr, &mix) != SOFTBUS_OK) {
            TRANS_LOGE(TRANS_SDK, "invalid addrInfo param");
            return SOFTBUS_INVALID_PARAM;
        }
        addr = &mix;
    } else {
        addr = (ConnectionAddr *)&addrInfo[addrIndex];
    }
    char *tmpName = NULL;
    Anonymize(sessionName, &tmpName);
    TRANS_LOGI(TRANS_SDK, "sessionName=%s", tmpName);
    AnonymizeFree(tmpName);
    int32_t sessionId;
    int32_t ret = ClientAddAuthSession(sessionName, &sessionId);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "add non encrypt session err: ret=%d", ret);
        return ret;
    }

    transInfo.channelId = ServerIpcOpenAuthSession(sessionName, addr);
    if (addr->type == CONNECTION_ADDR_BR || addr->type == CONNECTION_ADDR_BLE) {
        transInfo.channelType = CHANNEL_TYPE_PROXY;
    } else {
        transInfo.channelType = CHANNEL_TYPE_AUTH;
    }
    ret = ClientSetChannelBySessionId(sessionId, &transInfo);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "failed");
        (void)ClientDeleteSession(sessionId);
        return INVALID_SESSION_ID;
    }
    TRANS_LOGI(TRANS_SDK, "ok: sessionId=%d, channelId=%d, channelType=%d",
        sessionId, transInfo.channelId, transInfo.channelType);
    return sessionId;
}

void NotifyAuthSuccess(int sessionId)
{
    int32_t channelId = -1;
    int32_t channelType = -1;
    TRANS_LOGI(TRANS_SDK, "sessionId=%d", sessionId);
    int32_t ret = ClientGetChannelBySessionId(sessionId, &channelId, &channelType, NULL);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get sessionId=%d channel err, ret:%d.", sessionId, ret);
        return;
    }

    int isServer = 0;
    if (ClientGetSessionIntegerDataById(sessionId, &isServer, KEY_IS_SERVER) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get isServer failed");
        return;
    }
    if (isServer) {
        TRANS_LOGE(TRANS_SDK, "device is service side, no notification");
        return;
    }
    TRANS_LOGI(TRANS_SDK, "device is client side");

    if (ServerIpcNotifyAuthSuccess(channelId, channelType) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK,
            "ServerIpcNotifyAuthSuccess err channelId=%d, channeltype=%d", channelId, channelType);
        return;
    }
}

static int32_t CheckSessionIsOpened(int32_t sessionId)
{
#define SESSION_STATUS_CHECK_MAX_NUM 100
#define SESSION_CHECK_PERIOD 50000
    int32_t i = 0;
    bool isEnable = false;

    while (i < SESSION_STATUS_CHECK_MAX_NUM) {
        if (ClientGetChannelBySessionId(sessionId, NULL, NULL, &isEnable) != SOFTBUS_OK) {
            return SOFTBUS_NOT_FIND;
        }
        if (isEnable == true) {
            TRANS_LOGI(TRANS_SDK, "session is enable");
            return SOFTBUS_OK;
        }
        usleep(SESSION_CHECK_PERIOD);
        i++;
    }

    TRANS_LOGE(TRANS_SDK, "session open timeout");
    return SOFTBUS_ERR;
}

int OpenSessionSync(const char *mySessionName, const char *peerSessionName, const char *peerNetworkId,
    const char *groupId, const SessionAttribute *attr)
{
    int ret = CheckParamIsValid(mySessionName, peerSessionName, peerNetworkId, groupId, attr);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return INVALID_SESSION_ID;
    }
    PrintSessionName(mySessionName, peerSessionName);

    TransInfo transInfo;
    SessionParam param = {
        .sessionName = mySessionName,
        .peerSessionName = peerSessionName,
        .peerDeviceId = peerNetworkId,
        .groupId = groupId,
        .attr = attr,
    };

    int32_t sessionId = INVALID_SESSION_ID;
    bool isEnabled = false;

    ret = ClientAddSession(&param, &sessionId, &isEnabled);
    if (ret != SOFTBUS_OK) {
        if (ret == SOFTBUS_TRANS_SESSION_REPEATED) {
            TRANS_LOGI(TRANS_SDK, "session already opened");
            CheckSessionIsOpened(sessionId);
            return OpenSessionWithExistSession(sessionId, isEnabled);
        }
        TRANS_LOGE(TRANS_SDK, "add session err: ret=%d", ret);
        return ret;
    }

    ret = ServerIpcOpenSession(&param, &transInfo);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "open session ipc err: ret=%d", ret);
        (void)ClientDeleteSession(sessionId);
        return ret;
    }
    ret = ClientSetChannelBySessionId(sessionId, &transInfo);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "server open session err: ret=%d", ret);
        (void)ClientDeleteSession(sessionId);
        return SOFTBUS_TRANS_SESSION_SET_CHANNEL_FAILED;
    }

    ret = CheckSessionIsOpened(sessionId);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "CheckSessionIsOpened err: ret=%d", ret);
        (void)ClientDeleteSession(sessionId);
        return SOFTBUS_TRANS_SESSION_NO_ENABLE;
    }
    TRANS_LOGI(TRANS_SDK, "ok: sessionId=%d, channelId=%d",
        sessionId, transInfo.channelId);
    return sessionId;
}

void CloseSession(int sessionId)
{
    TRANS_LOGI(TRANS_SDK, "sessionId=%d", sessionId);
    int32_t channelId = INVALID_CHANNEL_ID;
    int32_t type = CHANNEL_TYPE_BUTT;
    int32_t ret;

    if (!IsValidSessionId(sessionId)) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return;
    }
    ret = ClientGetChannelBySessionId(sessionId, &channelId, &type, NULL);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get channel err: ret=%d", ret);
        return;
    }
    ret = ClientTransCloseChannel(channelId, type);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "close channel err: ret=%d, channelId=%d, channeType=%d",
            ret, channelId, type);
    }
    ret = ClientDeleteSession(sessionId);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "delete session err: ret=%d", ret);
        return;
    }
    TRANS_LOGD(TRANS_SDK, "ok");
}

int GetMySessionName(int sessionId, char *sessionName, unsigned int len)
{
    if (!IsValidSessionId(sessionId) || (sessionName == NULL) || (len > SESSION_NAME_SIZE_MAX)) {
        return SOFTBUS_INVALID_PARAM;
    }

    return ClientGetSessionDataById(sessionId, sessionName, len, KEY_SESSION_NAME);
}

int GetPeerSessionName(int sessionId, char *sessionName, unsigned int len)
{
    if (!IsValidSessionId(sessionId) || (sessionName == NULL) || (len > SESSION_NAME_SIZE_MAX)) {
        return SOFTBUS_INVALID_PARAM;
    }

    return ClientGetSessionDataById(sessionId, sessionName, len, KEY_PEER_SESSION_NAME);
}

int GetPeerDeviceId(int sessionId, char *networkId, unsigned int len)
{
    if (!IsValidSessionId(sessionId) || (networkId  == NULL) || (len > SESSION_NAME_SIZE_MAX)) {
        return SOFTBUS_INVALID_PARAM;
    }

    return ClientGetSessionDataById(sessionId, networkId, len, KEY_PEER_DEVICE_ID);
}

int GetSessionSide(int sessionId)
{
    return ClientGetSessionSide(sessionId);
}

static bool IsValidFileReceivePath(const char *rootDir)
{
    if (!IsValidString(rootDir, FILE_RECV_ROOT_DIR_SIZE_MAX)) {
        TRANS_LOGE(TRANS_SDK, "recvPath=%s invalid.", rootDir);
        return false;
    }
    char *absPath = realpath(rootDir, NULL);
    if (absPath == NULL) {
        TRANS_LOGE(TRANS_SDK, "recvPath=%s not exist, errno=%d.", rootDir, errno);
        return false;
    }
    SoftBusFree(absPath);
    return true;
}

int SetFileReceiveListener(const char *pkgName, const char *sessionName,
    const IFileReceiveListener *recvListener, const char *rootDir)
{
    if (!IsValidString(pkgName, PKG_NAME_SIZE_MAX - 1) || !IsValidString(sessionName, SESSION_NAME_SIZE_MAX - 1) ||
        !IsValidFileReceivePath(rootDir) || (recvListener == NULL)) {
        TRANS_LOGW(TRANS_SDK, "set file receive listener invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    if (InitSoftBus(pkgName) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "set file receive listener init softbus client error");
        return SOFTBUS_TRANS_SESSION_ADDPKG_FAILED;
    }
    return TransSetFileReceiveListener(sessionName, recvListener, rootDir);
}

int SetFileSendListener(const char *pkgName, const char *sessionName, const IFileSendListener *sendListener)
{
    if (!IsValidString(pkgName, PKG_NAME_SIZE_MAX - 1) || !IsValidString(sessionName, SESSION_NAME_SIZE_MAX - 1) ||
        sendListener == NULL) {
        TRANS_LOGW(TRANS_SDK, "set file send listener invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    if (InitSoftBus(pkgName) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "set file send listener init softbus client error");
        return SOFTBUS_TRANS_SESSION_ADDPKG_FAILED;
    }
    return TransSetFileSendListener(sessionName, sendListener);
}

static const char *g_busName = "DistributedFileService";
static const char *g_deviceStatusName = "ohos.msdp.device_status";

static int32_t IsValidDFSSession(int32_t sessionId, int32_t *channelId)
{
    char sessionName[SESSION_NAME_SIZE_MAX] = {0};
    int32_t type;
    if (GetMySessionName(sessionId, sessionName, SESSION_NAME_SIZE_MAX) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get dfs session name failed");
        return SOFTBUS_ERR;
    }
    if (strncmp(sessionName, g_busName, strlen(g_busName)) != 0 &&
        strncmp(sessionName, g_deviceStatusName, strlen(g_deviceStatusName)) != 0) {
        TRANS_LOGE(TRANS_SDK, "invalid dfs session name");
        return SOFTBUS_TRANS_FUNC_NOT_SUPPORT;
    }

    if (ClientGetChannelBySessionId(sessionId, channelId, &type, NULL) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get channel failed");
        return SOFTBUS_ERR;
    }
    if (type != CHANNEL_TYPE_TCP_DIRECT) {
        TRANS_LOGE(TRANS_SDK, "invalid channel type");
        return SOFTBUS_TRANS_FUNC_NOT_SUPPORT;
    }
    return SOFTBUS_OK;
}

int32_t GetSessionKey(int32_t sessionId, char *key, unsigned int len)
{
    int32_t channelId;
    if (!IsValidSessionId(sessionId) || key == NULL || len < SESSION_KEY_LEN) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    if (IsValidDFSSession(sessionId, &channelId) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "invalid dfs session");
        return SOFTBUS_TRANS_FUNC_NOT_SUPPORT;
    }
    return ClientGetSessionKey(channelId, key, len);
}

int32_t GetSessionHandle(int32_t sessionId, int *handle)
{
    int32_t channelId;
    if (!IsValidSessionId(sessionId) || handle == NULL) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    if (IsValidDFSSession(sessionId, &channelId) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "invalid dfs session");
        return SOFTBUS_TRANS_FUNC_NOT_SUPPORT;
    }
    return ClientGetHandle(channelId, handle);
}

int32_t DisableSessionListener(int32_t sessionId)
{
    int32_t channelId;
    if (!IsValidSessionId(sessionId)) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    if (IsValidDFSSession(sessionId, &channelId) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "invalid dfs session");
        return SOFTBUS_TRANS_FUNC_NOT_SUPPORT;
    }
    return ClientDisableSessionListener(channelId);
}

int32_t QosReport(int32_t sessionId, int32_t appType, int32_t quality)
{
    if (quality != QOS_IMPROVE && quality != QOS_RECOVER) {
        TRANS_LOGW(TRANS_SDK, "qos report invalid param");
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t channelId = INVALID_CHANNEL_ID;
    int32_t type = CHANNEL_TYPE_BUTT;
    int32_t ret = ClientGetChannelBySessionId(sessionId, &channelId, &type, NULL);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get channel err, ret=%d.", ret);
        return ret;
    }
    if (ClientGetSessionSide(sessionId) != IS_CLIENT) {
        TRANS_LOGE(TRANS_SDK,
            "qos report sessionId=%d not exist or not client side", sessionId);
        return SOFTBUS_TRANS_INVALID_SESSION_ID;
    }
    if ((ret = ClientQosReport(channelId, type, appType, quality)) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "qos report sessionId=%d failed", sessionId);
    }
    return ret;
}

static const ConfigTypeMap g_configTypeMap[] = {
    {CHANNEL_TYPE_AUTH, BUSINESS_TYPE_BYTE, SOFTBUS_INT_AUTH_MAX_BYTES_LENGTH},
    {CHANNEL_TYPE_AUTH, BUSINESS_TYPE_MESSAGE, SOFTBUS_INT_AUTH_MAX_MESSAGE_LENGTH},
    {CHANNEL_TYPE_PROXY, BUSINESS_TYPE_BYTE, SOFTBUS_INT_PROXY_MAX_BYTES_LENGTH},
    {CHANNEL_TYPE_PROXY, BUSINESS_TYPE_MESSAGE, SOFTBUS_INT_PROXY_MAX_MESSAGE_LENGTH},
    {CHANNEL_TYPE_TCP_DIRECT, BUSINESS_TYPE_BYTE, SOFTBUS_INT_MAX_BYTES_LENGTH},
    {CHANNEL_TYPE_TCP_DIRECT, BUSINESS_TYPE_MESSAGE, SOFTBUS_INT_MAX_MESSAGE_LENGTH},
};

int32_t GetDefaultConfigType(int32_t channelType, int32_t businessType)
{
    const uint32_t nums = sizeof(g_configTypeMap) / sizeof(ConfigTypeMap);
    for (uint32_t i = 0; i < nums; i++) {
        if ((g_configTypeMap[i].channelType == channelType) &&
            (g_configTypeMap[i].businessType == businessType)) {
                return g_configTypeMap[i].configType;
            }
    }
    return SOFTBUS_CONFIG_TYPE_MAX;
}

int ReadMaxSendBytesSize(int32_t channelId, int32_t type, void* value, uint32_t valueSize)
{
    if (valueSize != sizeof(uint32_t)) {
        TRANS_LOGE(TRANS_SDK, "valueSize=%d, not match", valueSize);
        return SOFTBUS_INVALID_PARAM;
    }

    uint32_t dataConfig = INVALID_DATA_CONFIG;
    if (ClientGetDataConfigByChannelId(channelId, type, &dataConfig) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get config failed.");
        return SOFTBUS_GET_CONFIG_VAL_ERR;
    }

    (*(uint32_t*)value) = dataConfig;
    return SOFTBUS_OK;
}

int ReadMaxSendMessageSize(int32_t channelId, int32_t type, void* value, uint32_t valueSize)
{
    if (valueSize != sizeof(uint32_t)) {
        TRANS_LOGE(TRANS_SDK, "valueSize=%d, not match", valueSize);
        return SOFTBUS_INVALID_PARAM;
    }

    uint32_t dataConfig = INVALID_DATA_CONFIG;
    if (ClientGetDataConfigByChannelId(channelId, type, &dataConfig) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get config failed.");
        return SOFTBUS_GET_CONFIG_VAL_ERR;
    }

    (*(uint32_t*)value) = dataConfig;
    return SOFTBUS_OK;
}

int ReadSessionLinkType(int32_t channelId, int32_t type, void* value, uint32_t valueSize)
{
    if (valueSize != sizeof(uint32_t)) {
        TRANS_LOGE(TRANS_SDK, "valueSize=%d, not match", valueSize);
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t routeType = INVALID_ROUTE_TYPE;
    if (ClientGetRouteTypeByChannelId(channelId, type, &routeType) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get link type failed.");
        return SOFTBUS_GET_CONFIG_VAL_ERR;
    }

    (*(int32_t*)value) = routeType;
    return SOFTBUS_OK;
}

static const SessionOptionItem g_SessionOptionArr[SESSION_OPTION_BUTT] = {
    {true, ReadMaxSendBytesSize},
    {true, ReadMaxSendMessageSize},
    {true, ReadSessionLinkType},
};

int GetSessionOption(int sessionId, SessionOption option, void* optionValue, uint32_t valueSize)
{
    if ((option >= SESSION_OPTION_BUTT) || (optionValue == NULL) || (valueSize == 0)) {
        TRANS_LOGW(TRANS_SDK, "invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    if (!g_SessionOptionArr[option].canRead) {
        TRANS_LOGE(TRANS_SDK, "option=%d can not be get", option);
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t channelId = INVALID_CHANNEL_ID;
    int32_t type = CHANNEL_TYPE_BUTT;
    int32_t ret = ClientGetChannelBySessionId(sessionId, &channelId, &type, NULL);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get channel err, ret=%d.", ret);
        return ret;
    }

    return g_SessionOptionArr[option].readFunc(channelId, type, optionValue, valueSize);
}

int CreateSocket(const char *pkgName, const char *sessionName)
{
    if (!IsValidString(pkgName, PKG_NAME_SIZE_MAX - 1) || !IsValidString(sessionName, SESSION_NAME_SIZE_MAX - 1)) {
        TRANS_LOGE(TRANS_SDK, "invalid pkgName or sessionName");
        return SOFTBUS_INVALID_PARAM;
    }

    if (InitSoftBus(pkgName) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "init softbus err");
        return SOFTBUS_TRANS_SESSION_ADDPKG_FAILED;
    }

    if (CheckPackageName(pkgName) != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "invalid pkg name");
        return SOFTBUS_INVALID_PKGNAME;
    }

    int ret = ClientAddSocketServer(SEC_TYPE_CIPHERTEXT, pkgName, sessionName);
    if (ret == SOFTBUS_SERVER_NAME_REPEATED) {
        TRANS_LOGI(TRANS_SDK, "SessionServer is already created in client");
    } else if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "add session server err, ret=%d.", ret);
        return ret;
    }

    ret = ServerIpcCreateSessionServer(pkgName, sessionName);
    if (ret == SOFTBUS_SERVER_NAME_REPEATED) {
        TRANS_LOGI(TRANS_SDK, "SessionServer is already created in server");
        ret = SOFTBUS_OK;
    } else if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "Server createSessionServer failed");
        (void)ClientDeleteSessionServer(SEC_TYPE_CIPHERTEXT, sessionName);
    }
    TRANS_LOGI(TRANS_SDK, "CreateSocket ok: ret=%d", ret);
    return ret;
}

static SessionAttribute *CreateSessionAttributeBySocketInfoTrans(const SocketInfo *info)
{
    SessionAttribute *tmpAttr = (SessionAttribute *)SoftBusCalloc(sizeof(SessionAttribute));
    if (tmpAttr == NULL) {
        TRANS_LOGE(TRANS_SDK, "SoftBusCalloc SessionAttribute failed");
        return NULL;
    }

    tmpAttr->fastTransData = NULL;
    tmpAttr->fastTransDataSize = 0;
    switch (info->dataType) {
        case DATA_TYPE_MESSAGE:
            tmpAttr->dataType = TYPE_MESSAGE;
            break;
        case DATA_TYPE_BYTES:
            tmpAttr->dataType = TYPE_BYTES;
            break;
        case DATA_TYPE_FILE:
            tmpAttr->dataType = TYPE_FILE;
            break;
        case DATA_TYPE_RAW_STREAM:
            tmpAttr->dataType = TYPE_STREAM;
            tmpAttr->attr.streamAttr.streamType = RAW_STREAM;
            break;
        case DATA_TYPE_VIDEO_STREAM:
            tmpAttr->dataType = TYPE_STREAM;
            tmpAttr->attr.streamAttr.streamType = COMMON_VIDEO_STREAM;
            break;
        case DATA_TYPE_AUDIO_STREAM:
            tmpAttr->dataType = TYPE_STREAM;
            tmpAttr->attr.streamAttr.streamType = COMMON_AUDIO_STREAM;
            break;
        case DATA_TYPE_SLICE_STREAM:
            tmpAttr->dataType = TYPE_STREAM;
            tmpAttr->attr.streamAttr.streamType = VIDEO_SLICE_STREAM;
            break;
        default:
            SoftBusFree(tmpAttr);
            tmpAttr = NULL;
            TRANS_LOGE(TRANS_SDK, "invalid dataType:%d", info->dataType);
            break;
    }
    return tmpAttr;
}

int32_t ClientAddSocket(const SocketInfo *info, int32_t *sessionId)
{
    if (info == NULL || sessionId == NULL) {
        TRANS_LOGE(TRANS_SDK, "ClientAddSocket invalid param");
        return SOFTBUS_INVALID_PARAM;
    }

    SessionAttribute *tmpAttr = CreateSessionAttributeBySocketInfoTrans(info);
    if (tmpAttr == NULL) {
        TRANS_LOGE(TRANS_SDK, "Create SessionAttribute failed");
        return SOFTBUS_ERR;
    }

    SessionParam param = {
        .sessionName = info->name,
        .peerSessionName = info->peerName,
        .peerDeviceId = info->peerNetworkId,
        .groupId = "reserved",
        .attr = tmpAttr,
    };

    bool isEnabled = false;
    int32_t ret = ClientAddSocketSession(&param, sessionId, &isEnabled);
    if (ret != SOFTBUS_OK) {
        SoftBusFree(tmpAttr);
        if (ret == SOFTBUS_TRANS_SESSION_REPEATED) {
            TRANS_LOGI(TRANS_SDK, "socket already create");
            return SOFTBUS_OK;
        }
        TRANS_LOGE(TRANS_SDK, "add socket err: ret=%d", ret);
        return ret;
    }
    SoftBusFree(tmpAttr);
    return SOFTBUS_OK;
}

int32_t ClientBind(int32_t socket, const QosTV qos[], uint32_t len, const ISocketListenerAdapt *listener)
{
    if (listener == NULL) {
        TRANS_LOGI(TRANS_SDK, "ClientBind invalid param");
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t ret = ClientSetListenerBySessionId(socket, listener);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGI(TRANS_SDK, "ClientBind set listener failed: %d", ret);
        return ret;
    }

    TransInfo transInfo;
    ret = ClientIpcOpenSession(socket, (QosTV *)qos, len, &transInfo);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGI(TRANS_SDK, "ClientBind open session failed: %d", ret);
        return ret;
    }

    ret = ClientSetChannelBySessionId(socket, &transInfo);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "set channel failed");
        return SOFTBUS_ERR;
    }

    ret = CheckSessionIsOpened(socket);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "CheckSessionIsOpened err: ret=%d", ret);
        return SOFTBUS_TRANS_SESSION_NO_ENABLE;
    }

    ret = ClientSetSocketState(socket, SESSION_ROLE_CLIENT);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "set session role failed: %d", ret);
        return SOFTBUS_ERR;
    }

    TRANS_LOGI(TRANS_SDK, "ClientBind ok: socket=%d, channelId=%d, channelType = %d", socket,
        transInfo.channelId, transInfo.channelType);
    return SOFTBUS_OK;
}

int32_t ClientListen(int32_t socket, const QosTV qos[], uint32_t len, const ISocketListenerAdapt *listener)
{
    if (listener == NULL) {
        TRANS_LOGI(TRANS_SDK, "ClientBind invalid param");
        return SOFTBUS_INVALID_PARAM;
    }

    int32_t ret = ClientSetListenerBySessionId(socket, listener);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGI(TRANS_SDK, "ClientBind set listener failed: %d", ret);
        return ret;
    }

    ret = ClientSetSocketState(socket, SESSION_ROLE_SERVER);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "set session role failed: %d", ret);
        return SOFTBUS_ERR;
    }

    return SOFTBUS_OK;
}

void ClientShutdown(int32_t socket)
{
    if (!IsValidSessionId(socket)) {
        TRANS_LOGE(TRANS_SDK, "invalid param");
        return;
    }

    int32_t channelId = INVALID_CHANNEL_ID;
    int32_t type = CHANNEL_TYPE_BUTT;
    int32_t ret = ClientGetChannelBySessionId(socket, &channelId, &type, NULL);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "get channel err: ret=%d", ret);
        return;
    }

    ret = ClientTransCloseChannel(channelId, type);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGI(TRANS_SDK, "close channel err: ret=%d, channelId=%d, channeType=%d", ret,
            channelId, type);
    }

    ret = ClientDeleteSocketSession(socket);
    if (ret != SOFTBUS_OK) {
        TRANS_LOGE(TRANS_SDK, "ClientShutdown delete socket session server: ret=%d", ret);
    }
    TRANS_LOGI(TRANS_SDK, "ClientShutdown ok: socket=%d", socket);
}