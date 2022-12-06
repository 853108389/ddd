/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "auth_common.h"
#include "auth_hichain.h"
#include "auth_interface.h"
#include "auth_manager.h"
#include "auth_net_ledger_mock.h"
#include "auth_request.h"
#include "lnn_connection_mock.h"
#include "lnn_hichain_mock.h"
#include "lnn_socket_mock.h"
#include "message_handler.h"
#include "softbus_access_token_test.h"
#include "softbus_adapter_mem.h"
#include "softbus_errcode.h"
#include "softbus_feature_config.h"
#include "softbus_log.h"
#include <cinttypes>
#include <gtest/gtest.h>
#include <securec.h>
#include <sys/time.h>

namespace OHOS {
using namespace testing;
using namespace testing::ext;

const AuthConnInfo connInfo = {
    .type = AUTH_LINK_TYPE_BR,
    .info.brInfo.brMac = "11:22:33:44:55:66",
    .peerUid = "002",
};
uint32_t g_requestId = 88;
const AuthVerifyCallback callback = {
    .onVerifyPassed = LnnConnectInterfaceMock::OnVerifyPassed,
    .onVerifyFailed = LnnConnectInterfaceMock::onVerifyFailed,
};
static const int MILLIS = 15;

class AuthTestEnhance : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void AuthTestEnhance::SetUpTestCase()
{
    SetAceessTokenPermission("AuthTestEnhance");
}

void AuthTestEnhance::TearDownTestCase() {}

void AuthTestEnhance::SetUp()
{
    LooperInit();
    LOG_INFO("AuthTest start.");
}

void AuthTestEnhance::TearDown()
{
    LooperDeinit();
}

void AuthInitMock(LnnConnectInterfaceMock &connMock, LnnHichainInterfaceMock &hichainMock, GroupAuthManager authManager,
    DeviceGroupManager groupManager)
{
    groupManager.regDataChangeListener = LnnHichainInterfaceMock::InvokeDataChangeListener;
    authManager.authDevice = LnnHichainInterfaceMock::InvokeAuthDevice;
    groupManager.unRegDataChangeListener = LnnHichainInterfaceMock::ActionofunRegDataChangeListener;
    ON_CALL(connMock, ConnSetConnectCallback(_, _)).WillByDefault(Return(SOFTBUS_OK));
    ON_CALL(hichainMock, InitDeviceAuthService()).WillByDefault(Return(0));
    ON_CALL(hichainMock, GetGaInstance()).WillByDefault(Return(&authManager));
    ON_CALL(hichainMock, GetGmInstance()).WillByDefault(Return(&groupManager));
}

/*
 * @tc.name: AUTH_START_LISTENING_Test_001
 * @tc.desc: auth start listening
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(AuthTestEnhance, AUTH_START_LISTENING_Test_001, TestSize.Level0)
{
    LnnConnectInterfaceMock connMock;
    {
        EXPECT_CALL(connMock, ConnStartLocalListening(_)).WillRepeatedly(Return(SOFTBUS_OK));
        int32_t port = 5566;
        int32_t ret = AuthStartListening(AUTH_LINK_TYPE_P2P, "192.168.78.1", port);
        printf("ret %d\n", ret);
        EXPECT_TRUE(ret == SOFTBUS_OK);
    }
    {
        EXPECT_CALL(connMock, ConnStartLocalListening(_)).WillRepeatedly(Return(SOFTBUS_ERR));
        int32_t port = 5566;
        int32_t ret = AuthStartListening(AUTH_LINK_TYPE_P2P, "192.168.78.1", port);
        printf("ret %d\n", ret);
        EXPECT_TRUE(ret == SOFTBUS_ERR);
    }
}

/*
 * @tc.name: AUTH_HICHAIN_START_AUTH_Test_001
 * @tc.desc: hichain start auth
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(AuthTestEnhance, AUTH_HICHAIN_START_AUTH_Test_001, TestSize.Level0)
{
    const char *udid = "1111222233334444";
    const char *uid = "8888";
    int64_t authSeq = 5678;
    LnnHichainInterfaceMock hichainMock;
    GroupAuthManager authManager;
    authManager.authDevice = LnnHichainInterfaceMock::InvokeAuthDevice;
    EXPECT_CALL(hichainMock, InitDeviceAuthService()).WillRepeatedly(Return(0));
    EXPECT_CALL(hichainMock, GetGaInstance()).WillRepeatedly(Return(&authManager));
    int32_t ret = HichainStartAuth(authSeq, udid, uid);
    EXPECT_TRUE(ret == SOFTBUS_OK);
}

/*
 * @tc.name: AUTH_HICHAIN_GET_JOINED_GROUPS_Test_001
 * @tc.desc: hichain get joined group
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(AuthTestEnhance, AUTH_HICHAIN_GET_JOINED_GROUPS_Test_001, TestSize.Level1)
{
    LnnHichainInterfaceMock hichainMock;
    DeviceGroupManager groupManager;
    EXPECT_CALL(hichainMock, GetGmInstance()).WillOnce(ReturnNull()).WillOnce(ReturnNull());
    EXPECT_TRUE(!AuthHasTrustedRelation());
    groupManager.getJoinedGroups = LnnHichainInterfaceMock::InvokeGetJoinedGroups1;
    EXPECT_CALL(hichainMock, GetGmInstance()).WillRepeatedly(Return(&groupManager));
    EXPECT_TRUE(AuthHasTrustedRelation());
    groupManager.getJoinedGroups = LnnHichainInterfaceMock::InvokeGetJoinedGroups2;
    EXPECT_TRUE(!AuthHasTrustedRelation());
}

/*
 * @tc.name: AUTH_INIT_Test_001
 * @tc.desc: auth init
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(AuthTestEnhance, AUTH_INIT_Test_001, TestSize.Level0)
{
    NiceMock<LnnConnectInterfaceMock> connMock;
    NiceMock<LnnHichainInterfaceMock> hichainMock;
    GroupAuthManager authManager;
    DeviceGroupManager groupManager;
    AuthInitMock(connMock, hichainMock, authManager, groupManager);
    int32_t ret = AuthInit();
    EXPECT_TRUE(ret == SOFTBUS_OK);
}

/*
 * @tc.name: AUTH_START_VERIFY_Test_001
 * @tc.desc: client auth start verify
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(AuthTestEnhance, CLINET_AUTH_START_VERIFY_Test_001, TestSize.Level1)
{
    NiceMock<LnnConnectInterfaceMock> connMock;
    NiceMock<LnnHichainInterfaceMock> hichainMock;
    LnnSocketInterfaceMock socketMock;
    GroupAuthManager authManager;
    DeviceGroupManager groupManager;
    AuthInitMock(connMock, hichainMock, authManager, groupManager);
    int32_t ret = AuthInit();
    EXPECT_CALL(connMock, ConnConnectDevice(_, _, _)).WillRepeatedly(Return(SOFTBUS_OK));
    EXPECT_CALL(socketMock, ConnOpenClientSocket(_, _, _)).WillRepeatedly(Return(SOFTBUS_OK));
    ret = AuthStartVerify(&connInfo, g_requestId, &callback);
    SoftBusSleepMs(MILLIS);
    EXPECT_TRUE(ret == SOFTBUS_OK);
    AuthDeviceDeinit();
}

/*
 * @tc.name: AUTH_START_VERIFY_Test_002
 * @tc.desc: client auth start verify sucess callback
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(AuthTestEnhance, CLINET_AUTH_START_VERIFY_Test_002, TestSize.Level1)
{
    NiceMock<LnnConnectInterfaceMock> connMock;
    NiceMock<LnnHichainInterfaceMock> hichainMock;
    AuthNetLedgertInterfaceMock ledgermock;
    LnnSocketInterfaceMock socketMock;
    GroupAuthManager authManager;
    DeviceGroupManager groupManager;
    AuthInitMock(connMock, hichainMock, authManager, groupManager);
    EXPECT_CALL(connMock, ConnSetConnectCallback(_, _))
        .WillRepeatedly(LnnConnectInterfaceMock::ActionofConnSetConnectCallback);
    EXPECT_CALL(ledgermock, LnnGetLocalStrInfo).WillRepeatedly(Return(SOFTBUS_OK));
    int32_t ret = AuthInit();
    EXPECT_TRUE(ret == SOFTBUS_OK);
    EXPECT_CALL(connMock, ConnConnectDevice(_, _, NotNull()))
        .WillRepeatedly(LnnConnectInterfaceMock::ActionofOnConnectSuccessed);
    EXPECT_CALL(connMock, ConnPostBytes).WillRepeatedly(Return(SOFTBUS_OK));
    EXPECT_CALL(socketMock, ConnOpenClientSocket(_, _, _)).WillRepeatedly(Return(SOFTBUS_OK));
    ret = AuthStartVerify(&connInfo, g_requestId, &callback);
    EXPECT_TRUE(ret == SOFTBUS_OK);
    SoftBusSleepMs(MILLIS);
    AuthDeviceDeinit();
}

/*
 * @tc.name: POST_DEVICEID_001
 * @tc.desc: client auth start verify failed callback
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(AuthTestEnhance, CLINET_CONN_FAILED_001, TestSize.Level1)
{
    NiceMock<LnnConnectInterfaceMock> connMock;
    NiceMock<LnnHichainInterfaceMock> hichainMock;
    AuthNetLedgertInterfaceMock ledgermock;
    LnnSocketInterfaceMock socketMock;
    GroupAuthManager authManager;
    DeviceGroupManager groupManager;
    AuthInitMock(connMock, hichainMock, authManager, groupManager);
    EXPECT_CALL(connMock, ConnSetConnectCallback(_, _))
        .WillRepeatedly(LnnConnectInterfaceMock::ActionofConnSetConnectCallback);
    EXPECT_CALL(ledgermock, LnnGetLocalStrInfo).WillRepeatedly(Return(SOFTBUS_OK));
    int32_t ret = AuthInit();
    EXPECT_TRUE(ret == SOFTBUS_OK);
    EXPECT_CALL(connMock, ConnConnectDevice(_, _, NotNull()))
        .WillRepeatedly(LnnConnectInterfaceMock::ActionofOnConnectFailed);
    EXPECT_CALL(connMock, ConnPostBytes).WillRepeatedly(Return(SOFTBUS_OK));
    EXPECT_CALL(socketMock, ConnOpenClientSocket(_, _, _)).WillRepeatedly(Return(SOFTBUS_OK));
    ret = AuthStartVerify(&connInfo, g_requestId, &callback);
    EXPECT_TRUE(ret == SOFTBUS_OK);
    SoftBusSleepMs(MILLIS);
    AuthDeviceDeinit();
}
} // namespace OHOS
