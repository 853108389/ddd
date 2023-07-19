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

#ifndef LNN_SECURE_STORAGE_H
#define LNN_SECURE_STORAGE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
    LNN_DATA_TYPE_LOCAL_DEVINFO = 1,
    LNN_DATA_TYPE_REMOTE_DEVINFO,
    LNN_DATA_TYPE_DEVICE_KEY,
} LnnDataType;

int32_t LnnSaveDeviceData(const char *data, LnnDataType dataType);
int32_t LnnRetrieveDeviceData(LnnDataType dataType, char **data, uint32_t *dataLen);
int32_t LnnUpdateDeviceData(const char *data, LnnDataType dataType);
int32_t LnnDeletaDeviceData(LnnDataType dataType);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LNN_SECURE_STORAGE_H */