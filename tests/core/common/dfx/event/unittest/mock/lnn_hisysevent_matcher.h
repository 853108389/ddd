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

#ifndef LNN_HISYSEVENT_MATCHER_H
#define LNN_HISYSEVENT_MATCHER_H

#include <gmock/gmock.h>

#include "convert/lnn_event_converter.h"
#include "hisysevent_c.h"
#include "softbus_event.h"

MATCHER_P2(LnnValidParamArrayMatcher, inExtra, validSize, "lnn valid param array match fail")
{
    const auto *params = static_cast<const HiSysEventParam *>(arg);
    params += SOFTBUS_ASSIGNER_SIZE; // Skip softbus params, they are matched by SoftbusParamArrayMatcher
    auto extra = static_cast<LnnEventExtra>(inExtra);
    int32_t index = 0;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.result);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.errcode);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.connectionId);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.authType);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.authId);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.lnnType);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.onlineNum);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_EQ(params[index].v.i32, extra.peerDeviceAbility);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerDeviceInfo);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerIp);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerBrMac);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerBleMac);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerWifiMac);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerPort);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerUdid);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerNetworkId);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.peerDeviceType);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.callerPkg);
    ++index;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[index].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[index].type);
    EXPECT_STREQ(params[index].v.s, extra.calleePkg);

    EXPECT_EQ(++index, validSize);
    return true;
}

MATCHER_P2(LnnInvalidParamArrayMatcher, inExtra, validSize, "lnn invalid param array match fail")
{
    const auto *params = static_cast<const HiSysEventParam *>(arg);
    params += SOFTBUS_ASSIGNER_SIZE; // Skip softbus params, they are matched by SoftbusParamArrayMatcher
    auto extra = static_cast<LnnEventExtra>(inExtra);
    int32_t index = 0;
    int32_t errcodeIndex = 1;
    EXPECT_STREQ(params[index].name, g_lnnAssigners[errcodeIndex].name);
    EXPECT_EQ(params[index].t, g_lnnAssigners[errcodeIndex].type);
    EXPECT_EQ(params[index].v.i32, extra.errcode);
    EXPECT_EQ(++index, validSize);
    return true;
}

#endif // LNN_HISYSEVENT_MATCHER_H
