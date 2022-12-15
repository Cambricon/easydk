/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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
#ifndef MPS_SERVICE_IMPL_VDEC_HPP_
#define MPS_SERVICE_IMPL_VDEC_HPP_

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>

#include "glog/logging.h"

#include "cn_buffer.h"
#include "cn_vdec.h"

#include "../mps_service.hpp"
#include "mps_internal/cnsample_comm.h"

namespace cnedk {

constexpr int kMaxMpsVdecNum = 16;

class MpsVdec : private NonCopyable {
 public:
  explicit MpsVdec(IVBInfo *vb_info) : vb_info_(vb_info) {}
  ~MpsVdec() {}

  int Config(const MpsServiceConfig &config);
  void *Create(IVDecResult *result, cnEnPayloadType_t type, int max_width, int max_height, int buf_num = 12,
               cnEnPixelFormat_t pix_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_VU);
  int Destroy(void *handle);
  int SendStream(void *handle, const cnvdecStream_t *pst_stream, cnS32_t milli_sec);
  int ReleaseFrame(void *handle, const cnVideoFrameInfo_t *info);

 private:
  int GetId() {
    std::unique_lock<std::mutex> lk(id_mutex_);
    if (id_q_.size()) {
      int id = id_q_.front();
      id_q_.pop();
      return id + 1;
    }
    LOG(ERROR) << "[EasyDK] [MpsVdec] GetId(): No available decoder id";
    return -1;
  }
  void ReturnId(int id) {
    std::unique_lock<std::mutex> lk(id_mutex_);
    if (id < mps_config_.codec_id_start + 1 || id >= kMaxMpsVdecNum) {
      LOG(ERROR) << "[EasyDK] [MpsVdec] ReturnId(): decoder id is invalid";
      return;
    }
    id_q_.push(id - 1);
  }
  int CheckHandleEos(void *handle);

 private:
  IVBInfo *vb_info_ = nullptr;
  MpsServiceConfig mps_config_;
  std::mutex id_mutex_;
  std::queue<int> id_q_;

  struct VDecCtx {
    IVDecResult *result_ = nullptr;
    std::atomic<bool> eos_sent_{false};
    std::atomic<bool> error_flag_{false};
    std::atomic<bool> created_{false};
    cnvdecChnParam_t chn_param_;
    cnvdecChnAttr_t chn_attr_;
    vdecChn_t vdec_chn_ = -1;
    int fd_ = -1;
    std::mutex mutex_;
  } vdec_ctx_[kMaxMpsVdecNum];
};

}  // namespace cnedk

#endif  // MPS_SERVICE_IMPL_VDEC_HPP_
