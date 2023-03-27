/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

#include "device/mlu_context.h"

#include <cnrt.h>
#include <glog/logging.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "internal/cnrt_wrap.h"
#include "internal/mlu_task_queue.h"

using std::string;
using std::to_string;

namespace edk {

MluTaskQueue::Mark MluTaskQueue::PlaceMark() {
  uint32_t idx = 0;
  for (; idx < priv_->marks_valid.size(); ++idx) {
    if (priv_->marks_valid[idx]) break;
  }

  constexpr uint32_t marks_max_num = 40;
  if (idx == priv_->marks_valid.size()) {
    if (priv_->marks.size() > marks_max_num) {
      THROW_EXCEPTION(Exception::UNAVAILABLE,
          "[EasyDK Device] [MluTaskQueue] The number of marks reaches up limit, please do not store marks anymore");
    }
    priv_->marks.emplace_back();
    priv_->marks_valid.push_back(true);
    VLOG(4) << "[EasyDK Device] [MluTaskQueue] add new TimeMark, total: " << priv_->marks.size();
  }

  priv_->marks[idx].Mark(priv_->queue);
  priv_->marks_valid[idx] = false;
  return MluTaskQueue::Mark([this](int id) { priv_->marks_valid[id] = true; }, idx);
}

float MluTaskQueue::Count(const MluTaskQueue::Mark& s, const MluTaskQueue::Mark& e) const {
  int start = s.Index(), end = e.Index();
  if (start < 0 || start >= static_cast<int>(priv_->marks.size()) ||
      end < 0 || end >= static_cast<int>(priv_->marks.size())) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "[EasyDK Device] [MluTaskQueue] Marks not exist");
  }
  if (priv_->marks_valid[start] || priv_->marks_valid[end]) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "[EasyDK Device] [MluTaskQueue] Marks has not been placed");
  }
  return TimeMark::Count(priv_->marks[start], priv_->marks[end]);
}

MluTaskQueue::MluTaskQueue() {
  priv_.reset(new MluTaskQueuePrivate);
}

std::shared_ptr<MluTaskQueue> MluTaskQueue::Create() {
  auto q = std::shared_ptr<MluTaskQueue>(new MluTaskQueue);
  VLOG(2) << "[EasyDK Device] [MluTaskQueue] Create cnrtQueue";
  CALL_CNRT_FUNC(cnrt::QueueCreate(&q->priv_->queue), "[EasyDK Device] [MluTaskQueue] Create cnrtQueue failed.");
  return q;
}

void MluTaskQueue::Sync() {
  CHECK(priv_->queue) << "[EasyDK Device] [MluTaskQueue] Task queue is uninitialized!";
  CALL_CNRT_FUNC(cnrt::QueueSync(priv_->queue), "[EasyDK Device] [MluTaskQueue] Sync queue failed.");
  VLOG(4) << "[EasyDK Device] [MluTaskQueue] Sync MLU task queue: " << reinterpret_cast<void*>(priv_->queue);
}

MluTaskQueuePrivate::~MluTaskQueuePrivate() {
  if (queue) {
    VLOG(2) << "[EasyDK Device] [MluTaskQueue] Destroy cnrtQueue";
    cnrtRet_t ret = cnrt::QueueDestroy(queue);
    queue = nullptr;
    if (ret != CNRT_RET_SUCCESS) {
      LOG(ERROR) << "[EasyDK Device] [MluTaskQueue] Destroy cnrtQueue failed, error code: " << ret;
    }
  }
}

namespace _cnrt_init_tool {
/**
 * @brief singleton for init cambricon runtime
 */
class CnrtInitTool {
 public:
  CnrtInitTool() : is_initialized_(false) {}

  ~CnrtInitTool() {
#if CNRT_MAJOR_VERSION < 5
    if (is_initialized_) {
      LOG(INFO) << "[EasyDK Device] [CnrtInitTool] Cambricon runtime destroy";
      cnrtDestroy();
    }
#endif
  }

  void Init() {
#if CNRT_MAJOR_VERSION < 5
    std::lock_guard<std::mutex> lk(lock_);
    if (!is_initialized_) {
      CALL_CNRT_FUNC(cnrtInit(0), "[EasyDK Device] [CnrtInitTool] Init cambricon runtime failed.");
      uint32_t dev_cnt;
      CALL_CNRT_FUNC(cnrtGetDeviceCount(&dev_cnt), "[EasyDK Device] [CnrtInitTool] Get device count failed.");
      if (static_cast<decltype(dev_cnt)>(0) == dev_cnt) {
        THROW_EXCEPTION(Exception::UNAVAILABLE, "[EasyDK Device] [CnrtInitTool] No device found.");
      }
      LOG(INFO) << "[EasyDK Device] [CnrtInitTool] Cambricon runtime init success.";
      is_initialized_ = true;
    }
#endif
  }

 private:
  std::atomic<bool> is_initialized_;
  std::mutex lock_;

  // disable copy and assign
  CnrtInitTool(const CnrtInitTool&) = delete;
  CnrtInitTool& operator=(const CnrtInitTool&) = delete;
};  // class CnrtInitTool
static CnrtInitTool cnrt_init_tool;
}  // namespace _cnrt_init_tool

bool MluContext::CheckDeviceId(int id) {
  _cnrt_init_tool::cnrt_init_tool.Init();
#if CNRT_MAJOR_VERSION < 5
  cnrtDev_t dev;
  return CNRT_RET_SUCCESS == cnrtGetDeviceHandle(&dev, id);
#else
  unsigned int count;
  cnrtGetDeviceCount(&count);
  if (id >= static_cast<int>(count) || id < 0) {
    LOG(ERROR) << "[EasyDK] GetDeviceInfo(): device id is invalid, device_id: " << id << ", total count: "
               << count;
    return false;
  }
  return true;
#endif
}

uint32_t MluContext::GetDeviceNum() {
  _cnrt_init_tool::cnrt_init_tool.Init();
  uint32_t dev_cnt;
  CALL_CNRT_FUNC(cnrtGetDeviceCount(&dev_cnt), "[EasyDK Device] [MluContext] Get device count failed.");
  return dev_cnt;
}

void MluContext::BindDevice() {
  _cnrt_init_tool::cnrt_init_tool.Init();
#if CNRT_MAJOR_VERSION < 5
  cnrtDev_t dev;
  CALL_CNRT_FUNC(cnrtGetDeviceHandle(&dev, dev_id_), "[EasyDK Device] [MluContext] Get device failed.");
  CALL_CNRT_FUNC(cnrtSetCurrentDevice(dev), "[EasyDK Device] [MluContext] Set current device failed.");
#else
  CALL_CNRT_FUNC(cnrtSetDevice(dev_id_), "[EasyDK Device] [MluContext] Set device failed.");
#endif
  VLOG(2) << "[EasyDK Device] [MluContext] Bind device [" << dev_id_ << "] for this thread";
#if CNRT_MAJOR_VERSION < 5
  CALL_CNRT_FUNC(cnrtSetDeviceFlag(1), "[EasyDK Device] [MluContext] Set device flag failed.");
#endif
}

CoreVersion MluContext::GetCoreVersion() {
  _cnrt_init_tool::cnrt_init_tool.Init();
  static std::mutex m;
#if CNRT_MAJOR_VERSION < 5
  CoreVersion version;
  cnrtDeviceInfo_t device_info;
  std::unique_lock<std::mutex> lk(m);
  CALL_CNRT_FUNC(cnrtGetDeviceInfo(&device_info, dev_id_), "[EasyDK Device] [MluContext] Get device info failed.");
  lk.unlock();
  switch (device_info.core_version) {
    case CNRT_MLU220: {
      version = CoreVersion::MLU220;
      VLOG(4) << "[EasyDK Device] [MluContext] Get Core Version MLU220";
      break;
    }
    case CNRT_MLU270: {
      version = CoreVersion::MLU270;
      VLOG(4) << "[EasyDK Device] [MluContext] Get Core Version MLU270";
      break;
    }
    default:
      LOG(ERROR) << "[EasyDK Device] [MluContext] Unsupported CNRT core version "
                 << static_cast<int>(device_info.core_version);
      version = CoreVersion::INVALID;
  }
  return version;
#else
  constexpr int DEVICE_NAME_LENGTH = 64;
  struct DeviceName {
    char name[DEVICE_NAME_LENGTH];
    CoreVersion version;
  };
  using edk::CoreVersion;
  static DeviceName name_list_table[] = {
    {"MLU270", CoreVersion::MLU270},
    {"MLU220", CoreVersion::MLU220},
    {"MLU370", CoreVersion::MLU370},
    {"CE3226", CoreVersion::CE3226}
  };
  static uint32_t num = sizeof(name_list_table) / sizeof(DeviceName);
  cnrtDeviceProp_t prop;
  CALL_CNRT_FUNC(cnrtGetDeviceProperties(&prop, dev_id_), "[EasyDK Device] [MluContext] Get device properties failed");
  for (uint32_t idx = 0; idx < num; ++idx) {
    const char* name = name_list_table[idx].name;
    if (0 == strncmp(name, prop.name, strlen(name))) {
      return name_list_table[idx].version;
    }
  }
  LOG(ERROR) << "[EasyDK Device] [MluContext] Unsupported device name " << prop.name;
  return CoreVersion::INVALID;
#endif
}

}  // namespace edk
