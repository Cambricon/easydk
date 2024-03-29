/*************************************************************************
 * Copyright (C) [2020] by Cambricon, Inc. All rights reserved
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

#include <glog/logging.h>

#include "cnis/infer_server.h"
#include "cnrt.h"

#define CNRT_SAFECALL(func, val)                                                       \
  do {                                                                                 \
    auto ret = (func);                                                                 \
    if (ret != cnrtSuccess) {                                                          \
      LOG(ERROR) << "[EasyDK InferServer] Call " #func " failed, ret = " << ret; \
      return val;                                                                      \
    }                                                                                  \
  } while (0)

namespace infer_server {

bool SetCurrentDevice(int device_id) noexcept {
  CNRT_SAFECALL(cnrtSetDevice(device_id), false);
  VLOG(3) << "[EasyDK InferServer] SetCurrentDevice(): Set device [" << device_id << "] for this thread";
  return true;
}

uint32_t TotalDeviceCount() noexcept {
  uint32_t dev_cnt;
  CNRT_SAFECALL(cnrtGetDeviceCount(&dev_cnt), 0);
  return dev_cnt;
}

bool CheckDevice(int device_id) noexcept {
  uint32_t dev_cnt;
  CNRT_SAFECALL(cnrtGetDeviceCount(&dev_cnt), false);
  return device_id < static_cast<int>(dev_cnt) && device_id >= 0;
}

}  // namespace infer_server
