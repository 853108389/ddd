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

#include "trans_channel_limit.h"

#include <securec.h>

#include "anonymizer.h"
#include "softbus_def.h"
#include "trans_log.h"


#define AUTH_SESSION_WHITE_LIST_NUM (8)
static char g_sessionWhiteList[AUTH_SESSION_WHITE_LIST_NUM][SESSION_NAME_SIZE_MAX] = {
    "ohos.distributedhardware.devicemanager.resident",
    "com.huawei.devicegroupmanage",
    "IShareAuthSession",
    "com.huawei.devicemanager.resident",
    "com.huawei.plrdtest.dsoftbus",
    "com.huawei.android.airsharing+CastPlusDiscoveryModule",
    "com.huawei.dmsdp+dmsdp",
    "com.huawei.devicemanager.dynamic",
};

#define NO_PKG_NAME_SESSION_WHITE_LIST_NUM (1)
static char g_noPkgNameSessionWhiteList[NO_PKG_NAME_SESSION_WHITE_LIST_NUM][SESSION_NAME_SIZE_MAX] = {
    "com.huawei.devicemanager.resident",
};

bool CheckSessionNameValidOnAuthChannel(const char *sessionName)
{
    if (sessionName == NULL) {
        return false;
    }

    uint16_t index = 0;
    size_t len = 0;
    for (; index < AUTH_SESSION_WHITE_LIST_NUM; ++index) {
        len = strnlen(g_sessionWhiteList[index], SESSION_NAME_SIZE_MAX);
        if (strncmp(sessionName, g_sessionWhiteList[index], len) == 0) {
            return true;
        }
    }
    char *tmpName = NULL;
    Anonymize(sessionName, &tmpName);
    TRANS_LOGE(TRANS_CTRL,
        "auth channel sessionName invalid. sessionName=%{public}s", tmpName);
    AnonymizeFree(tmpName);
    return false;
}

bool IsNoPkgNameSession(const char *sessionName)
{
    if (sessionName == NULL) {
        return false;
    }

    uint16_t index = 0;
    size_t len = 0;
    for (; index < NO_PKG_NAME_SESSION_WHITE_LIST_NUM; ++index) {
        len = strnlen(g_noPkgNameSessionWhiteList[index], SESSION_NAME_SIZE_MAX);
        if (strncmp(sessionName, g_noPkgNameSessionWhiteList[index], len) == 0) {
            return true;
        }
    }

    return false;
}
