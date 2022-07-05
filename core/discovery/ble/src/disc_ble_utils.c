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
#include "disc_ble_utils.h"

#include <stdbool.h>
#include <stdint.h>

#include "bus_center_manager.h"
#include "cJSON.h"
#include "disc_ble_constant.h"
#include "discovery_service.h"
#include "lnn_device_info.h"
#include "securec.h"
#include "softbus_adapter_crypto.h"
#include "softbus_common.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_utils.h"
#include "softbus_adapter_mem.h"

#define DATA_TYPE_MASK 0xF0
#define DATA_LENGTH_MASK 0x0F
#define BYTE_SHIFT 4

#define MAC_BIT_ZERO 0
#define MAC_BIT_ONE 1
#define MAC_BIT_TWO 2
#define MAC_BIT_THREE 3
#define MAC_BIT_FOUR 4
#define MAC_BIT_FIVE 5

#ifndef PACKET_CHECK_LENGTH
#define PACKET_CHECK_LENGTH(len) \
    if (len >= 0) { \
        curLen += len; \
    } else { \
        return len; \
    }
#endif

bool CheckBitMapEmpty(uint32_t capBitMapNum, const uint32_t *capBitMap)
{
    if (capBitMap == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < capBitMapNum; i++) {
        if (capBitMap[i] != 0x0) {
            return false;
        }
    }
    return true;
}

bool CheckCapBitMapExist(uint32_t capBitMapNum, const uint32_t *capBitMap, uint32_t pos)
{
    if (capBitMap == NULL) {
        return false;
    }
    uint32_t index = pos / INT32_MAX_BIT_NUM;
    if (index >= capBitMapNum) {
        return false;
    }
    return (capBitMap[index] >> (pos % INT32_MAX_BIT_NUM)) & 0x1 ? true : false;
}

bool CheckCapBitMapEqual(const uint32_t *srcBitMap, const uint32_t *dstBitMap, uint32_t capBitMapNum)
{
    if (srcBitMap == NULL || dstBitMap == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < capBitMapNum; i++) {
        if (srcBitMap[i] != dstBitMap[i]) {
            return false;
        }
    }
    return true;
}

void SetCapBitMapPos(uint32_t capBitMapNum, uint32_t *capBitMap, uint32_t pos)
{
    if (capBitMap == NULL) {
        return;
    }
    uint32_t index = pos / INT32_MAX_BIT_NUM;
    if (index >= capBitMapNum) {
        return;
    }
    capBitMap[index] = capBitMap[index] | (0x1 << (pos % INT32_MAX_BIT_NUM));
}

void UnsetCapBitMapPos(uint32_t capBitMapNum, uint32_t *capBitMap, uint32_t pos)
{
    if (capBitMap == NULL) {
        return;
    }
    uint32_t index = pos / INT32_MAX_BIT_NUM;
    if (index >= capBitMapNum) {
        return;
    }
    uint32_t mask = 0xffffffff ^ (0x1 << (pos % INT32_MAX_BIT_NUM));
    capBitMap[index] = capBitMap[index] & mask;
}

int32_t DiscBleGetHwAccount(char *hwAccount)
{
    if (hwAccount == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    const char *account = "";
    if (memcpy_s(hwAccount, strlen(account) + 1, account, strlen(account) + 1) != EOK) {
        return SOFTBUS_MEM_ERR;
    }
    return SOFTBUS_OK;
}

int32_t DiscBleGetDeviceUdid(char *udid, uint32_t len)
{
    if (udid == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    if (LnnGetLocalStrInfo(STRING_KEY_DEV_UDID, udid, len) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "Get local dev Id failed.");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t DiscBleGetDeviceName(char *deviceName)
{
    if (deviceName == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    if (LnnGetLocalStrInfo(STRING_KEY_DEV_NAME, deviceName, DEVICE_NAME_BUF_LEN) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "Get local device name failed.");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

uint8_t DiscBleGetDeviceType(void)
{
    char type[DEVICE_TYPE_BUF_LEN] = {0};
    uint8_t typeId;
    if (LnnGetLocalStrInfo(STRING_KEY_DEV_TYPE, type, DEVICE_TYPE_BUF_LEN) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "Get local device type failed.");
        return TYPE_UNKNOW_ID;
    }
    if (LnnConvertDeviceTypeToId(type, (uint16_t *)&typeId) != SOFTBUS_OK) {
        return TYPE_UNKNOW_ID;
    }
    return typeId;
}

int32_t DiscBleGetDeviceIdHash(unsigned char *hashStr)
{
    if (hashStr == NULL) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "hashstr is null");
        return SOFTBUS_INVALID_PARAM;
    }
    char udid[DISC_MAX_DEVICE_ID_LEN] = {0};
    char hashResult[SHA_HASH_LEN] = {0};
    int32_t ret = DiscBleGetDeviceUdid(udid, sizeof(udid));
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "GetDeviceId failed");
        return ret;
    }
    ret = SoftBusGenerateStrHash((const unsigned char *)udid, strlen(udid) + 1, (unsigned char *)hashResult);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "GenerateStrHash failed");
        return ret;
    }
    ret = ConvertBytesToHexString((char *)hashStr, SHORT_DEVICE_ID_HASH_LENGTH + 1, (const unsigned char *)hashResult,
        SHORT_DEVICE_ID_HASH_LENGTH / HEXIFY_UNIT_LEN);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "ConvertBytesToHexString failed");
        return ret;
    }
    return SOFTBUS_OK;
}

int32_t DiscBleGetShortUserIdHash(unsigned char *hashStr, uint32_t len)
{
    if (hashStr == NULL || len == 0) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "hashstr is null");
        return SOFTBUS_INVALID_PARAM;
    }
    uint8_t account[SHA_256_HASH_LEN] = {0};
    if (LnnGetLocalByteInfo(BYTE_KEY_USERID_HASH, account, SHA_256_HASH_LEN) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "DiscBleGetShortUserIdHash get local user id failed");
        return SOFTBUS_ERR;
    }
    if (memcpy_s(hashStr, len, account, len) != EOK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "DiscBleGetShortUserIdHash memcpy_s failed");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t AssembleTLV(BoardcastData *boardcastData, unsigned char dataType, const void *value, uint32_t dataLen)
{
    if (boardcastData == NULL || value == NULL || dataLen == 0 || boardcastData->dataLen >= BOARDCAST_MAX_LEN) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "AssembleTLV invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    uint32_t len = dataLen & DATA_LENGTH_MASK;
    boardcastData->data.data[boardcastData->dataLen] = (dataType << BYTE_SHIFT) & DATA_TYPE_MASK;
    boardcastData->data.data[boardcastData->dataLen] |= dataLen & DATA_LENGTH_MASK;
    boardcastData->dataLen += 1;
    uint32_t remainLen = BOARDCAST_MAX_LEN - boardcastData->dataLen;
    if (remainLen == 0) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "tlv remainLen is 0.");
        return SOFTBUS_ERR;
    }
    uint32_t validLen = (len > remainLen) ? remainLen : len;
    if (memcpy_s(&(boardcastData->data.data[boardcastData->dataLen]), validLen, value, validLen) != EOK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "assemble tlv memcpy failed");
        return SOFTBUS_MEM_ERR;
    }
    boardcastData->dataLen += validLen;
    return SOFTBUS_OK;
}

static int32_t ParseRecvAdvData(const unsigned char *data, uint32_t dataLen, unsigned char type,
    uint32_t index, void *value)
{
    if (data == NULL || value == NULL) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "ParseRecvAdvData input param invalid");
        return SOFTBUS_INVALID_PARAM;
    }
    if (dataLen <= index) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_INFO, "index >= dataLen");
        return 0;
    }
    if (type != ((data[index] & DATA_TYPE_MASK) >> BYTE_SHIFT)) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "type check failed");
        return 0;
    }
    uint32_t len = (uint32_t)(data[index] & DATA_LENGTH_MASK);
    if (index + TL_LEN >= dataLen) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "advData out of range");
        return SOFTBUS_ERR;
    }
    if (len == 0) {
        return 0;
    }
    if (memcpy_s(value, len, &data[index + 1], len) != EOK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "ParseRecvAdvData memcpy failed");
        return SOFTBUS_MEM_ERR;
    }
    return len + TL_LEN;
}

static int32_t ParseRecvTlvs(DeviceWrapper *device, const unsigned char *data, uint32_t dataLen)
{
    device->info->capabilityBitmap[0] = data[POS_CAPABLITY + ADV_HEAD_LEN];
    int32_t curLen = POS_TLV + ADV_HEAD_LEN;
    unsigned char devType;
    int32_t len = ParseRecvAdvData(data, dataLen, TLV_TYPE_DEVICE_ID_HASH, curLen, device->info->devId);
    PACKET_CHECK_LENGTH(len);
    len = ParseRecvAdvData(data, dataLen, TLV_TYPE_DEVICE_TYPE, curLen, &devType);
    PACKET_CHECK_LENGTH(len);
    device->info->devType = (DeviceType)devType;
    len = ParseRecvAdvData(data, dataLen, TLV_TYPE_BR_MAC, curLen, device->info->addr[0].info.ble.bleMac);
    PACKET_CHECK_LENGTH(len);
    len = ParseRecvAdvData(data, dataLen, TLV_TYPE_RANGE_POWER, curLen, &(device->power));
    PACKET_CHECK_LENGTH(len);
    len = ParseRecvAdvData(data, dataLen, TLV_TYPE_CUST, curLen, device->info->custData);
    PACKET_CHECK_LENGTH(len);
    len = ParseRecvAdvData(data, dataLen, TLV_TYPE_DEVICE_NAME, curLen, device->info->devName);
    PACKET_CHECK_LENGTH(len);
    return SOFTBUS_OK;
}

int32_t GetDeviceInfoFromDisAdvData(DeviceWrapper *device, const unsigned char *data, uint32_t dataLen)
{
    if (device == NULL || device->info == NULL || data == NULL || dataLen == 0) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "GetDeviceInfoFromAdvData input param is invalid");
        return SOFTBUS_INVALID_PARAM;
    }
    if (memcpy_s(device->info->accountHash, SHORT_USER_ID_HASH_LEN,
        &data[POS_USER_ID_HASH + ADV_HEAD_LEN], SHORT_USER_ID_HASH_LEN) != EOK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "copy accountHash failed");
        return SOFTBUS_MEM_ERR;
    }

    if (dataLen <= MAX_BROADCAST_DATA + SCAN_RSP_HEADER_LEN) {
        return ParseRecvTlvs(device, data, dataLen);
    }
    
    // it needs to skip scan resp AD Structure header when TLV overflow to scan rsp
    int indScanRspDataLen = dataLen - SCAN_RSP_HEADER_LEN;
    unsigned char *copyData = SoftBusCalloc(indScanRspDataLen);
    if (copyData == NULL) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "malloc failed.");
        return SOFTBUS_MEM_ERR;
    }

    if (memcpy_s(copyData, MAX_BROADCAST_DATA, data, MAX_BROADCAST_DATA) != EOK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "memcpy_s adv failed, can not skip scan resp flag.");
        SoftBusFree(copyData);
        return SOFTBUS_MEM_ERR;
    }

    const unsigned char *destScanbRspTlv = copyData + MAX_BROADCAST_DATA;
    int destScanRspTlvLen = indScanRspDataLen - MAX_BROADCAST_DATA;
    const unsigned char *srcScanRspTlv = data + MAX_BROADCAST_DATA + SCAN_RSP_HEADER_LEN;
    int srcScanRspTlvLen = dataLen-MAX_BROADCAST_DATA-SCAN_RSP_HEADER_LEN;
    if (memcpy_s(destScanbRspTlv, destScanRspTlvLen, srcScanRspTlv,  srcScanRspTlvLen) != EOK) {
        SoftBusLog(SOFTBUS_LOG_DISC, SOFTBUS_LOG_ERROR, "memcpy_s scan rsp failed, can not skip scan resp flag.");
        SoftBusFree(copyData);
        return SOFTBUS_MEM_ERR;
    }

    int32_t ret = ParseRecvTlvs(device, copyData, indScanRspDataLen);
    SoftBusFree(copyData);
    return ret;
}