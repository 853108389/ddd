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
#ifndef P2P_V1_PROCESSOR_H
#define P2P_V1_PROCESSOR_H

#include "wifi_direct_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P2P_VERSION 2

struct P2pV1Processor {
    PROCESSOR_BASE;

    void (*initBasicInnerLink)(struct InnerLink *innerLink, bool isClient);
    int32_t (*saveCurrentMessage)(struct NegotiateMessage *msg);
    void (*setInnerLinkDeviceId)(struct NegotiateMessage *msg, struct InnerLink *innerLink);
    int32_t (*createGroup)(struct NegotiateMessage *msg);
    int32_t (*connectGroup)(struct NegotiateMessage *msg);
    int32_t (*reuseP2p)(void);
    int32_t (*removeLink)(const char *remoteMac);
    void (*notifyNewClient)(int requestId, char *localInterface, char *remoteMac);
    void (*cancelNewClient)(int requestId, char *localInterface, const char *remoteMac);
    char *(*getGoMac)(enum WifiDirectRole myRole);

    struct InnerLink *currentInnerLink;
    bool needReply;
    struct NegotiateMessage *pendingRequestMsg;
    int32_t currentRequestId;
    int32_t goPort;
};

struct P2pV1Processor* GetP2pV1Processor(void);

#ifdef __cplusplus
}
#endif
#endif