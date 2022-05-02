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
#ifndef EDK_SAMPLES_PREPROCESS_PREPROC_H_
#define EDK_SAMPLES_PREPROCESS_PREPROC_H_

#include <glog/logging.h>

#include <memory>
#include <utility>
#include <vector>

#ifdef HAVE_CNCV
#include "cncv.h"
#endif
#include <cnrt.h>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "easycodec/vformat.h"

#ifdef HAVE_CNCV
cncvPixelFormat GetCncvPixFmt(edk::PixelFmt fmt);
uint32_t GetCncvDepthSize(cncvDepth_t depth);
void SetCncvStride(cncvImageDescriptor* desc);
#endif

struct PreprocSSD {
#ifdef HAVE_CNCV
  struct Context {
    bool Init();
    bool Destroy();

    Context(uint32_t batch_size, int dev_id, edk::PixelFmt dst_fmt);
    ~Context();

    Context(const Context &obj);
    Context(Context &&obj);
    Context& operator= (const Context &obj);

    uint32_t batch_size_{0};
    int dev_id_{0};
    edk::PixelFmt dst_fmt_;
    cnrtQueue_t queue_{nullptr};
    cncvHandle_t handle_{nullptr};
    infer_server::Buffer** workspace_{nullptr};
    size_t* workspace_size_{nullptr};

    std::shared_ptr<infer_server::Buffer> mlu_input_y_{nullptr};
    std::shared_ptr<infer_server::Buffer> mlu_input_uv_{nullptr};
    std::shared_ptr<infer_server::Buffer> mlu_output_{nullptr};
    std::shared_ptr<infer_server::Buffer> cpu_input_y_{nullptr};
    std::shared_ptr<infer_server::Buffer> cpu_input_uv_{nullptr};
    std::shared_ptr<infer_server::Buffer> cpu_output_{nullptr};
  };

  const Context& ConvertContext(infer_server::ProcessFuncContextPtr ctx) {
    return ctx->GetLref<Context>();
  }
#endif

  infer_server::ProcessFuncContextPtr GetContextPtr() {
    VLOG(2) << "[EasyDK Samples] [PreprocSSD] GetContextPtr()";
#ifdef HAVE_CNCV
    if (!process_func_ctx_) {
      VLOG(1) << "[EasyDK Samples] [PreprocSSD] Create Context and set context to Process function context.";
      process_func_ctx_ = std::make_shared<infer_server::ProcessFuncContext>();
      process_func_ctx_->Set(std::move(Context(model_->BatchSize(), dev_id_, dst_fmt_)));
    }
#endif
    return process_func_ctx_;
  }

  PreprocSSD(infer_server::ModelPtr model, int dev_id, edk::PixelFmt dst_fmt);

  bool operator()(infer_server::ModelIO* model_input, const infer_server::BatchData& batch_infer_data,
                  const infer_server::ModelInfo* model, infer_server::ProcessFuncContextPtr context);

 private:
  int dev_id_;
  infer_server::ModelPtr model_;
  edk::PixelFmt dst_fmt_;
  infer_server::ProcessFuncContextPtr process_func_ctx_{nullptr};
};  // struct PreprocSSD

#endif  // EDK_SAMPLES_PREPROCESS_PREPROC_H_
