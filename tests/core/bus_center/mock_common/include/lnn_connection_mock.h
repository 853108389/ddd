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

#ifndef LNN_CONNECTION_MOCK_H
#define LNN_CONNECTION_MOCK_H

#include <gmock/gmock.h>
#include <mutex>

#include "disc_interface.h"
#include "softbus_conn_interface.h"

namespace OHOS {
class LnnConnectInterface {
public:
    LnnConnectInterface() {};
    virtual ~LnnConnectInterface() {};

    virtual int32_t ConnGetConnectionInfo(uint32_t connectionId, ConnectionInfo *info) = 0;
    virtual int32_t ConnSetConnectCallback(ConnModule moduleId, const ConnectCallback *callback) = 0;
    virtual void ConnUnSetConnectCallback(ConnModule moduleId) = 0;
    virtual int32_t ConnConnectDevice(const ConnectOption *option, uint32_t requestId, const ConnectResult *result) = 0;
    virtual int32_t ConnDisconnectDevice(uint32_t connectionId) = 0;
    virtual uint32_t ConnGetHeadSize(void) = 0;
    virtual int32_t ConnPostBytes(uint32_t connectionId, ConnPostData *data) = 0;
    virtual bool CheckActiveConnection(const ConnectOption *option) = 0;
    virtual int32_t ConnStartLocalListening(const LocalListenerInfo *info) = 0;
    virtual int32_t ConnStopLocalListening(const LocalListenerInfo *info) = 0;
    virtual uint32_t ConnGetNewRequestId(ConnModule moduleId) = 0;
    virtual void DiscDeviceInfoChanged(InfoTypeChanged type) = 0;
};
class LnnConnectInterfaceMock : public LnnConnectInterface {
public:
    LnnConnectInterfaceMock();
    ~LnnConnectInterfaceMock() override;
    MOCK_METHOD2(ConnGetConnectionInfo, int32_t (uint32_t, ConnectionInfo *));
    MOCK_METHOD2(ConnSetConnectCallback, int32_t (ConnModule, const ConnectCallback *));
    MOCK_METHOD1(ConnUnSetConnectCallback, void (ConnModule));
    MOCK_METHOD3(ConnConnectDevice, int32_t (const ConnectOption *, uint32_t, const ConnectResult *));
    MOCK_METHOD1(ConnDisconnectDevice, int32_t (uint32_t));
    MOCK_METHOD0(ConnGetHeadSize, uint32_t (void));
    MOCK_METHOD2(ConnPostBytes, int32_t (uint32_t, ConnPostData *));
    MOCK_METHOD1(CheckActiveConnection, bool (const ConnectOption *));
    MOCK_METHOD1(ConnStartLocalListening, int32_t (const LocalListenerInfo *));
    MOCK_METHOD1(ConnStopLocalListening, int32_t (const LocalListenerInfo *));
    MOCK_METHOD1(ConnGetNewRequestId, uint32_t (ConnModule));
    MOCK_METHOD1(DiscDeviceInfoChanged, void (InfoTypeChanged));
};
} // namespace OHOS
#endif // AUTH_CONNECTION_MOCK_H