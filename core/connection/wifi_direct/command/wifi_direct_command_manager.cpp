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

#include "wifi_direct_command_manager.h"
#include <list>
#include <mutex>
#include "conn_log.h"
#include "wifi_direct_command.h"
#include "utils/wifi_direct_timer_list.h"

static std::list<struct WifiDirectCommand *> g_commandQueue;
static std::mutex g_mutex;
static constexpr int32_t WIFI_DIRECT_COMMAND_WAIT_TIMEOUT = 15000;

static void CommandTimeoutHandler(struct WifiDirectCommand *command)
{
    {
        std::lock_guard lockGuard(g_mutex);
        g_commandQueue.remove(command);
    }
    command->onTimeout(command);
    command->destructor(command);
}

static void EnqueueCommand(struct WifiDirectCommand *command)
{
    if (command->type == COMMAND_TYPE_CONNECT || command->type == COMMAND_TYPE_DISCONNECT) {
        command->timerId = GetWifiDirectTimerList()->startTimer(reinterpret_cast<TimeoutHandler>(CommandTimeoutHandler),
            WIFI_DIRECT_COMMAND_WAIT_TIMEOUT, TIMER_FLAG_ONE_SHOOT, command);
    }
    std::lock_guard lockGuard(g_mutex);
    g_commandQueue.push_back(command);
}

static struct WifiDirectCommand* DequeueCommand()
{
    std::lock_guard lockGuard(g_mutex);
    if (g_commandQueue.empty()) {
        CONN_LOGI(CONN_WIFI_DIRECT, "queue empty");
        return nullptr;
    }
    struct WifiDirectCommand *command = g_commandQueue.front();
    g_commandQueue.pop_front();
    if (command->timerId >= 0) {
        GetWifiDirectTimerList()->stopTimer(command->timerId);
    }
    return command;
}

static struct WifiDirectCommandManager g_manager = {
    .enqueueCommand = EnqueueCommand,
    .dequeueCommand = DequeueCommand,
};

struct WifiDirectCommandManager* GetWifiDirectCommandManager(void)
{
    return &g_manager;
}