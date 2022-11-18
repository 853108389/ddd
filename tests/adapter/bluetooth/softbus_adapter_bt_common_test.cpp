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

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "softbus_adapter_bt_common.h"
#include "softbus_errcode.h"
#include "softbus_log.h"

#include "bluetooth_mock.h"

using namespace testing::ext;
using ::testing::Return;
using ::testing::_;
using ::testing::AtMost;

namespace OHOS {

/**
 * @tc.name: AdapterBtCommonTest_ConvertStatus
 * @tc.desc: test enable bt
 * @tc.type: FUNC
 * @tc.require: NONE
 */
HWTEST(AdapterBtCommonTest, SoftBusEnableBt, TestSize.Level3)
{
    MockBluetoothCommonn mocker;
    EXPECT_CALL(mocker, EnableBle())
        .Times(2)
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_EQ(SoftBusEnableBt(), SOFTBUS_OK);
    EXPECT_EQ(SoftBusEnableBt(), SOFTBUS_ERR);
}

/**
 * @tc.name: AdapterBtCommonTest_SoftBusDisableBt
 * @tc.desc: test disable bt
 * @tc.type: FUNC
 * @tc.require: NONE
 */
HWTEST(AdapterBtCommonTest, SoftBusDisableBt, TestSize.Level3)
{
    MockBluetoothCommonn mocker;
    EXPECT_CALL(mocker, DisableBle())
        .Times(2)
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_EQ(SoftBusDisableBt(), SOFTBUS_OK);
    EXPECT_EQ(SoftBusDisableBt(), SOFTBUS_ERR);
}

/**
 * @tc.name: AdapterBtCommonTest_SoftBusGetBtState
 * @tc.desc: test get bt status
 * @tc.type: FUNC
 * @tc.require: NONE
 */
HWTEST(AdapterBtCommonTest, SoftBusGetBtState, TestSize.Level3)
{
    MockBluetoothCommonn mocker;
    EXPECT_CALL(mocker, IsBleEnabled())
        .Times(2)
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_EQ(SoftBusGetBtState(), BLE_ENABLE);
    EXPECT_EQ(SoftBusGetBtState(), BLE_DISABLE);
}

/**
 * @tc.name: AdapterBtCommonTest_SoftBusGetBtMacAddr
 * @tc.desc: test get bt mac address
 * @tc.type: FUNC
 * @tc.require: NONE
 */
HWTEST(AdapterBtCommonTest, SoftBusGetBtMacAddr, TestSize.Level3)
{
    EXPECT_EQ(SoftBusGetBtMacAddr(NULL), SOFTBUS_ERR);
    MockBluetoothCommonn mocker;
    SoftBusBtAddr mac = {0};
    EXPECT_CALL(mocker, GetLocalAddr(mac.addr, BT_ADDR_LEN))
        .Times(2)
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_EQ(SoftBusGetBtMacAddr(&mac), SOFTBUS_OK);
    EXPECT_EQ(SoftBusGetBtMacAddr(&mac), SOFTBUS_ERR);
}

/**
 * @tc.name: AdapterBtCommonTest_SoftBusSetBtName
 * @tc.desc: test set bt name
 * @tc.type: FUNC
 * @tc.require: NONE
 */
HWTEST(AdapterBtCommonTest, SoftBusSetBtName, TestSize.Level3)
{
    MockBluetoothCommonn mocker;
    const char *name = "awesome";
    EXPECT_CALL(mocker, SetLocalName((unsigned char *)name, (unsigned char)strlen(name)))
        .Times(2)
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_EQ(SoftBusSetBtName(name), SOFTBUS_OK);
    EXPECT_EQ(SoftBusSetBtName(name), SOFTBUS_ERR);
}

static testing::AssertionResult PrepareBtStateListener(
    MockBluetoothCommonn &mocker, int *outlistenerId, BtGapCallBacks **outcallback)
{
    EXPECT_CALL(mocker, GapRegisterCallbacks(_))
        .Times(AtMost(1))
        .WillOnce(Return(OHOS_BT_STATUS_SUCCESS));
    auto listenerId = SoftBusAddBtStateListener(MockBluetoothCommonn::GetMockBtStateListener());
    if (listenerId == SOFTBUS_ERR) {
        return testing::AssertionFailure() << "SoftBusAddBtStateListener failed";
    }
    auto callback = MockBluetoothCommonn::GetBtGapCallBacks();
    if (callback == nullptr) {
        return testing::AssertionFailure() << "GetBtGapCallBacks failed";
    }
    *outlistenerId = listenerId;
    *outcallback = callback;
    return testing::AssertionSuccess();
}

/**
 * @tc.name: AdapterBtCommonTest_SoftBusAddBtStateListener
 * @tc.desc: test set bt name
 * @tc.type: FUNC
 * @tc.require: NONE
 */
HWTEST(AdapterBtCommonTest, SoftBusAddBtStateListener, TestSize.Level3)
{
    BtGapCallBacks *callback = nullptr;
    int registeredListenerId = -1;
    MockBluetoothCommonn mocker;
    auto prepareResult = PrepareBtStateListener(mocker, &registeredListenerId, &callback);
    ASSERT_TRUE(prepareResult);
    callback->stateChangeCallback(OHOS_BT_TRANSPORT_BR_EDR, OHOS_GAP_STATE_TURNING_ON);
    auto btStateResult = MockBluetoothCommonn::ExpectOnBtStateChanged(registeredListenerId,
        SOFTBUS_BR_STATE_TURNING_ON);
    EXPECT_TRUE(btStateResult);

    BdAddr bdAddr = {
        .addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
    };
    callback->aclStateChangedCallbak(&bdAddr, OHOS_GAP_ACL_STATE_CONNECTED, 0);
    SoftBusBtAddr addr = {
        .addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
    };
    auto aclStateResult = MockBluetoothCommonn::ExpectOnBtAclStateChanged(registeredListenerId, addr,
        SOFTBUS_ACL_STATE_CONNECTED);
    EXPECT_TRUE(aclStateResult);
}

/**
 * @tc.name: AdapterBtCommonTest_BluetoothPair
 * @tc.desc: test br pair
 * @tc.type: FUNC
 * @tc.require: NONE
 */
HWTEST(AdapterBtCommonTest, BluetoothPair, TestSize.Level3)
{
    BdAddr bdAddr = {
        .addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
    };
    MockBluetoothCommonn mocker;
    EXPECT_CALL(mocker, PairRequestReply(&bdAddr, OHOS_BT_TRANSPORT_LE, true))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(mocker, SetDevicePairingConfirmation(&bdAddr, OHOS_BT_TRANSPORT_LE, true))
        .Times(1)
        .WillOnce(Return(true));
    BtGapCallBacks *callback = nullptr;
    int ignore = -1;
    auto prepareResult = PrepareBtStateListener(mocker, &ignore, &callback);
    ASSERT_TRUE(prepareResult);
    callback->pairRequestedCallback(&bdAddr, OHOS_BT_TRANSPORT_LE);
    callback->pairConfiremedCallback(&bdAddr, OHOS_BT_TRANSPORT_LE, 0, 0);
}

}