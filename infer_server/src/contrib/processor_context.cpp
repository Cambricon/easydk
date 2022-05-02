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
#include <glog/logging.h>

#include <memory>
#include <utility>

#ifdef HAVE_CNCV
#include "cncv.h"
#endif
#include <cnrt.h>

#include "cnis/contrib/processor_context.h"
#include "internal/cnrt_wrap.h"

namespace infer_server {
namespace video {

#ifdef HAVE_CNCV
#define CNRT_SAFE_CALL(func, val)                                 \
  do {                                                            \
    cnrtRet_t ret = (func);                                       \
    if (ret != CNRT_RET_SUCCESS) {                                \
      LOG(ERROR) << "[EasyDK InferServer] Call " #func " failed. error code: " << ret; \
      return val;                                                 \
    }                                                             \
  } while (0)
#define CNCV_SAFE_CALL(func, val)                                 \
  do {                                                            \
    cncvStatus_t ret = (func);                                    \
    if (ret != CNCV_STATUS_SUCCESS) {                             \
      LOG(ERROR) << "[EasyDK InferServer] Call " #func " failed. error code: " << ret; \
      return val;                                                 \
    }                                                             \
  } while (0)
bool CncvContext::Init() {
  CNRT_SAFE_CALL(cnrt::QueueCreate(&queue_), false);
  CNCV_SAFE_CALL(cncvCreate(&handle_), false);
  CNCV_SAFE_CALL(cncvSetQueue(handle_, queue_), false);
  workspace_ = reinterpret_cast<void**>(malloc(sizeof(void*)));
  *workspace_ = nullptr;
  workspace_size_ = new size_t(0);
  return true;
}

bool CncvContext::Destroy() {
  if (handle_) CNCV_SAFE_CALL(cncvDestroy(handle_), false);
  if (queue_) CNRT_SAFE_CALL(cnrt::QueueDestroy(queue_), false);
  if (workspace_) {
    if (*workspace_) {
      CNRT_SAFE_CALL(cnrtFree(*workspace_), false);
    }
    free(workspace_);
  }
  if (workspace_size_) delete workspace_size_;
  return true;
}

CncvContext::CncvContext() {
  if (!Init()) {
    LOG(FATAL) << "[EasyDK InferServer] [CncvContext] Constructor: Init failed";
  }
}

CncvContext::~CncvContext() {
  if (!Destroy()) {
    LOG(FATAL) << "[EasyDK InferServer] [CncvContext] Deconstructor: Destroy failed";
  }
}

CncvContext::CncvContext(const CncvContext &obj) {
  if (!Init()) {
    LOG(FATAL) << "[EasyDK InferServer] [CncvContext] Copy constructor: Init failed";
  }
}

CncvContext::CncvContext(CncvContext &&obj) {
  workspace_ = obj.workspace_;
  if (obj.workspace_) {
    *workspace_ = *(obj.workspace_);
  }
  workspace_size_ = obj.workspace_size_;
  queue_ = obj.queue_;
  handle_ = obj.handle_;
  if (obj.workspace_) {
    *(obj.workspace_) = nullptr;
  }
  obj.workspace_ = nullptr;
  obj.workspace_size_ = nullptr;
  obj.queue_ = nullptr;
  obj.handle_ = nullptr;
}

CncvContext& CncvContext::operator= (const CncvContext &obj) {
  if (!Init()) {
    LOG(FATAL) << "[EasyDK InferServer] [CncvContext] Operator= : Init failed";
  }
  return *this;
}
#endif

CustomCncvPreprocessor::CustomCncvPreprocessor() noexcept
    : Preprocessor() {
#ifdef HAVE_CNCV
  ProcessFuncContextPtr ctx = std::make_shared<ProcessFuncContext>();
  ctx->Set(std::move(CncvContext()));
  SetParams("process_func_context", ctx);
#else
  LOG(ERROR) << "[EasyDK InferServer] [CustomCncvPreprocessor] CNCV is not found, please install CNCV";
#endif
}

}  // namespace video
}  // namespace infer_server

