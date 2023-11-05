/*
 * Copyright (C) 2021-2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/license/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANT KIND, either express or implied.
 * See the License for the specific language governing permission and
 * limitations under the License.
 */

#ifndef OHOS_LNN_META_NODE_INTERFACE_H
#define OHOS_LNN_META_NODE_INTERFACE_H

#include <stdint.h>
#include "softbus_bus_center.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t LnnLoadMetaNode(const int32_t tType);
int32_t LnnUnLoadMetaNode(const int32_t tType);
int32_t MetaNodeServerLeaveExt(const char *metaNodeId, MetaNodeType tType);
int32_t MetaNodeServerJoinExt(CustomData *customData);
int32_t LnnInitMetaNode(void);
void LnnDeinitMetaNode(void);
MetaNodeType FindMetaNodeType(const char *metaNodeId);
int32_t LnnInitMetaNodeExtLedger(void);
void LnnDeinitMetaNodeExtLedger(void);

#ifdef __cplusplus
}
#endif
#endif