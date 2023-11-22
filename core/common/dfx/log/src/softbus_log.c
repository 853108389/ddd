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

#include "softbus_log.h"

#include <securec.h>
#include <string.h>

#if defined(__LITEOS_M__)
#define SOFTBUS_PRINTF
#include "log.h"
#else
#include "hilog/log.h"
#endif

#define NSTACKX_LOG_CONVERT_BASE 8

static void SoftBusLogExtraInfoFormat(char **str, const char *fileName, int lineNum, const char *funName)
{
    (void)sprintf_s(*str, LOG_LINE_MAX_LENGTH + 1, "[%s:%d] %s# ", fileName, lineNum, funName);
}

static void SoftBusLogPrint(const char *buf, SoftBusDfxLogLevel level, unsigned int domain, const char *tag)
{
#ifdef SOFTBUS_PRINTF
    (void)level;
    (void)domain;
    (void)tag;
    printf("%s\n", buf);
#else
    (void)HiLogPrint(LOG_CORE, (LogLevel)level, domain, tag, "%{public}s", buf);
#endif
}

void SoftBusLogInnerImpl(SoftBusDfxLogLevel level, SoftBusLogLabel label, const char *fileName, int lineNum,
    const char *funName, const char *fmt, ...)
{
    uint32_t pos;
    va_list args;
    char *str = (char *)malloc(LOG_LINE_MAX_LENGTH + 1);
    if (str == NULL) {
        return; // Do not print log here
    }
    SoftBusLogExtraInfoFormat(&str, fileName, lineNum, funName);
    pos = strlen(str);
    if (memset_s(&args, sizeof(va_list), 0, sizeof(va_list)) != EOK) {
        free(str);
        return; // Do not print log here
    }
    va_start(args, fmt);
    (void)vsprintf_s(&str[pos], LOG_LINE_MAX_LENGTH + 1, fmt, args);
    va_end(args);
    SoftBusLogPrint(str, level, label.domain, label.tag);
    free(str);
}

void NstackxLogInnerImpl(const char *moduleName, uint32_t logLevel, const char *fmt, ...)
{
    SoftBusDfxLogLevel level = NSTACKX_LOG_CONVERT_BASE - logLevel;
    va_list args;
    char *str = (char *)malloc(LOG_LINE_MAX_LENGTH + 1);
    if (str == NULL) {
        return; // Do not print log here
    }
    if (memset_s(&args, sizeof(va_list), 0, sizeof(va_list)) != EOK) {
        free(str);
        return; // Do not print log here
    }
    va_start(args, fmt);
    (void)vsprintf_s(str, LOG_LINE_MAX_LENGTH + 1, fmt, args);
    va_end(args);
    SoftBusLogPrint(str, level, NSTACKX_LOG_DOMAIN, moduleName);
    free(str);
}