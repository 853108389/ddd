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

#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

#include "hilog_mock.h"
#include "softbus_log.h"

using namespace std;
using namespace testing;
using namespace testing::ext;

namespace {
const char *TEST_LOG_DETAIL = "softbus test log";
const char *NSTACKX_TEST_MODULE_NAME = "nStackxTest";
enum {
    NSTACKX_LOG_LEVEL_FATAL = 1,
    NSTACKX_LOG_LEVEL_ERROR = 2,
    NSTACKX_LOG_LEVEL_WARNING = 3,
    NSTACKX_LOG_LEVEL_INFO = 4,
    NSTACKX_LOG_LEVEL_DEBUG = 5,
};
} // namespace

namespace OHOS {
class SoftBusLogTest : public testing::Test { };

/**
 * @tc.name: SoftBusLogTest001
 * @tc.desc: Test SoftBusLogLevel is consistent with LogLevel in hilog_c.h
 * @tc.type: FUNC
 * @tc.require: I8DW1W
 */
HWTEST_F(SoftBusLogTest, SoftBusLogTest001, TestSize.Level0)
{
    EXPECT_EQ(static_cast<int>(LOG_DEBUG), static_cast<int>(SOFTBUS_DFX_LOG_DEBUG));
    EXPECT_EQ(static_cast<int>(LOG_INFO), static_cast<int>(SOFTBUS_DFX_LOG_INFO));
    EXPECT_EQ(static_cast<int>(LOG_WARN), static_cast<int>(SOFTBUS_DFX_LOG_WARN));
    EXPECT_EQ(static_cast<int>(LOG_ERROR), static_cast<int>(SOFTBUS_DFX_LOG_ERROR));
    EXPECT_EQ(static_cast<int>(LOG_FATAL), static_cast<int>(SOFTBUS_DFX_LOG_FATAL));
}

/**
 * @tc.name: SoftBusLogTest002
 * @tc.desc: Test SOFTBUS_LOG_INNER macro
 * @tc.type: FUNC
 * @tc.require: I8DW1W
 */
HWTEST_F(SoftBusLogTest, SoftBusLogTest002, TestSize.Level0)
{
    SoftBusDfxLogLevel level = SOFTBUS_DFX_LOG_INFO;
    SoftBusLogLabel label = {
        .domain = DOMAIN_ID_TEST,
        .tag = "SoftBusTest",
    };

    HilogMock mock;
    EXPECT_CALL(mock,
        HiLogPrint(Eq(LOG_CORE), Eq(static_cast<LogLevel>(level)), Eq(label.domain), StrEq(label.tag),
            StrEq("%{public}s"), EndsWith(TEST_LOG_DETAIL)))
        .Times(1);
    SOFTBUS_LOG_INNER(level, label, TEST_LOG_DETAIL);
}

/**
 * @tc.name: SoftBusLogTest003
 * @tc.desc: Test NstackxLogInnerImpl
 * @tc.type: FUNC
 * @tc.require: I8DW1W
 */
HWTEST_F(SoftBusLogTest, SoftBusLogTest003, TestSize.Level0)
{
    const char *moduleName = NSTACKX_TEST_MODULE_NAME;

    // Test for debug level
    uint32_t logLevel = NSTACKX_LOG_LEVEL_DEBUG;
    SoftBusDfxLogLevel expectLevel = SOFTBUS_DFX_LOG_DEBUG;
    HilogMock mock;
    EXPECT_CALL(mock,
        HiLogPrint(Eq(LOG_CORE), Eq(static_cast<LogLevel>(expectLevel)), Eq(NSTACKX_LOG_DOMAIN), StrEq(moduleName),
            StrEq("%{public}s"), EndsWith(TEST_LOG_DETAIL)))
        .Times(1);
    NstackxLogInnerImpl(moduleName, logLevel, TEST_LOG_DETAIL);

    // Test for info level
    logLevel = NSTACKX_LOG_LEVEL_INFO;
    expectLevel = SOFTBUS_DFX_LOG_INFO;
    EXPECT_CALL(mock,
        HiLogPrint(Eq(LOG_CORE), Eq(static_cast<LogLevel>(expectLevel)), Eq(NSTACKX_LOG_DOMAIN), StrEq(moduleName),
            StrEq("%{public}s"), EndsWith(TEST_LOG_DETAIL)))
        .Times(1);
    NstackxLogInnerImpl(moduleName, logLevel, TEST_LOG_DETAIL);

    // Test for warn level
    logLevel = NSTACKX_LOG_LEVEL_WARNING;
    expectLevel = SOFTBUS_DFX_LOG_WARN;
    EXPECT_CALL(mock,
        HiLogPrint(Eq(LOG_CORE), Eq(static_cast<LogLevel>(expectLevel)), Eq(NSTACKX_LOG_DOMAIN), StrEq(moduleName),
            StrEq("%{public}s"), EndsWith(TEST_LOG_DETAIL)))
        .Times(1);
    NstackxLogInnerImpl(moduleName, logLevel, TEST_LOG_DETAIL);

    // Test for error level
    logLevel = NSTACKX_LOG_LEVEL_ERROR;
    expectLevel = SOFTBUS_DFX_LOG_ERROR;
    EXPECT_CALL(mock,
        HiLogPrint(Eq(LOG_CORE), Eq(static_cast<LogLevel>(expectLevel)), Eq(NSTACKX_LOG_DOMAIN), StrEq(moduleName),
            StrEq("%{public}s"), EndsWith(TEST_LOG_DETAIL)))
        .Times(1);
    NstackxLogInnerImpl(moduleName, logLevel, TEST_LOG_DETAIL);

    // Test for fatal level
    logLevel = NSTACKX_LOG_LEVEL_FATAL;
    expectLevel = SOFTBUS_DFX_LOG_FATAL;
    EXPECT_CALL(mock,
        HiLogPrint(Eq(LOG_CORE), Eq(static_cast<LogLevel>(expectLevel)), Eq(NSTACKX_LOG_DOMAIN), StrEq(moduleName),
            StrEq("%{public}s"), EndsWith(TEST_LOG_DETAIL)))
        .Times(1);
    NstackxLogInnerImpl(moduleName, logLevel, TEST_LOG_DETAIL);
}
} // namespace OHOS