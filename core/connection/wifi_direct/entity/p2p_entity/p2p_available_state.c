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

#include "entity/p2p_entity/p2p_available_state.h"
#include <string.h>
#include "securec.h"
#include "softbus_log.h"
#include "softbus_error_code.h"
#include "softbus_adapter_mem.h"
#include "wifi_direct_p2p_adapter.h"
#include "data/resource_manager.h"
#include "entity/p2p_entity/p2p_entity.h"
#include "utils/wifi_direct_network_utils.h"
#include "utils/wifi_direct_anonymous.h"

#define LOG_LABEL "[WifiDirect] P2pAvailableState: "
#define LINK_ATTR_STR_LEN 64

/* public interface */
static void Enter(struct P2pEntityState *self)
{
    CLOGI(LOG_LABEL "enter");
    GetP2pEntity()->stopTimer();
}

static void Exit(struct P2pEntityState *self)
{
    CLOGI(LOG_LABEL "enter");
}

static void SetLinkAttr(struct WifiDirectConnectParams *params)
{
    if (strlen(params->remoteMac) == 0) {
        CLOGI(LOG_LABEL "no need to set link attr");
        return;
    }

    char linkAttr[LINK_ATTR_STR_LEN] = {0};
    int32_t ret = sprintf_s(linkAttr, sizeof(linkAttr), "isProxyEnable=%s,mac=%s",
                            params->isProxyEnable ? "1" : "0", params->remoteMac);
    CONN_CHECK_AND_RETURN_LOG(ret > 0, LOG_LABEL "format link attr string failed");

    CLOGI(LOG_LABEL "interface=%s isProxyEnable=%s,mac=%s", params->interface, params->isProxyEnable ? "1" : "0",
          WifiDirectAnonymizeMac(params->remoteMac));
    GetWifiDirectP2pAdapter()->setWifiLinkAttr(params->interface, linkAttr);
}

static int32_t CreateServer(struct P2pEntityState *self, struct WifiDirectConnectParams *params)
{
    CONN_CHECK_AND_RETURN_RET_LOG(params, SOFTBUS_INVALID_PARAM, LOG_LABEL "params is null");
    SetLinkAttr(params);
    struct WifiDirectP2pAdapter *adapter = GetWifiDirectP2pAdapter();
    int32_t ret = adapter->createGroup(params->freq, params->isWideBandSupported);
    CONN_CHECK_AND_RETURN_RET_LOG(ret == SOFTBUS_OK, ret, LOG_LABEL "p2p create group failed");

    GetP2pEntity()->changeState(P2P_ENTITY_STATE_GROUP_CREATING);
    return SOFTBUS_OK;
}

static int32_t Connect(struct P2pEntityState *self, struct WifiDirectConnectParams *params)
{
    CONN_CHECK_AND_RETURN_RET_LOG(params, SOFTBUS_INVALID_PARAM, LOG_LABEL "params is null");
    SetLinkAttr(params);
    struct WifiDirectP2pAdapter *adapter = GetWifiDirectP2pAdapter();
    int32_t ret = adapter->connectGroup(params->groupConfig);
    CONN_CHECK_AND_RETURN_RET_LOG(ret == SOFTBUS_OK, ret, LOG_LABEL "p2p connect group failed");

    GetP2pEntity()->changeState(P2P_ENTITY_STATE_GROUP_CONNECTING);
    return SOFTBUS_OK;
}

static int32_t RemoveLink(struct P2pEntityState *self, struct WifiDirectConnectParams *params)
{
    CONN_CHECK_AND_RETURN_RET_LOG(params, SOFTBUS_INVALID_PARAM, LOG_LABEL "params is null");
    struct P2pEntity *entity = GetP2pEntity();

    CLOGI(LOG_LABEL "shareLinkRemoveGroup Async");
    struct WifiDirectP2pAdapter *adapter = GetWifiDirectP2pAdapter();
    int32_t ret = adapter->shareLinkRemoveGroupAsync(params->interface);
    CONN_CHECK_AND_RETURN_RET_LOG(ret == SOFTBUS_OK, ret, LOG_LABEL "p2p share link remove group failed");

    entity->changeState(P2P_ENTITY_STATE_GROUP_REMOVING);
    return SOFTBUS_OK;
}

static int32_t DestroyServer(struct P2pEntityState *self, struct WifiDirectConnectParams *params)
{
    CONN_CHECK_AND_RETURN_RET_LOG(params, SOFTBUS_INVALID_PARAM, LOG_LABEL "params is null");
    struct P2pEntity *entity = GetP2pEntity();

    struct WifiDirectP2pAdapter *adapter = GetWifiDirectP2pAdapter();
    int32_t ret = adapter->removeGroup(params->interface);
    CONN_CHECK_AND_RETURN_RET_LOG(ret == SOFTBUS_OK, ret, LOG_LABEL "p2p remove group failed");

    entity->changeState(P2P_ENTITY_STATE_GROUP_REMOVING);
    return SOFTBUS_OK;
}

static void HandleConnectionChange(struct P2pEntityState *self, struct WifiDirectP2pGroupInfo *groupInfo)
{
    struct P2pEntity *entity = GetP2pEntity();
    if (!groupInfo) {
        CLOGI(LOG_LABEL "no groupInfo");
        entity->stopNewClientTimer();
        entity->clearJoiningClient();
        return;
    }
    if (!groupInfo->isGroupOwner) {
        CLOGI(LOG_LABEL "not go, ignore");
        return;
    }

    CLOGI(LOG_LABEL "remove joining client, clientDeviceSize=%d", groupInfo->clientDeviceSize);
    for (int32_t i = 0; i < groupInfo->clientDeviceSize; i++) {
        struct P2pEntityConnectingClient *client = NULL;
        struct P2pEntityConnectingClient *clientNext = NULL;
        LIST_FOR_EACH_ENTRY_SAFE(client, clientNext, &entity->joiningClientList,
                                 struct P2pEntityConnectingClient, node) {
            char mac[MAC_ADDR_STR_LEN] = {0};
            GetWifiDirectNetWorkUtils()->macArrayToString(groupInfo->clientDevices[i].address, MAC_ADDR_ARRAY_SIZE,
                                                          mac, sizeof(mac));
            if (strcmp(mac, client->remoteMac) == 0) {
                CLOGI(LOG_LABEL "remove joining client, request=%d remoteMac=%s",
                      client->requestId, WifiDirectAnonymizeMac(client->remoteMac));
                entity->stopNewClientTimer();
                ListDelete(&client->node);
                SoftBusFree(client);
                entity->joiningClientCount--;
            }

            entity->listener->onClientConnected(mac);
        }
    }

    struct InterfaceInfo *info = GetResourceManager()->getInterfaceInfo(IF_NAME_P2P);
    CONN_CHECK_AND_RETURN_LOG(info, "interface info is null");
    int32_t reuseCount = info->getInt(info, II_KEY_REUSE_COUNT, 0);
    CLOGI(LOG_LABEL "joiningClientCount=%d reuseCount=%d", entity->joiningClientCount, reuseCount);
    if (groupInfo->clientDeviceSize == 0 && entity->joiningClientCount == 0 && reuseCount > 0) {
        CLOGI(LOG_LABEL "gc disconnected abnormally");
        GetWifiDirectP2pAdapter()->shareLinkRemoveGroupSync(IF_NAME_P2P);
    }
}

static void HandleConnectStateChange(struct P2pEntityState *self, enum WifiDirectP2pConnectState state)
{
    if (state == WIFI_DIRECT_P2P_CONNECTED) {
        CLOGI(LOG_LABEL "connected");
    } else if (state == WIFI_DIRECT_P2P_CONNECTING) {
        CLOGI(LOG_LABEL "connecting");
    } else if (state == WIFI_DIRECT_P2P_CONNECTION_FAIL) {
        CLOGI(LOG_LABEL "connect failed");
    }
}

/* constructor */
static void AvailableStateConstructor(struct P2pAvailableState *self)
{
    P2pEntityStateConstructor((struct P2pEntityState *)self);

    self->enter = Enter;
    self->exit = Exit;
    self->createServer = CreateServer;
    self->connect = Connect;
    self->removeLink = RemoveLink;
    self->destroyServer = DestroyServer;
    self->handleConnectionChange = HandleConnectionChange;
    self->handleConnectStateChange = HandleConnectStateChange;
    self->isInited = true;
}

static struct P2pAvailableState g_state = {
    .isInited = false,
    .name = "P2pEntityAvailableState",
};

/* class static method */
struct P2pAvailableState* GetP2pAvailableState(void)
{
    struct P2pAvailableState *self = (struct P2pAvailableState *)&g_state;
    if (!self->isInited) {
        AvailableStateConstructor(self);
    }

    return self;
}