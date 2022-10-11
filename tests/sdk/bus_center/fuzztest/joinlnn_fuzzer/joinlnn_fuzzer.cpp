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

#include "joinlnn_fuzzer.h"
#include <cstddef>
#include <cstring>
#include <securec.h>
#include "softbus_bus_center.h"
#include "softbus_def.h"
#include "softbus_errcode.h"

namespace OHOS {
    void OnJoinLNNResult(ConnectionAddr *addr, const char *networkId, int32_t retCode)
    {
        (void)addr;
        (void)networkId;
        (void)retCode;
    }

    static ConnectionAddr addr;
    static const int32_t MAX_CONNECT_TYPE = CONNECTION_ADDR_MAX;
    static const char *IP = "192.168.43.16";
    static const int32_t port = 6007;

    void GenRandAddr(const uint8_t *data, size_t size)
    {
        addr.type = (ConnectionAddrType)(size % MAX_CONNECT_TYPE);
        memcpy_s(addr.peerUid, MAX_ACCOUNT_HASH_LEN, data, size);
        memcpy_s(addr.info.ip.ip, IP_STR_MAX_LEN, IP, strlen(IP));
        addr.info.ip.port = port + size;
    }


    bool JoinLnnFuzzerTest(const uint8_t* data, size_t size)
    {
        if (data == nullptr || size == 0) {
            return true;
        }

        char tmp[65] = {0};
        if (memcpy_s(tmp, sizeof(tmp) - 1, data, size) != EOK) {
            return true;
        }
        if (strnlen((const char *)tmp, PKG_NAME_SIZE_MAX) >= PKG_NAME_SIZE_MAX) {
            return true;
        }

        GenRandAddr(data, size);
        JoinLNN((const char *)tmp, &addr, OnJoinLNNResult);
        return true;
    }
}

/* Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    /* Run your code on data */
    OHOS::JoinLnnFuzzerTest(data, size);
    return 0;
}