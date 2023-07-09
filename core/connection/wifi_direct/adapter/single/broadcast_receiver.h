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
#ifndef WIFI_DIRECT_BROADCAST_RECEIVER_H
#define WIFI_DIRECT_BROADCAST_RECEIVER_H

#include <stdint.h>
#include <stddef.h>
#include "common_list.h"
#include "wifi_p2p.h"
#include "wifi_direct_p2p_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

enum BroadcastReceiverAction {
    WIFI_P2P_STATE_CHANGED_ACTION = 0,
    WIFI_P2P_CONNECTION_CHANGED_ACTION = 1,
    WIFI_CFG_CHANGED_ACTION = 2,
    BROADCAST_RECEIVER_ACTION_MAX,
};

struct P2pConnChangedInfo {
    WifiP2pLinkedInfo p2pLinkInfo;
    struct WifiDirectGroupInfo *groupInfo;
};

struct BroadcastParam {
    enum BroadcastReceiverAction action;
    union {
        P2pState p2pState;
        struct P2pConnChangedInfo changedInfo;
        int wifiConnectChangedState;
    };
};

typedef void (*BroadcastListener)(enum BroadcastReceiverAction action, const struct BroadcastParam *param);

struct BroadcastReceiver {
    void (*registerBroadcastListener)(const enum BroadcastReceiverAction *actionArray, size_t actionSize,
                                      const char *name, BroadcastListener listener);

    ListNode listeners[BROADCAST_RECEIVER_ACTION_MAX];
    bool isInited;
};

struct BroadcastReceiver* GetBroadcastReceiver(void);
int32_t BroadcastReceiverInit(void);

#ifdef __cplusplus
}
#endif
#endif