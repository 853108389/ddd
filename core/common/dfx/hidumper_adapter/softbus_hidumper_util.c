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
#include "softbus_hidumper_util.h"

#include <stdio.h>
#include <string.h>
#include <securec.h>
#include <time.h>

#include "auth_interface.h"
#include "cJSON.h"
#include "comm_log.h"
#include "lnn_map.h"
#include "message_handler.h"
#include "softbus_adapter_mem.h"
#include "softbus_adapter_thread.h"
#include "softbus_def.h"
#include "softbus_error_code.h"
#include "softbus_event.h"
#include "stats_event.h"
#include "hisysevent_manager_c.h"

#define BIZ_SCENE_NAME "BIZ_SCENE"
#define BIZ_STAGE_NAME "BIZ_STAGE"
#define STAGE_RES_NAME "STAGE_RES"
#define ONLINE_NUM_NAME "ONLINE_NUM"
#define TIME_CONSUMING_NAME "COST_TIME"
#define BT_FLOW_NAME "BT_FLOW"
#define CALLER_PID_NAME "CALLER_PID"
#define ERROR_CODE_NAME "ERROR_CODE"
#define LINK_TYPE_NAME "LINK_TYPE"
#define MIN_BW_NAME "MIN_BW"
#define METHOD_ID_NAME "METHOD_ID"
#define PERMISSION_NAME "PERMISSION_NAME"
#define SESSION_NAME "SESSION_NAME"
#define AUTH_LINK_TYPE_NAME "AUTH_TYPE"
#define SOCKET_KEY_NAME "SOCKET_NAME"
#define MODULE_NAME_TRANS "trans"
#define MODULE_NAME_CONN "conn"
#define MODULE_NAME_AUTH "auth"

#define QUERY_EVENT_FULL_QUERY_PARAM (-1)
#define MAX_NUM_OF_EVENT_RESULT 100
#define DAY_MINUTE (24 * 60)
#define SEVEN_DAY_MINUTE (7 * DAY_MINUTE)
#define DAY_TIME (24 * 60 * 60 * 1000)
#define MINUTE_TIME (60 * 1000)
#define TIME_THOUSANDS_FACTOR (1000L)
#define WAIT_QUERY_TIME (1000L)
#define RATE_HUNDRED 100
#define MSG_STATISTIC_QUERY_REPORT 0

#define QUERY_RULES_MAX_NUM 10
#define MAX_LENGTH_OF_EVENT_DOMAIN 17
#define MAX_LENGTH_OF_EVENT_NAME 33
#define MAX_LENGTH_OF_SUCCESS_RATE 50

typedef void (*HandleMessageFunc)(SoftBusMessage* msg);

typedef enum {
    SOFTBUS_CONNECTION_STATS_TYPE,
    SOFTBUS_BUSCENTER_STATS_TYPE,
    SOFTBUS_TRANSPORT_STATS_TYPE,
    STATS_UNUSE_BUTT,
} SoftBusStatsType;

typedef struct {
    HiSysEventQueryRule queryRules[QUERY_RULES_MAX_NUM];
    HiSysEventQueryCallback callback;
    int32_t eventSize;
    int32_t dataSize;
} HiSysEventQueryParam;

typedef struct {
    int32_t connFailTotal;
    int32_t connSuccessTotal;
    int32_t linkTypeTotal[CONNECT_TYPE_MAX];
    int32_t linkTypeSuccessTotal[CONNECT_TYPE_MAX];
} ConnStatsInfo;

typedef struct {
    int32_t authFailTotal;
    int32_t authSuccessTotal;
    int32_t authLinkTypeTotal[AUTH_LINK_TYPE_MAX];
    int32_t authLinkTypeSuccessTotal[AUTH_LINK_TYPE_MAX];
    int32_t onlineDevMaxNum;
    int32_t joinLnnNum;
    int32_t leaveLnnNum;
} LnnStatsInfo;

typedef struct {
    int32_t total;
    int32_t successTotal;
} TransStatsSuccessRateDetail;

typedef struct {
    int32_t openSessionFailTotal;
    int32_t openSessionSuccessTotal;
    int32_t delayTimeTotal;
    int32_t delayNum;
    int32_t btFlowTotal;
    int32_t currentParaSessionNum;
    int32_t maxParaSessionNum;
    int32_t laneScoreOverTimes;
    int32_t activityFailTotal;
    int32_t activitySuccessTotal;
    int32_t detectionTimes;
    Map sessionNameLinkTypeMap;
} TransStatsInfo;

static bool g_isDumperInit = false;

static bool g_isConnQueryEnd = false;
static bool g_isLnnQueryEnd = false;
static bool g_isTransQueryEnd = false;
static bool g_isAlarmQueryEnd = false;

static SoftBusMutex g_statsQueryLock = {0};
static SoftBusMutex g_alarmQueryLock = {0};
static SoftBusMutex g_connOnQueryLock = {0};
static SoftBusMutex g_lnnOnQueryLock = {0};
static SoftBusMutex g_transOnQueryLock = {0};
static SoftBusMutex g_alarmOnQueryLock = {0};

static SoftBusMutex g_transMapLock = {0};
static bool g_isTransMapInit = false;
static ConnStatsInfo g_connStatsInfo = {0, 0, {0}, {0}};
static LnnStatsInfo g_lnnStatsInfo = {0, 0, {0}, {0}, 0, 0, 0};
static TransStatsInfo g_transStatsInfo = {0};
static SoftBusAlarmEvtResult g_alarmEvtResult = {0};

static HiSysEventQueryParam g_queryStatsParam[STATS_UNUSE_BUTT];
static HiSysEventQueryParam g_queryAlarmParam[ALARM_UNUSE_BUTT];

static bool TransMapInit(void)
{
    if (SoftBusMutexInit(&g_transMapLock, NULL) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "transMap mutex init fail");
        return false;
    }
    LnnMapInit(&g_transStatsInfo.sessionNameLinkTypeMap);
    g_isTransMapInit = true;
    return true;
}

static int32_t TransMapLock(void)
{
    if (!g_isTransMapInit) {
        if (!TransMapInit()) {
            return SOFTBUS_ERR;
        }
    }
    return SoftBusMutexLock(&g_transMapLock);
}

static void TransMapUnlock(void)
{
    if (!g_isTransMapInit) {
        (void)TransMapInit();
        return;
    }
    (void)SoftBusMutexUnlock(&g_transMapLock);
}

static int32_t GetInt32ValueByRecord(HiSysEventRecordC* record, char* name)
{
    int64_t value;
    int32_t res = OH_HiSysEvent_GetParamInt64Value(record, name, &value);
    if (res != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    return (int32_t)value;
}

static char* GetStringValueByRecord(HiSysEventRecordC* record, char* name)
{
    char* value;
    int32_t res = OH_HiSysEvent_GetParamStringValue(record, name, &value);
    if (res != SOFTBUS_OK) {
        return NULL;
    }
    return value;
}

static void GetLocalTime(char* time, uint64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm* tmInfo = NULL;
    tmInfo = localtime(&t);
    if (tmInfo == NULL) {
        return;
    }
    (void)strftime(time, SOFTBUS_ALARM_TIME_LEN, "%Y-%m-%d %H:%M:%S", tmInfo);
}

static void ConnStatsLinkType(int32_t linkTypePara, bool success)
{
    int32_t linkType = linkTypePara;
    if (linkType < CONNECT_TCP || linkType >= CONNECT_TYPE_MAX) {
        linkType = SOFTBUS_ZERO;
    }
    g_connStatsInfo.linkTypeTotal[linkType]++;
    if (success) {
        g_connStatsInfo.linkTypeSuccessTotal[linkType]++;
    }
}

static void OnQueryConn(HiSysEventRecordC srcRecord[], size_t size)
{
    COMM_LOGI(COMM_DFX, "OnQueryConn start");
    if (SoftBusMutexLock(&g_connOnQueryLock) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "conn query lock fail");
        return;
    }

    for (size_t i = 0; i < size; i++) {
        int32_t scene = GetInt32ValueByRecord(&srcRecord[i], BIZ_SCENE_NAME);
        int32_t stage = GetInt32ValueByRecord(&srcRecord[i], BIZ_STAGE_NAME);
        int32_t stageRes = GetInt32ValueByRecord(&srcRecord[i], STAGE_RES_NAME);
        if (scene == SOFTBUS_ERR || stage == SOFTBUS_ERR || stageRes == SOFTBUS_ERR) {
            continue;
        }
        int32_t linkType = GetInt32ValueByRecord(&srcRecord[i], LINK_TYPE_NAME);
        if (scene == EVENT_SCENE_CONNECT && stage == EVENT_STAGE_CONNECT_END &&
            stageRes == EVENT_STAGE_RESULT_OK) {
            g_connStatsInfo.connSuccessTotal++;
            ConnStatsLinkType(linkType, true);
        }
        if (scene == EVENT_SCENE_CONNECT && stage == EVENT_STAGE_CONNECT_END &&
            stageRes == EVENT_STAGE_RESULT_FAILED) {
            g_connStatsInfo.connFailTotal++;
            ConnStatsLinkType(linkType, false);
        }
    }
    (void)SoftBusMutexUnlock(&g_connOnQueryLock);
}

static void OnCompleteConn(int32_t reason, int32_t total)
{
    COMM_LOGI(COMM_DFX, "OnCompleteConn start, reason is %d, total is %d", reason, total);
    g_isConnQueryEnd = true;
}

static void LnnStatsAuthLinkType(int32_t authLinkTypePara, bool success)
{
    int32_t authLinkType = authLinkTypePara;
    if (authLinkType < AUTH_LINK_TYPE_WIFI || authLinkType >= AUTH_LINK_TYPE_MAX) {
        authLinkType = SOFTBUS_ZERO;
    }
    g_lnnStatsInfo.authLinkTypeTotal[authLinkType]++;
    if (success) {
        g_lnnStatsInfo.authLinkTypeSuccessTotal[authLinkType]++;
    }
}

static void LnnStats(int32_t scene, int32_t stage, int32_t stageRes, int32_t authLinkType)
{
    if (scene == EVENT_SCENE_JOIN_LNN && stage == EVENT_STAGE_AUTH_DEVICE && stageRes == EVENT_STAGE_RESULT_OK) {
        g_lnnStatsInfo.authSuccessTotal++;
        LnnStatsAuthLinkType(authLinkType, true);
        return;
    }
        
    if (scene == EVENT_SCENE_JOIN_LNN && stage == EVENT_STAGE_AUTH_DEVICE && stageRes == EVENT_STAGE_RESULT_FAILED) {
        g_lnnStatsInfo.authFailTotal++;
        LnnStatsAuthLinkType(authLinkType, false);
        return;
    }
        
    if (scene == EVENT_SCENE_JOIN_LNN && stage == EVENT_STAGE_JOIN_LNN_END && stageRes == EVENT_STAGE_RESULT_OK) {
        g_lnnStatsInfo.joinLnnNum++;
        return;
    }

    if (scene == EVENT_SCENE_LEAVE_LNN && stage == EVENT_STAGE_LEAVE_LNN_END && stageRes == EVENT_STAGE_RESULT_OK) {
        g_lnnStatsInfo.leaveLnnNum++;
        return;
    }
}

static void OnQueryLnn(HiSysEventRecordC srcRecord[], size_t size)
{
    COMM_LOGI(COMM_DFX, "OnQueryLnn start");
    if (SoftBusMutexLock(&g_lnnOnQueryLock) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "lnn query lock fail");
        return;
    }

    for (size_t i = 0; i < size; i++) {
        int32_t scene = GetInt32ValueByRecord(&srcRecord[i], BIZ_SCENE_NAME);
        int32_t stage = GetInt32ValueByRecord(&srcRecord[i], BIZ_STAGE_NAME);
        int32_t stageRes = GetInt32ValueByRecord(&srcRecord[i], STAGE_RES_NAME);
        if (scene == SOFTBUS_ERR || stage == SOFTBUS_ERR || stageRes == SOFTBUS_ERR) {
            continue;
        }

        int32_t authLinkType = GetInt32ValueByRecord(&srcRecord[i], AUTH_LINK_TYPE_NAME);
        LnnStats(scene, stage, stageRes, authLinkType);
        int32_t onlineMaxNum = g_lnnStatsInfo.onlineDevMaxNum;
        int32_t onlineNum = GetInt32ValueByRecord(&srcRecord[i], ONLINE_NUM_NAME);
        if (onlineNum != SOFTBUS_ERR) {
            g_lnnStatsInfo.onlineDevMaxNum = (onlineMaxNum > onlineNum) ? onlineMaxNum : onlineNum;
        }
    }
    (void)SoftBusMutexUnlock(&g_lnnOnQueryLock);
}

static void OnCompleteLnn(int32_t reason, int32_t total)
{
    COMM_LOGI(COMM_DFX, "OnCompleteLnn start, reason is %d, total is %d", reason, total);
    g_isLnnQueryEnd = true;
}

static void TransStatsSuccessDetail(bool success, const char *socketName, int32_t linkTypePara)
{
    int linkType = linkTypePara;
    if (linkType < CONNECT_TCP || linkType >= CONNECT_TYPE_MAX) {
        linkType = SOFTBUS_ZERO;
    }

    char keyStr[SESSION_NAME_SIZE_MAX + 3] = {0};
    int32_t ret = sprintf_s(keyStr, sizeof(keyStr), "%s-%d", socketName, linkType);
    if (ret <= 0) {
        COMM_LOGE(COMM_DFX, "generate key fail");
        return;
    }
    if (TransMapLock() != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "lock TransMap fail");
        return;
    }
    TransStatsSuccessRateDetail *data =
        (TransStatsSuccessRateDetail *)LnnMapGet(&g_transStatsInfo.sessionNameLinkTypeMap, keyStr);
    if (data == NULL) {
        TransStatsSuccessRateDetail newResult;
        newResult.successTotal = 0;
        newResult.total = 1;
        if (success) {
            newResult.successTotal = 1;
        }
        if (LnnMapSet(&g_transStatsInfo.sessionNameLinkTypeMap, keyStr, (const void *)&newResult,
            sizeof(TransStatsSuccessRateDetail)) != SOFTBUS_OK) {
            COMM_LOGE(COMM_DFX, "insert keyStr:%s fail", keyStr);
        }
        TransMapUnlock();
        return;
    }
    data->total++;
    if (success) {
        data->successTotal++;
    }
    TransMapUnlock();
}

static void TransStats(int32_t scene, int32_t stage, int32_t stageRes, const char *socketName, int32_t linkType)
{
    if (scene == EVENT_SCENE_OPEN_CHANNEL && stage == EVENT_STAGE_OPEN_CHANNEL_END
        && stageRes == EVENT_STAGE_RESULT_OK) {
        TransStatsSuccessDetail(true, socketName, linkType);
        g_transStatsInfo.openSessionSuccessTotal++;
        g_transStatsInfo.currentParaSessionNum++;
        return;
    }

    if (scene == EVENT_SCENE_OPEN_CHANNEL && stage == EVENT_STAGE_OPEN_CHANNEL_END
        && stageRes == EVENT_STAGE_RESULT_FAILED) {
        TransStatsSuccessDetail(false, socketName, linkType);
        g_transStatsInfo.openSessionFailTotal++;
        return;
    }

    if (scene == EVENT_SCENE_ACTIVATION && stage == SOFTBUS_DEFAULT_STAGE && stageRes == EVENT_STAGE_RESULT_OK) {
        g_transStatsInfo.activitySuccessTotal++;
        return;
    }

    if (scene == EVENT_SCENE_ACTIVATION && stage == SOFTBUS_DEFAULT_STAGE && stageRes == EVENT_STAGE_RESULT_FAILED) {
        g_transStatsInfo.activityFailTotal++;
        return;
    }

    if (scene == EVENT_SCENE_LANE_SCORE && stage == SOFTBUS_DEFAULT_STAGE && stageRes == EVENT_STAGE_RESULT_OK) {
        g_transStatsInfo.laneScoreOverTimes++;
        return;
    }

    if (scene == EVENT_SCENE_DETECTION && stage == SOFTBUS_DEFAULT_STAGE && stageRes == EVENT_STAGE_RESULT_OK) {
        g_transStatsInfo.detectionTimes++;
        return;
    }
}

static void OnQueryTrans(HiSysEventRecordC srcRecord[], size_t size)
{
    COMM_LOGI(COMM_DFX, "OnQueryTrans start");
    if (SoftBusMutexLock(&g_transOnQueryLock) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "trans query lock fail");
        return;
    }

    for (size_t i = 0; i < size; i++) {
        int32_t scene = GetInt32ValueByRecord(&srcRecord[i], BIZ_SCENE_NAME);
        int32_t stage = GetInt32ValueByRecord(&srcRecord[i], BIZ_STAGE_NAME);
        int32_t stageRes = GetInt32ValueByRecord(&srcRecord[i], STAGE_RES_NAME);
        if (scene == SOFTBUS_ERR || stage == SOFTBUS_ERR || stageRes == SOFTBUS_ERR) {
            continue;
        }

        char* socketName = GetStringValueByRecord(&srcRecord[i], SOCKET_KEY_NAME);
        int32_t linkType = GetInt32ValueByRecord(&srcRecord[i], LINK_TYPE_NAME);
        TransStats(scene, stage, stageRes, socketName, linkType);
        cJSON_free(socketName);
        if (scene == EVENT_SCENE_CLOSE_CHANNEL_ACTIVE && stage == EVENT_STAGE_CLOSE_CHANNEL &&
            stageRes == EVENT_STAGE_RESULT_OK && g_transStatsInfo.currentParaSessionNum > 0) {
            g_transStatsInfo.currentParaSessionNum--;
        }
        int32_t maxParaSessionNum = g_transStatsInfo.maxParaSessionNum;
        g_transStatsInfo.maxParaSessionNum = (maxParaSessionNum > g_transStatsInfo.currentParaSessionNum) ?
            maxParaSessionNum : g_transStatsInfo.currentParaSessionNum;

        int32_t timeConsuming = GetInt32ValueByRecord(&srcRecord[i], TIME_CONSUMING_NAME);
        if (timeConsuming != SOFTBUS_ERR && stageRes == EVENT_STAGE_RESULT_OK) {
            g_transStatsInfo.delayTimeTotal += timeConsuming;
            g_transStatsInfo.delayNum++;
        }
        int32_t btFlow = GetInt32ValueByRecord(&srcRecord[i], BT_FLOW_NAME);
        if (btFlow != SOFTBUS_ERR) {
            g_transStatsInfo.btFlowTotal += btFlow;
        }
    }
    (void)SoftBusMutexUnlock(&g_transOnQueryLock);
}

static void OnCompleteTrans(int32_t reason, int32_t total)
{
    COMM_LOGI(COMM_DFX, "OnCompleteTrans start, reason is %d, total is %d", reason, total);
    g_isTransQueryEnd = true;
}

static void OnQueryAlarm(HiSysEventRecordC srcRecord[], size_t size)
{
    COMM_LOGI(COMM_DFX, "OnQueryAlarm start");
    if (SoftBusMutexLock(&g_alarmOnQueryLock) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "alarm query lock fail");
        return;
    }
    g_alarmEvtResult.recordSize = size;

    for (size_t i = 0; i < size; i++) {
        AlarmRecord* record = &g_alarmEvtResult.records[i];
        int32_t scene = GetInt32ValueByRecord(&srcRecord[i], BIZ_SCENE_NAME);
        if (scene != SOFTBUS_ERR) {
            record->type = scene;
        }

        int32_t callerPid = GetInt32ValueByRecord(&srcRecord[i], CALLER_PID_NAME);
        if (callerPid != SOFTBUS_ERR) {
            record->callerPid = callerPid;
        }

        int32_t errorCode = GetInt32ValueByRecord(&srcRecord[i], ERROR_CODE_NAME);
        if (errorCode != SOFTBUS_ERR) {
            record->errorCode = errorCode;
        }

        int32_t linkType = GetInt32ValueByRecord(&srcRecord[i], LINK_TYPE_NAME);
        if (linkType != SOFTBUS_ERR) {
            record->linkType = linkType;
        }

        int32_t minBw = GetInt32ValueByRecord(&srcRecord[i], MIN_BW_NAME);
        if (minBw != SOFTBUS_ERR) {
            record->minBw = minBw;
        }

        int32_t methodId = GetInt32ValueByRecord(&srcRecord[i], METHOD_ID_NAME);
        if (methodId != SOFTBUS_ERR) {
            record->methodId = methodId;
        }

        char* permissionName = GetStringValueByRecord(&srcRecord[i], PERMISSION_NAME);
        if (permissionName != NULL) {
            record->permissionName = permissionName;
        }

        char* sessionName = GetStringValueByRecord(&srcRecord[i], SESSION_NAME);
        if (sessionName != NULL) {
            record->sessionName = sessionName;
        }

        GetLocalTime(record->time, srcRecord[i].time / TIME_THOUSANDS_FACTOR);
    }
    (void)SoftBusMutexUnlock(&g_alarmOnQueryLock);
}

static void OnCompleteAlarm(int32_t reason, int32_t total)
{
    COMM_LOGI(COMM_DFX, "OnCompleteAlarm start, reason is %d, total is %d", reason, total);
    g_isAlarmQueryEnd = true;
}

static void SoftBusEventQueryInfo(int time, HiSysEventQueryParam* queryParam)
{
    HiSysEventQueryArg queryArg;
    queryArg.endTime = SoftBusGetSysTimeMs();
    queryArg.beginTime = queryArg.endTime - time * MINUTE_TIME;
    queryArg.maxEvents = queryParam->dataSize;
    
    int32_t ret = OH_HiSysEvent_Query(&queryArg, queryParam->queryRules, queryParam->eventSize, &queryParam->callback);
    COMM_LOGI(COMM_DFX, "SoftBusHisEvtQuery result, reason is %d", ret);
}

void GenerateTransSuccessRateString(MapIterator *it, char *res, uint64_t maxLen)
{
    char sessionName[SESSION_NAME_SIZE_MAX] = {0};
    const char *key = (const char *)it->node->key;
    char *delimit = strrchr(key, '-');
    if (strncpy_s(sessionName, SESSION_NAME_SIZE_MAX, key, delimit - key) != EOK) {
        COMM_LOGE(COMM_DFX, "sessionName strncpy failed");
        return;
    }
    int32_t linkType = atoi(delimit + 1);
    TransStatsSuccessRateDetail *quantity = (TransStatsSuccessRateDetail *)it->node->value;
    if (quantity == NULL) {
        COMM_LOGE(COMM_DFX, "quantity is nullptr");
        return;
    }
    float rate = 0;
    if (quantity->total > 0) {
        rate = 1.0 * quantity->successTotal / quantity->total * RATE_HUNDRED;
    }
    int32_t ret = sprintf_s(res, maxLen, "%s,%d,%d,%d,%2.2f", sessionName, linkType, quantity->total,
        quantity->successTotal, rate);
    if (ret <= 0) {
        COMM_LOGE(COMM_DFX, "sprintf_s fail");
        return;
    }
}

void FillTransSuccessRateDetail(cJSON *transObj)
{
    if (transObj == NULL) {
        COMM_LOGE(COMM_DFX, "%s json is add to root fail", MODULE_NAME_TRANS);
        return;
    }
    if (TransMapLock() != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "lock TransMap fail");
        return;
    }
    MapIterator *it = LnnMapInitIterator(&g_transStatsInfo.sessionNameLinkTypeMap);
    if (it == NULL) {
        COMM_LOGI(COMM_DFX, "map is empty");
        return;
    }
    while (LnnMapHasNext(it)) {
        it = LnnMapNext(it);
        if (it == NULL || it->node->value == NULL) {
            break;
        }
        char detail[SESSION_NAME_SIZE_MAX + MAX_LENGTH_OF_SUCCESS_RATE] = {0};
        GenerateTransSuccessRateString(it, detail, sizeof(detail));
        cJSON_AddItemToArray(transObj, cJSON_CreateString(detail));
    }
    LnnMapDeinitIterator(it);
    TransMapUnlock();
}

void FillConnSuccessRateDetail(cJSON *connObj)
{
    if (connObj == NULL) {
        COMM_LOGE(COMM_DFX, "%s json is add to root fail", MODULE_NAME_CONN);
        return;
    }
    for (int i = SOFTBUS_ZERO; i < CONNECT_TYPE_MAX; i++) {
        float rate = 0;
        if (g_connStatsInfo.linkTypeTotal[i] > 0) {
            rate = 1.0 * g_connStatsInfo.linkTypeSuccessTotal[i] / g_connStatsInfo.linkTypeTotal[i] * RATE_HUNDRED;
        }
        char detail[MAX_LENGTH_OF_SUCCESS_RATE] = {0};
        int32_t ret = sprintf_s(detail, sizeof(detail), "%d,%d,%d,%2.2f", i, g_connStatsInfo.linkTypeTotal[i],
            g_connStatsInfo.linkTypeSuccessTotal[i], rate);
        if (ret <= 0) {
            COMM_LOGE(COMM_DFX, "sprintf_s fail");
            return;
        }
        cJSON_AddItemToArray(connObj, cJSON_CreateString(detail));
    }
}

void FillAuthSuccessRateDetail(cJSON *authObj)
{
    if (authObj == NULL) {
        COMM_LOGE(COMM_DFX, "%s json is add to root fail", MODULE_NAME_AUTH);
        return;
    }
    for (int i = SOFTBUS_ZERO; i < AUTH_LINK_TYPE_MAX; i++) {
        float rate = 0;
        if (g_lnnStatsInfo.authLinkTypeTotal[i] > 0) {
            rate =
                1.0 * g_lnnStatsInfo.authLinkTypeSuccessTotal[i] / g_lnnStatsInfo.authLinkTypeTotal[i] * RATE_HUNDRED;
        }
        char detail[MAX_LENGTH_OF_SUCCESS_RATE] = {0};
        int32_t ret = sprintf_s(detail, sizeof(detail), "%d,%d,%d,%2.2f", i, g_lnnStatsInfo.authLinkTypeTotal[i],
            g_lnnStatsInfo.authLinkTypeSuccessTotal[i], rate);
        if (ret <= 0) {
            COMM_LOGE(COMM_DFX, "sprintf_s fail");
            return;
        }
        cJSON_AddItemToArray(authObj, cJSON_CreateString(detail));
    }
}

void FillSuccessRateDetail(SoftBusStatsResult *result)
{
    cJSON *root_obj = cJSON_CreateObject();
    if (root_obj == NULL) {
        COMM_LOGE(COMM_DFX, "root_obj is null");
        return;
    }
    cJSON *transObj = cJSON_AddArrayToObject(root_obj, MODULE_NAME_TRANS);
    FillTransSuccessRateDetail(transObj);
    cJSON *connObj = cJSON_AddArrayToObject(root_obj, MODULE_NAME_CONN);
    FillConnSuccessRateDetail(connObj);
    cJSON *authObj = cJSON_AddArrayToObject(root_obj, MODULE_NAME_AUTH);
    FillAuthSuccessRateDetail(authObj);
    result->successRateDetail = cJSON_PrintUnformatted(root_obj);
    cJSON_Delete(root_obj);
}

static void SoftBusProcessStatsQueryData(SoftBusStatsResult* result)
{
    FillSuccessRateDetail(result);
    result->btFlow = g_transStatsInfo.btFlowTotal;
    result->successRate = 0;
    int32_t total = g_transStatsInfo.openSessionSuccessTotal + g_transStatsInfo.openSessionFailTotal;
    if (total != 0) {
        result->successRate = (1.0 * g_transStatsInfo.openSessionSuccessTotal) / total;
    }

    result->sessionSuccessDuration = 0;
    if (g_transStatsInfo.delayNum != 0) {
        result->sessionSuccessDuration = g_transStatsInfo.delayTimeTotal / g_transStatsInfo.delayNum;
    }

    result->activityRate = 0;
    int32_t activityTotal = g_transStatsInfo.activityFailTotal + g_transStatsInfo.activitySuccessTotal;
    if (activityTotal != 0) {
        result->activityRate = (1.0 * g_transStatsInfo.activitySuccessTotal) / activityTotal;
    }

    result->deviceOnlineNum = g_lnnStatsInfo.onlineDevMaxNum;
    result->deviceOnlineTimes = g_lnnStatsInfo.joinLnnNum;
    result->deviceOfflineTimes = g_lnnStatsInfo.leaveLnnNum;
    result->maxParaSessionNum = g_transStatsInfo.maxParaSessionNum;
    result->laneScoreOverTimes = g_transStatsInfo.laneScoreOverTimes;
    result->detectionTimes = g_transStatsInfo.detectionTimes;

    if (memset_s(&g_connStatsInfo, sizeof(g_connStatsInfo), 0, sizeof(g_connStatsInfo)) != EOK) {
        COMM_LOGE(COMM_DFX, "memset g_connStatsInfo fail!");
        return;
    }
    if (memset_s(&g_lnnStatsInfo, sizeof(g_lnnStatsInfo), 0, sizeof(g_lnnStatsInfo)) != EOK) {
        COMM_LOGE(COMM_DFX, "memset g_lnnStatsInfo fail!");
        return;
    }
    if (TransMapLock() != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "lock TransMap fail");
    }
    LnnMapDelete(&g_transStatsInfo.sessionNameLinkTypeMap);
    TransMapUnlock();
    if (memset_s(&g_transStatsInfo, sizeof(g_transStatsInfo), 0, sizeof(g_transStatsInfo)) != EOK) {
        COMM_LOGE(COMM_DFX, "memset g_transStatsInfo fail!");
        return;
    }

    g_isConnQueryEnd = false;
    g_isLnnQueryEnd = false;
    g_isTransQueryEnd = false;
}

static void SoftBusProcessAlarmQueryData(SoftBusAlarmEvtResult* result)
{
    result->recordSize = g_alarmEvtResult.recordSize;
    result->records = g_alarmEvtResult.records;
    g_isAlarmQueryEnd = false;
    return;
}

SoftBusStatsResult* MallocSoftBusStatsResult(unsigned int size)
{
    SoftBusStatsResult *result = (SoftBusStatsResult *)SoftBusMalloc(size);
    if (result != NULL) {
        result->successRateDetail = NULL;
    }
    return result;
}

void FreeSoftBusStatsResult(SoftBusStatsResult* result)
{
    if (result != NULL && result->successRateDetail != NULL) {
        cJSON_free(result->successRateDetail);
        result->successRateDetail = NULL;
    }
    SoftBusFree(result);
}

int32_t SoftBusQueryStatsInfo(int time, SoftBusStatsResult* result)
{
    COMM_LOGI(COMM_DFX, "SoftBusQueryStatsInfo start");
    if (time <= SOFTBUS_ZERO || time > SEVEN_DAY_MINUTE) {
        COMM_LOGE(COMM_DFX, "SoftBusQueryStatsInfo fail, time is %d", time);
        return SOFTBUS_ERR;
    }
    if (SoftBusMutexLock(&g_statsQueryLock) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "lock g_statsQueryLock fail");
        return SOFTBUS_ERR;
    }
    for (int i = 0; i < STATS_UNUSE_BUTT; i++) {
        SoftBusEventQueryInfo(time, &g_queryStatsParam[i]);
    }
    while (!g_isConnQueryEnd || !g_isLnnQueryEnd || !g_isTransQueryEnd) {
        SoftBusSleepMs(WAIT_QUERY_TIME);
    }

    SoftBusProcessStatsQueryData(result);
    (void)SoftBusMutexUnlock(&g_statsQueryLock);
    return SOFTBUS_OK;
}

int32_t SoftBusQueryAlarmInfo(int time, int type, SoftBusAlarmEvtResult* result)
{
    COMM_LOGI(COMM_DFX, "SoftBusQueryAlarmInfo start");
    if (time <= SOFTBUS_ZERO || time > SEVEN_DAY_MINUTE) {
        COMM_LOGE(COMM_DFX, "QueryAlarmInfo fail, time is %d", time);
        return SOFTBUS_ERR;
    }
    if (type < SOFTBUS_MANAGEMENT_ALARM_TYPE || type >= ALARM_UNUSE_BUTT) {
        COMM_LOGE(COMM_DFX, "QueryAlarmInfo fail, type is %d", type);
        return SOFTBUS_ERR;
    }
    if (SoftBusMutexLock(&g_alarmQueryLock) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "QueryAlarmInfo fail,lock fail");
        return SOFTBUS_ERR;
    }
    g_alarmEvtResult.recordSize = 0;
    if (memset_s(g_alarmEvtResult.records, sizeof(AlarmRecord) * MAX_NUM_OF_EVENT_RESULT, 0,
        sizeof(AlarmRecord) * MAX_NUM_OF_EVENT_RESULT) != EOK) {
        COMM_LOGE(COMM_DFX, "memset g_alarmEvtResult records fail!");
        (void)SoftBusMutexUnlock(&g_alarmQueryLock);
        return SOFTBUS_ERR;
    }
    SoftBusEventQueryInfo(time, &g_queryAlarmParam[type]);
    while (!g_isAlarmQueryEnd) {
        SoftBusSleepMs(WAIT_QUERY_TIME);
    }

    SoftBusProcessAlarmQueryData(result);
    (void)SoftBusMutexUnlock(&g_alarmQueryLock);
    return SOFTBUS_OK;
}

static int32_t InitDumperUtilMutexLock(void)
{
    SoftBusMutexAttr mutexAttr = {SOFTBUS_MUTEX_RECURSIVE};
    if (SoftBusMutexInit(&g_statsQueryLock, &mutexAttr) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "init statistic lock fail");
        (void)SoftBusMutexDestroy(&g_statsQueryLock);
        return SOFTBUS_ERR;
    }
    
    if (SoftBusMutexInit(&g_alarmQueryLock, &mutexAttr) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "init alarm lock fail");
        (void)SoftBusMutexDestroy(&g_alarmQueryLock);
        return SOFTBUS_ERR;
    }
    
    if (SoftBusMutexInit(&g_connOnQueryLock, &mutexAttr) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "init conn onQuery lock fail");
        (void)SoftBusMutexDestroy(&g_connOnQueryLock);
        return SOFTBUS_ERR;
    }
    
    if (SoftBusMutexInit(&g_lnnOnQueryLock, &mutexAttr) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "init lnn onQuery lock fail");
        (void)SoftBusMutexDestroy(&g_lnnOnQueryLock);
        return SOFTBUS_ERR;
    }
    
    if (SoftBusMutexInit(&g_transOnQueryLock, &mutexAttr) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "init trans onQuery lock fail");
        (void)SoftBusMutexDestroy(&g_transOnQueryLock);
        return SOFTBUS_ERR;
    }

    if (SoftBusMutexInit(&g_alarmOnQueryLock, &mutexAttr) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "init alarm onQuery lock fail");
        (void)SoftBusMutexDestroy(&g_alarmOnQueryLock);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

static void UpdateSysEventQueryParam(HiSysEventQueryParam* param, char* eventName)
{
    HiSysEventQueryRule* queryRule = &param->queryRules[SOFTBUS_ZERO];
    if (strcpy_s(queryRule->domain, MAX_LENGTH_OF_EVENT_DOMAIN, SOFTBUS_EVENT_DOMAIN) != EOK) {
        COMM_LOGE(COMM_DFX, "UpdateSysEventQueryParam  copy domain fail");
    }
    if (strcpy_s(queryRule->eventList[SOFTBUS_ZERO], MAX_LENGTH_OF_EVENT_NAME, eventName) != EOK) {
        COMM_LOGE(COMM_DFX, "UpdateSysEventQueryParam  copy domain fail");
    }
    queryRule->eventListSize = SOFTBUS_ONE;
    queryRule->condition = NULL;
    param->eventSize = SOFTBUS_ONE;
}

static void InitSoftBusQueryEventParam(void)
{
    HiSysEventQueryParam* connParam = &g_queryStatsParam[SOFTBUS_CONNECTION_STATS_TYPE];
    UpdateSysEventQueryParam(connParam, CONN_EVENT_NAME);
    connParam->callback.OnQuery = OnQueryConn;
    connParam->callback.OnComplete = OnCompleteConn;
    connParam->dataSize = QUERY_EVENT_FULL_QUERY_PARAM;

    HiSysEventQueryParam* lnnParam = &g_queryStatsParam[SOFTBUS_BUSCENTER_STATS_TYPE];
    UpdateSysEventQueryParam(lnnParam, LNN_EVENT_NAME);
    lnnParam->callback.OnQuery = OnQueryLnn;
    lnnParam->callback.OnComplete = OnCompleteLnn;
    lnnParam->dataSize = QUERY_EVENT_FULL_QUERY_PARAM;

    HiSysEventQueryParam* transParam = &g_queryStatsParam[SOFTBUS_TRANSPORT_STATS_TYPE];
    UpdateSysEventQueryParam(transParam, TRANS_EVENT_NAME);
    transParam->callback.OnQuery = OnQueryTrans;
    transParam->callback.OnComplete = OnCompleteTrans;
    transParam->dataSize = QUERY_EVENT_FULL_QUERY_PARAM;

    HiSysEventQueryParam* manageParam = &g_queryAlarmParam[SOFTBUS_MANAGEMENT_ALARM_TYPE];
    UpdateSysEventQueryParam(manageParam, MANAGE_ALARM_EVENT_NAME);
    manageParam->callback.OnQuery = OnQueryAlarm;
    manageParam->callback.OnComplete = OnCompleteAlarm;
    manageParam->dataSize = MAX_NUM_OF_EVENT_RESULT;

    HiSysEventQueryParam* controlParam = &g_queryAlarmParam[SOFTBUS_CONTROL_ALARM_TYPE];
    UpdateSysEventQueryParam(controlParam, CONTROL_ALARM_EVENT_NAME);
    controlParam->callback.OnQuery = OnQueryAlarm;
    controlParam->callback.OnComplete = OnCompleteAlarm;
    controlParam->dataSize = MAX_NUM_OF_EVENT_RESULT;
}

static void QueryStatisticInfo(SoftBusMessage* param)
{
    (void)param;
    SoftBusStatsResult* result = MallocSoftBusStatsResult(sizeof(SoftBusStatsResult));
    if (result == NULL) {
        COMM_LOGI(COMM_DFX, "create SoftBusStatsResult failed");
        return;
    }
    
    if (SoftBusQueryStatsInfo(DAY_MINUTE, result) != SOFTBUS_OK) {
        COMM_LOGI(COMM_DFX, "QueryStatisticInfo query fail!\n");
        FreeSoftBusStatsResult(result);
        return;
    }

    StatsEventExtra extra = {
        .btFlow = result->btFlow,
        .successRate = (int32_t)(result->successRate * RATE_HUNDRED),
        .maxParaSessionNum = result->maxParaSessionNum,
        .sessionSuccessDuration = result->sessionSuccessDuration,
        .deviceOnlineNum = result->deviceOnlineNum,
        .deviceOnlineTimes = result->deviceOnlineTimes,
        .deviceOfflineTimes = result->deviceOfflineTimes,
        .laneScoreOverTimes = result->laneScoreOverTimes,
        .activationRate = (int32_t)(result->activityRate * RATE_HUNDRED),
        .detectionTimes = result->detectionTimes,
        .successRateDetail = result->successRateDetail,
        .result = EVENT_STAGE_RESULT_OK
    };
    DSOFTBUS_STATS(EVENT_SCENE_STATS, extra);
    COMM_LOGI(COMM_DFX, "QueryStatisticInfo query success!\n");
    FreeSoftBusStatsResult(result);
}

static inline SoftBusHandler* CreateHandler(SoftBusLooper* looper, HandleMessageFunc callback)
{
    SoftBusHandler* handler = SoftBusMalloc(sizeof(SoftBusHandler));
    if (handler == NULL) {
        COMM_LOGI(COMM_DFX, "create handler failed");
        return NULL;
    }
    handler->looper = looper;
    handler->name = "softbusHidumperHandler";
    handler->HandleMessage = callback;

    return handler;
}

static void FreeMessageFunc(SoftBusMessage* msg)
{
    if (msg == NULL) {
        return;
    }

    if (msg->handler != NULL) {
        SoftBusFree(msg->handler);
    }
    SoftBusFree(msg);
}

static SoftBusMessage* CreateMessage(SoftBusLooper* looper, HandleMessageFunc callback)
{
    SoftBusMessage* msg = SoftBusMalloc(sizeof(SoftBusMessage));
    if (msg == NULL) {
        COMM_LOGI(COMM_DFX, "malloc softbus message failed");
        return NULL;
    }

    SoftBusHandler* handler = CreateHandler(looper, callback);
    msg->what = MSG_STATISTIC_QUERY_REPORT;
    msg->obj = NULL;
    msg->handler = handler;
    msg->FreeMessage = FreeMessageFunc;
    return msg;
}

static int32_t CreateAndQueryMsgDelay(SoftBusLooper* looper, HandleMessageFunc callback, uint64_t delayMillis)
{
    if ((looper == NULL) || (callback == NULL)) {
        return SOFTBUS_INVALID_PARAM;
    }

    SoftBusMessage* message = CreateMessage(looper, callback);
    if (message == NULL) {
        return SOFTBUS_MEM_ERR;
    }

    looper->PostMessageDelay(looper, message, delayMillis);
    return SOFTBUS_OK;
}

static void QueryStatisticInfoPeriod(SoftBusMessage* msg)
{
    QueryStatisticInfo(msg);
    CreateAndQueryMsgDelay(GetLooper(LOOP_TYPE_HANDLE_FILE), QueryStatisticInfoPeriod, DAY_TIME);
}

int32_t SoftBusHidumperUtilInit(void)
{
    if (g_isDumperInit) {
        return SOFTBUS_OK;
    }
    if (InitDumperUtilMutexLock() != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "init dump util lock fail");
        return SOFTBUS_ERR;
    }

    g_alarmEvtResult.records = SoftBusMalloc(sizeof(AlarmRecord) * MAX_NUM_OF_EVENT_RESULT);
    if (g_alarmEvtResult.records == NULL) {
        COMM_LOGE(COMM_DFX, "init alarm record fail");
        return SOFTBUS_ERR;
    }
    InitSoftBusQueryEventParam();
    g_isDumperInit = true;
    if (CreateAndQueryMsgDelay(GetLooper(LOOP_TYPE_HANDLE_FILE), QueryStatisticInfoPeriod, DAY_TIME) != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "CreateAndQueryMsgDelay fail");
    }
    return SOFTBUS_OK;
}

void SoftBusHidumperUtilDeInit(void)
{
    if (!g_isDumperInit) {
        return;
    }

    SoftBusFree(g_alarmEvtResult.records);
    if (TransMapLock() != SOFTBUS_OK) {
        COMM_LOGE(COMM_DFX, "lock TransMap fail");
    }
    LnnMapDelete(&g_transStatsInfo.sessionNameLinkTypeMap);
    TransMapUnlock();
    SoftBusMutexDestroy(&g_transMapLock);
    g_isTransMapInit = false;
    SoftBusMutexDestroy(&g_statsQueryLock);
    SoftBusMutexDestroy(&g_alarmQueryLock);
    SoftBusMutexDestroy(&g_connOnQueryLock);
    SoftBusMutexDestroy(&g_lnnOnQueryLock);
    SoftBusMutexDestroy(&g_transOnQueryLock);
    SoftBusMutexDestroy(&g_alarmOnQueryLock);
    g_isDumperInit = false;
}