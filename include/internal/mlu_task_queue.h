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

#ifndef EASYINFER_MLU_TASK_QUEUE_H_
#define EASYINFER_MLU_TASK_QUEUE_H_

#include <cnrt.h>

#include "device/mlu_context.h"

namespace edk {

struct MluTaskQueuePrivate {
  ~MluTaskQueuePrivate();
  cnrtQueue_t queue = nullptr;
};

inline void MluTaskQueue::_PrivDelete::operator()(MluTaskQueuePrivate* p) { delete p; }

class MluTaskQueueProxy {
 public:
  static cnrtQueue_t GetCnrtQueue(MluTaskQueue_t q) noexcept { return q->priv_->queue; }

  static void SetCnrtQueue(MluTaskQueue_t q, cnrtQueue_t cnrt_q) {
    if (q->priv_->queue) {
      q->priv_.reset(new MluTaskQueuePrivate);
    }
    q->priv_->queue = cnrt_q;
  }

  static MluTaskQueue_t Wrap(cnrtQueue_t cnrt_q) {
    auto q = std::shared_ptr<MluTaskQueue>(new MluTaskQueue);
    q->priv_->queue = cnrt_q;
    return q;
  }
};

}  // namespace edk

#endif  // EASYINFER_MLU_TASK_QUEUE_H_
