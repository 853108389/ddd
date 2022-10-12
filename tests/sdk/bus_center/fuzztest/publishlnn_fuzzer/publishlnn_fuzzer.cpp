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

#include "publishlnn_fuzzer.h"
#include <cstddef>
#include <securec.h>
#include "softbus_bus_center.h"
#include "softbus_errcode.h"

namespace OHOS {
    static const int32_t MAX_SIZE_DISCOVER_MODE = 2;
    static const int32_t MAX_SIZE_EXCHANGE_MEDIUM = MEDIUM_BUTT + 1;
    static const int32_t MAX_SIZE_EXCHANGE_FREQ = FREQ_BUTT + 1 ;
    static const int32_t MAX_SIZE_CAPABILITYMAP = OSD_CAPABILITY_BITMAP + 1;
    
    void OnPublishResult(int publishId, PublishResult reason)
    {
        (void)publishId;
        (void)reason;
    }

    static IPublishCb g_publishCb = {
        .OnPublishResult = OnPublishResult
    };

    static PublishInfo g_pInfo = {
        .publishId = 1,
        .mode = DISCOVER_MODE_ACTIVE,
        .medium = COAP,
        .freq = MID,
        .capability = "dvKit",
        .capabilityData = (unsigned char *)"capdata4",
        .dataLen = sizeof("capdata4")
    };

    static void GenRanPublishInfo(const uint8_t* data, size_t size)
    {
        g_pInfo.publishId = size;
        g_pInfo.mode = (size % MAX_SIZE_DISCOVER_MODE) ? DISCOVER_MODE_ACTIVE : DISCOVER_MODE_PASSIVE;
        g_pInfo.medium = (ExchangeMedium)(size % MAX_SIZE_EXCHANGE_MEDIUM);
        g_pInfo.freq = (ExchangeFreq)(size % MAX_SIZE_EXCHANGE_FREQ);
        g_pInfo.capability = g_capabilityMap[(DataBitMap)(size % MAX_SIZE_CAPABILITYMAP)].capability;
        g_pInfo.capabilityData = (unsigned char *)data;
        g_pInfo.dataLen = size;
    }

    bool DoSomethingInterestingWithMyAPI(const uint8_t* data, size_t size)
    {
        if (data == nullptr || size == 0) {
            return true;
        }
        GenRanPublishInfo(data, size);
        char tmp[65] = {0};
        if (memcpy_s(tmp, sizeof(tmp) - 1, data, size) != EOK) {
            return true;
        }
        int32_t ret = PublishLNN((const char *)tmp, &g_pInfo, &g_publishCb);
        if (ret == SOFTBUS_OK) {
            StopPublishLNN((const char *)data, g_pInfo.publishId);
        }
        return ret;
    }
}

/* Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    /* Run your code on data */
    OHOS::DoSomethingInterestingWithMyAPI(data, size);
    return 0;
}
