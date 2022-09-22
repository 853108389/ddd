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
#include "softbus_adapter_hisysevent.h"

#include <string>
#include <sstream>
#include <securec.h>
#include "softbus_error_code.h"

#include "softbus_adapter_log.h"
#include "softbus_adapter_mem.h"
#include "message_handler.h"
#include "hisysevent_c.h"

static const char *g_domain = "DSOFTBUS";
static HiSysEventParam g_dstParam[SOFTBUS_EVT_PARAM_BUTT];

static int32_t ConvertEventParam(SoftBusEvtParam *srcParam, HiSysEventParam *dstParam)
{
    switch (srcParam->paramType) {
        case SOFTBUS_EVT_PARAMTYPE_BOOL:
            dstParam->t = HISYSEVENT_BOOL;
            dstParam->v.b = srcParam->paramValue.b;
            break;
        case SOFTBUS_EVT_PARAMTYPE_UINT8:
            dstParam->t = HISYSEVENT_UINT8;
            dstParam->v.ui8 = srcParam->paramValue.u8v;
            break;
        case SOFTBUS_EVT_PARAMTYPE_UINT16:
            dstParam->t = HISYSEVENT_UINT16;
            dstParam->v.ui16 = srcParam->paramValue.u16v;
            break;
        case SOFTBUS_EVT_PARAMTYPE_INT32:
            dstParam->t = HISYSEVENT_INT32;
            dstParam->v.i32 = srcParam->paramValue.i32v;
            break;
        case SOFTBUS_EVT_PARAMTYPE_UINT32:
            dstParam->t = HISYSEVENT_UINT32;
            dstParam->v.ui32 = srcParam->paramValue.u32v;
            break;
        case SOFTBUS_EVT_PARAMTYPE_UINT64:
            dstParam->t = HISYSEVENT_UINT64;
            dstParam->v.ui64 = srcParam->paramValue.u64v;
            break;
        case SOFTBUS_EVT_PARAMTYPE_FLOAT:
            dstParam->t = HISYSEVENT_FLOAT;
            dstParam->v.f = srcParam->paramValue.f;
            break;
        case SOFTBUS_EVT_PARAMTYPE_DOUBLE:
            dstParam->t = HISYSEVENT_DOUBLE;
            dstParam->v.d = srcParam->paramValue.d;
            break;
        case SOFTBUS_EVT_PARAMTYPE_STRING:
            dstParam->t = HISYSEVENT_STRING;
            dstParam->v.s = (char *)SoftBusMalloc(sizeof(char) * strlen(srcParam->paramValue.str) + 1);
            if (strcpy_s(dstParam->v.s, strlen(srcParam->paramValue.str) + 1,
                srcParam->paramValue.str) != EOK) {
                SoftBusFree(dstParam->v.s);
                HILOG_ERROR(SOFTBUS_HILOG_ID, "copy string var fail");
                return SOFTBUS_ERR;
            }
            break;
        default:
            break;
    }
    return SOFTBUS_OK;
}

static int32_t ConvertMsgToHiSysEvent(SoftBusEvtReportMsg *msg)
{
    if (memset_s(g_dstParam, sizeof(SoftBusEvtReportMsg) * SOFTBUS_EVT_PARAM_BUTT, 0,
        sizeof(SoftBusEvtReportMsg) * SOFTBUS_EVT_PARAM_BUTT) != EOK) {
        HILOG_ERROR(SOFTBUS_HILOG_ID, "init  g_dstParam fail");
        return SOFTBUS_ERR;
    }
    for (uint32_t i = 0; i < msg->paramNum; i++) {
        if (strcpy_s(g_dstParam[i].name, SOFTBUS_HISYSEVT_NAME_LEN, msg->paramArray[i].paramName) != EOK) {
            HILOG_ERROR(SOFTBUS_HILOG_ID, "copy param fail");
            return SOFTBUS_ERR;
        }
        ConvertEventParam(&msg->paramArray[i], &g_dstParam[i]);
    }
    return SOFTBUS_OK;
}

static void HiSysEventParamDeInit(uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (g_dstParam[i].t == HISYSEVENT_STRING && g_dstParam[i].v.s != NULL) {
            SoftBusFree(g_dstParam[i].v.s);
        }
     }
 }
 
static HiSysEventEventType ConvertMsgType(SoftBusEvtType type)
{
    HiSysEventEventType hiSysEvtType;
    switch (type) {
        case SOFTBUS_EVT_TYPE_FAULT:
            hiSysEvtType = HISYSEVENT_FAULT;
            break;
        case SOFTBUS_EVT_TYPE_STATISTIC:
            hiSysEvtType = HISYSEVENT_STATISTIC;
            break;
        case SOFTBUS_EVT_TYPE_SECURITY:
            hiSysEvtType = HISYSEVENT_SECURITY;
            break;
        case SOFTBUS_EVT_TYPE_BEHAVIOR:
            hiSysEvtType = HISYSEVENT_BEHAVIOR;
            break;
        default:
            hiSysEvtType = HISYSEVENT_STATISTIC;
            break;
    }
    return hiSysEvtType;
}

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

int32_t SoftbusWriteHisEvt(SoftBusEvtReportMsg* reportMsg)
{
    if (reportMsg == nullptr) {
        return SOFTBUS_ERR;
    }
    ConvertMsgToHiSysEvent(reportMsg);
    OH_HiSysEvent_Write(g_domain, reportMsg->evtName, ConvertMsgType(reportMsg->evtType),
        g_dstParam, reportMsg->paramNum);
    HiSysEventParamDeInit(reportMsg->paramNum);
    return SOFTBUS_OK;
}

void SoftbusFreeEvtReporMsg(SoftBusEvtReportMsg* msg)
{
    if (msg == nullptr) {
        return;
    }

    if (msg->paramArray != nullptr) {
        SoftBusFree(msg->paramArray);
    }
    
    SoftBusFree(msg);
}

SoftBusEvtReportMsg* SoftbusCreateEvtReportMsg(int32_t paramNum)
{
    SoftBusEvtReportMsg *msg = (SoftBusEvtReportMsg*)SoftBusMalloc(sizeof(SoftBusEvtReportMsg));
    if (msg == nullptr) {
        return nullptr;
    }

    msg->paramArray = (SoftBusEvtParam*)SoftBusMalloc(sizeof(SoftBusEvtParam) * paramNum);
    if (msg->paramArray == nullptr) {
        SoftbusFreeEvtReporMsg(msg);
    }
    
    return msg;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
