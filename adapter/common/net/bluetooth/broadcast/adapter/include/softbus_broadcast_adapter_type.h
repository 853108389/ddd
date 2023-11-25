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

/**
 * @file softbus_broadcast_adapter_type.h
 * @brief Declare functions and constants for the soft bus broadcast adaptation
 *
 * @since 1.0
 * @version 1.0
 */

#ifndef SOFTBUS_BROADCAST_ADAPTER_TYPE_H
#define SOFTBUS_BROADCAST_ADAPTER_TYPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"{
#endif

/**
 * @brief Defines mac address length
 *
 * @since 1.0
 * @version 1.0
 */
#define SOFTBUS_ADDR_MAC_LEN 6

/**
 * @brief Defines different broadcast media protocol stacks
 *
 * @since 1.0
 * @version 1.0
 */
enum SoftbusMediumType {
    BROADCAST_MEDIUM_TYPE_BLE,
    BROADCAST_MEDIUM_TYPE_SLE,
    BROADCAST_MEDIUM_TYPE_BUTT,
};

/**
 * @brief Defines the broadcast data information
 *
 * @since 1.0
 * @version 1.0
 */
typedef struct {
    uint16_t uuidLen;
    uint16_t serviceLen;
    uint8_t *uuid;
    uint8_t *serviceData;
    uint16_t companyId;
    uint16_t manufacturerDataLen;
    uint8_t *manufacturerData;
    uint8_t flag;
    uint8_t rsv[3]; // Reserved
} SoftbusBroadcastData;

/**
 * @brief Defines mac address information
 *
 * @since 1.0
 * @version 1.0
 */
typedef struct {
    uint8_t addr[SOFTBUS_ADDR_MAC_LEN];
} SoftbusMacAddr;

/**
 * @brief Defines the device information returned by <b>SoftbusBroadcastCallback</b>.
 *
 * @since 1.0
 * @version 1.0
 */
typedef struct {
    uint8_t eventType;
    uint8_t dataStatus;
    uint8_t primaryPhy;
    uint8_t secondaryPhy;
    uint8_t advSid;
    int8_t txPower;
    int8_t rssi;
    uint8_t addrType;
    SoftbusMacAddr addr;
    SoftbusBroadcastData data;
} SoftBusBleScanResult;

/**
 * @brief Defines the broadcast parameters
 *
 * @since 1.0
 * @version 1.0
 */
typedef struct {
    int32_t minInterval;
    int32_t maxInterval;
    uint8_t advType;
    uint8_t advFilterPolicy;
    uint8_t ownAddrType;
    uint8_t peerAddrType;
    SoftbusMacAddr peerAddr;
    int32_t channelMap;
    int32_t duration;
    int8_t txPower;
} SoftbusBroadcastParam;

/**
 * @brief Defines broadcast scan filters
 *
 * @since 1.0
 * @version 1.0
 */
typedef struct {
    int8_t *address;
    int8_t *deviceName;
    uint32_t serviceUuidLength;
    uint8_t *serviceUuid;
    uint8_t *serviceUuidMask;
    uint32_t serviceDataLength;
    uint8_t *serviceData;
    uint8_t *serviceDataMask;
    uint32_t manufactureDataLength;
    uint8_t *manufactureData;
    uint8_t *manufactureDataMask;
    uint16_t manufactureId;
} SoftBusBcScanFilter;

/**
 * @brief Defines broadcast scan parameters
 *
 * @since 1.0
 * @version 1.0
 */
typedef struct {
    uint16_t scanInterval;
    uint16_t scanWindow;
    uint8_t scanType;
    uint8_t scanPhy;
    uint8_t scanFilterPolicy;
} SoftBusBcScanParams;

#ifdef __cplusplus
}
#endif

#endif /* SOFTBUS_BROADCAST_ADAPTER_TYPE_H */
