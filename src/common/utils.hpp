/*************************************************************************
 * Copyright (C) [2022] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#ifndef EASYDK_COMMON_UTILS_HPP_
#define EASYDK_COMMON_UTILS_HPP_

#include <string>

#include "cn_api.h"
#include "cncv.h"
#include "cnrt.h"

#define _SAFECALL(func, expected, msg, ret_val)                                                     \
  do {                                                                                              \
    int _ret = (func);                                                                              \
    if ((expected) != _ret) {                                                                       \
      LOG(ERROR) << "[EasyDK] Call [" << #func << "] failed, ret = " << _ret << ". " << msg;        \
      return (ret_val);                                                                             \
    }                                                                                               \
  } while (0)

#define CNDRV_SAFECALL(func, msg, ret_val) _SAFECALL(func, CN_SUCCESS, msg, ret_val)
#define CNRT_SAFECALL(func, msg, ret_val) _SAFECALL(func, cnrtSuccess, msg, ret_val)
#define CNCV_SAFECALL(func, msg, ret_val) _SAFECALL(func, CNCV_STATUS_SUCCESS, msg, ret_val)

#define _CALLFUNC(func, expected, msg)                                                     \
  do {                                                                                              \
    int _ret = (func);                                                                              \
    if ((expected) != _ret) {                                                                       \
      LOG(ERROR) << "[EasyDK] Call [" << #func << "] failed, ret = " << _ret << ". " << msg;        \
    }                                                                                               \
  } while (0)

#define CALL_CNRT_FUNC(func, msg) _CALLFUNC(func, cnrtSuccess, msg)
#define CALL_CNCV_FUNC(func, msg) _CALLFUNC(func, CNCV_STATUS_SUCCESS, msg)


namespace cnedk {

bool IsEdgePlatform(int device_id);
bool IsEdgePlatform(const std::string& platform_name);
bool IsCloudPlatform(int device_id);
bool IsCloudPlatform(const std::string& platform_name);

}  // namespace cnedk

#endif  // EASYDK_COMMON_UTILS_HPP_
