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

#include <vector>

#ifdef HAVE_CNCV
#include "cncv.h"
#endif

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "cxxutil/log.h"
#include "easycodec/vformat.h"

#ifdef HAVE_CNCV
cncvPixelFormat GetCncvPixFmt(edk::PixelFmt fmt);
uint32_t GetCncvDepthSize(cncvDepth_t depth);
void SetCncvStride(cncvImageDescriptor* desc);
#endif

struct PreprocSSD {
  PreprocSSD(infer_server::ModelPtr model, int dev_id, edk::PixelFmt dst_fmt);

  bool operator()(infer_server::ModelIO* model_input, const infer_server::BatchData& batch_infer_data,
                  const infer_server::ModelInfo* model);

 private:
  int dev_id_;
  infer_server::ModelPtr model_;
#ifdef HAVE_CNCV
  cnrtQueue_t queue_;
  cncvHandle_t handle_;
  infer_server::Buffer workspace_;
  size_t workspace_size_{0};

  std::vector<cncvImageDescriptor> src_descs_;
  std::vector<cncvImageDescriptor> dst_descs_;
  std::vector<cncvRect> src_rois_;
  std::vector<cncvRect> dst_rois_;

  infer_server::Buffer mlu_input_y_;
  infer_server::Buffer mlu_input_uv_;
  infer_server::Buffer mlu_output_;
  infer_server::Buffer cpu_input_y_;
  infer_server::Buffer cpu_input_uv_;
  infer_server::Buffer cpu_output_;
#endif
};  // struct PreprocSSD

#endif  // EDK_SAMPLES_PREPROCESS_PREPROC_H_
