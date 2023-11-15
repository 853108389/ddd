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

#ifndef SOFTBUS_EVENT_H
#define SOFTBUS_EVENT_H

#include <stdlib.h>

#include "form/softbus_event_form.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVENT_MODULE_CONN,
    EVENT_MODULE_DISC,
    EVENT_MODULE_LNN,
    EVENT_MODULE_TRANS,
} SoftbusEventModule;

void SoftbusEventInner(SoftbusEventModule module, SoftbusEventForm form);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif // SOFTBUS_EVENT_H