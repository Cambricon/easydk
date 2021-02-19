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

#include "request_ctrl.h"

#include <map>
#include <string>

#include "session.h"

namespace infer_server {

void RequestControl::ProcessDone(Status status, InferDataPtr output, uint32_t index,
                                 std::map<std::string, float> perf) noexcept {
  if (output) {
    CHECK_LT(index, data_num_);
    output_->data[index] = output;
  }

  SpinLockGuard lk(done_mutex_);
  for (auto& it : perf) {
    if (!output_->perf.count(it.first)) {
      output_->perf[it.first] = it.second;
    } else {
      output_->perf[it.first] += it.second;
    }
  }

  if (status != Status::SUCCESS && IsSuccess()) {
    status_.store(status);
  }

  VLOG(6) << "one data ready) request id: " << request_id_ << ", remain: " << wait_num_ - 1;
  CHECK_NE(wait_num_, 0u);
  if (--wait_num_ == 0) {
    process_finished_.store(true);
    done_notifier_(this);
  }
}

}  // namespace infer_server
