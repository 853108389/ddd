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
#ifndef NEGOTIATE_AVAILABLE_STATE_H
#define NEGOTIATE_AVAILABLE_STATE_H

#include "negotiate_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AvailableState {
    NEGOTIATE_STATE_BASE(AvailableState);
};

struct AvailableState* GetAvailableState(struct WifiDirectNegotiator *negotiator);

#ifdef __cplusplus
}
#endif
#endif