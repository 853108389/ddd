/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "hb_strategy_mock.h"
#include "softbus_error_code.h"

using namespace testing;
using namespace testing::ext;

namespace OHOS {
void *g_hbStrategyInterface;
HeartBeatStategyInterfaceMock::HeartBeatStategyInterfaceMock()
{
    g_hbStrategyInterface = reinterpret_cast<void *>(this);
}

HeartBeatStategyInterfaceMock::~HeartBeatStategyInterfaceMock()
{
    g_hbStrategyInterface = nullptr;
}

static HeartBeatStategyInterface *HeartBeatStrategyInterface()
{
    return reinterpret_cast<HeartBeatStategyInterfaceMock *>(g_hbStrategyInterface);
}

extern "C" {
int32_t LnnStartOfflineTimingStrategy(const char *networkId, ConnectionAddrType addrType)
{
    return HeartBeatStrategyInterface()->LnnStartOfflineTimingStrategy(networkId, addrType);
}

int32_t LnnStopOfflineTimingStrategy(const char *networkId, ConnectionAddrType addrType)
{
    return HeartBeatStrategyInterface()->LnnStopOfflineTimingStrategy(networkId, addrType);
}

int32_t LnnNotifyDiscoveryDevice(const ConnectionAddr *addr, bool isNeedConnect)
{
    return HeartBeatStrategyInterface()->LnnNotifyDiscoveryDevice(addr, isNeedConnect);
}

int32_t LnnNotifyMasterElect(const char *networkId, const char *masterUdid, int32_t masterWeight)
{
    return HeartBeatStrategyInterface()->LnnNotifyMasterElect(networkId, masterUdid, masterWeight);
}

int32_t LnnSetHbAsMasterNodeState(bool isMasterNode)
{
    return HeartBeatStrategyInterface()->LnnSetHbAsMasterNodeState(isMasterNode);
}

int32_t LnnStartHbByTypeAndStrategy(LnnHeartbeatType hbType, LnnHeartbeatStrategyType strategyType, bool isRelay)
{
    return HeartBeatStrategyInterface()->LnnStartHbByTypeAndStrategy(hbType, strategyType, isRelay);
}
int32_t LnnRequestLeaveSpecific(const char *networkId, ConnectionAddrType addrType)
{
    return HeartBeatStrategyInterface()->LnnRequestLeaveSpecific(networkId, addrType);
}
int32_t AuthStartVerify(const AuthConnInfo *connInfo, uint32_t requestId,
    const AuthVerifyCallback *callback, bool isFastAuth)
{
    return GetNetLedgerInterface()->AuthStartVerify(connInfo, requestId, callback, isFastAuth);
}
}
} // namespace OHOS