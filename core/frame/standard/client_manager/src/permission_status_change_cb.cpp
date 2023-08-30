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

#include "permission_status_change_cb.h"
#include "trans_client_proxy.h"
#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_server_ipc_interface_code.h"
#include "message_parcel.h"

namespace OHOS {
void PermissionStatusChangeCb::PermStateChangeCallback(PermStateChangeInfo& result)
{
    SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "%{public}s changed.", result.permissionName.c_str());
    if (InformPermissionChange(result.permStateChangeType, this->pkgName.c_str(), pid) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_ERROR, "InformPermissionChange fail");
    }
}

void RegisterDataSyncPermission(const uint32_t& callingTokenId,
                                const std::string& permissionName, const std::string& pkgName, int32_t pid)
{
    PermStateChangeScope scopeInfo;
    scopeInfo.permList = {permissionName};
    scopeInfo.tokenIDs = {callingTokenId};
    std::shared_ptr<PermissionStatusChangeCb> callbackPtr_ =
        std::make_shared<PermissionStatusChangeCb>(scopeInfo, pkgName, pid);
    SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "after tokenId:%{public}d register", callingTokenId);
    if (AccessTokenKit::RegisterPermStateChangeCallback(callbackPtr_) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_ERROR, "RegisterPermStateChangeCallback failed.");
    }
}
} // namespace OHOS