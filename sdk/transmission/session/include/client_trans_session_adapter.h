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

#ifndef CLIENT_TRANS_SESSION_ADAPTER_H
#define CLIENT_TRANS_SESSION_ADAPTER_H

#include <stdint.h>
#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif
int32_t CreateSocket(const char *pkgName, const char *sessionName);
int32_t ClientAddSocket(const SocketInfo *info, int32_t *sessionId);
int32_t ClientListen(int32_t socket, const QosTV qos[], uint32_t qosCount, const ISocketListener *listener);
int32_t ClientBind(int32_t socket, const QosTV qos[], uint32_t qosCount, const ISocketListener *listener);
void ClientShutdown(int32_t socket);
#ifdef __cplusplus
}
#endif
#endif // CLIENT_TRANS_SESSION_ADAPTER_H