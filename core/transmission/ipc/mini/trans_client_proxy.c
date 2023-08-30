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

#include "trans_client_proxy.h"

#include "client_trans_channel_callback.h"
#include "softbus_def.h"
#include "softbus_errcode.h"

int32_t ClientIpcOnChannelOpened(const char *pkgName, const char *sessionName,
    const ChannelInfo *channel, int32_t pid)
{
    (void)pkgName;
    (void)pid;
    return TransOnChannelOpened(sessionName, channel);
}

int32_t ClientIpcOnChannelOpenFailed(const char *pkgName, int32_t channelId, int32_t channelType,
    int32_t errCode, int32_t pid)
{
    (void)pkgName;
    (void)pid;
    return TransOnChannelOpenFailed(channelId, channelType, errCode);
}

int32_t ClientIpcOnChannelLinkDown(const char *pkgName, const char *networkId,
    const char *uuid, const char *udid, const char *peerIp, int32_t routeType, int32_t pid)
{
    (void)pkgName;
    (void)pid;
    (void)uuid;
    (void)udid;
    (void)peerIp;
    return TransOnChannelLinkDown(networkId, routeType);
}

int32_t ClientIpcOnChannelClosed(const char *pkgName, int32_t channelId, int32_t channelType,
    int32_t pid)
{
    (void)pkgName;
    (void)pid;
    return TransOnChannelClosed(channelId, channelType);
}

int32_t ClientIpcOnChannelMsgReceived(const char *pkgName, int32_t channelId, int32_t channelType,
    TransReceiveData *receiveData, int32_t pid)
{
    (void)pkgName;
    (void)pid;
    return TransOnChannelMsgReceived(channelId, channelType, receiveData->data, receiveData->dataLen,
        (SessionPktType)receiveData->dataType);
}

int32_t ClientIpcOnChannelQosEvent(const char *pkgName, const QosParam *param)
{
    (void)pkgName;
    (void)param;
    return SOFTBUS_FUNC_NOT_SUPPORT;
}
