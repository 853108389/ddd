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

#include "lnn_ohos_account_adapter.h"
#include "ohos_account_kits.h"
#include "securec.h"
#include "softbus_errcode.h"
#include "softbus_log.h"

#define ACCOUNT_STRTOLL_BASE 10

int32_t GetOsAccountId(char *id, uint32_t idLen, uint32_t *len)
{
    if (id == nullptr || len == nullptr || idLen == 0) {
        LLOGE("invalid parameter");
        return SOFTBUS_INVALID_PARAM;
    }

    auto accountInfo = OHOS::AccountSA::OhosAccountKits::GetInstance().QueryOhosAccountInfo();
    if (!accountInfo.first) {
        LLOGE("QueryOhosAccountInfo failed");
        return SOFTBUS_ERR;
    }

    if (accountInfo.second.uid_.empty()) {
        LLOGE("accountInfo uid is empty");
        return SOFTBUS_ERR;
    }

    *len = accountInfo.second.uid_.length();
    LLOGI("uid:%s len:%d", accountInfo.second.uid_.c_str(), *len);

    if (memcpy_s(id, idLen, accountInfo.second.uid_.c_str(), *len) != EOK) {
        LLOGE("memcpy_s uid failed, idLen:%d len:%d", idLen, *len);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int64_t GetCurrentAccount(void)
{
    auto accountInfo = OHOS::AccountSA::OhosAccountKits::GetInstance().QueryOhosAccountInfo();
    if (!accountInfo.first) {
        LLOGE("QueryOhosAccountInfo failed");
        return 0;
    }

    if (accountInfo.second.name_.empty()) {
        LLOGE("accountInfo name_ is empty");
        return 0;
    }

    LLOGI("name_:%s", accountInfo.second.name_.c_str());
    int64_t account = strtoll(accountInfo.second.name_.c_str(), nullptr, ACCOUNT_STRTOLL_BASE);
    if (account == 0) {
        LLOGE("strtoll failed");
    }

    return account;
}