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

#include "unpackauthdata_fuzzer.h"
#include <cstddef>
#include <cstring>
#include <securec.h>
#include "auth_connection.h"
#include "softbus_access_token_test.h"

namespace OHOS {
    bool UnpackAuthDataFuzzTest(const uint8_t* data, size_t size)
    {
        if (data == nullptr || size < sizeof(AuthDataHead)) {
            return false;
        }

        AuthDataHead head = *const_cast<AuthDataHead *>(reinterpret_cast<const AuthDataHead *>(data));
        SetAceessTokenPermission("AuthTest");
        UnpackAuthData(data + sizeof(AuthDataHead), size - sizeof(AuthDataHead), &head);
        return true;
    }
}

/* Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    /* Run your code on data */
    OHOS::UnpackAuthDataFuzzTest(data, size);
    return 0;
}