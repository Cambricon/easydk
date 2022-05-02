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
#include <glog/logging.h>

#include <memory>
#include <vector>

#ifdef HAVE_CNCV
#include "cncv.h"
#endif
#include "cnrt.h"

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "internal/cnrt_wrap.h"

#include "preproc.h"
#include "utils.h"

#ifdef HAVE_CNCV

bool PreprocSSD::Context::Init() {
  size_t ptr_size = batch_size_ * sizeof(void*);
  mlu_input_y_ = std::make_shared<infer_server::Buffer>(ptr_size, dev_id_);
  mlu_input_uv_ = std::make_shared<infer_server::Buffer>(ptr_size, dev_id_);
  mlu_output_ = std::make_shared<infer_server::Buffer>(ptr_size, dev_id_);
  cpu_input_y_ = std::make_shared<infer_server::Buffer>(ptr_size);
  cpu_input_uv_ = std::make_shared<infer_server::Buffer>(ptr_size);
  cpu_output_ = std::make_shared<infer_server::Buffer>(ptr_size);

  workspace_size_ = new size_t(0);
  workspace_ = reinterpret_cast<infer_server::Buffer**>(malloc(sizeof(infer_server::Buffer*)));
  *workspace_ = nullptr;

  CNRT_SAFE_CALL(cnrt::QueueCreate(&queue_), false);
  CNCV_SAFE_CALL(cncvCreate(&handle_), false);
  CNCV_SAFE_CALL(cncvSetQueue(handle_, queue_), false);
  return true;
}

bool PreprocSSD::Context::Destroy() {
  if (handle_) CNCV_SAFE_CALL(cncvDestroy(handle_), false);
  if (queue_) CNRT_SAFE_CALL(cnrt::QueueDestroy(queue_), false);
  if (workspace_size_) delete workspace_size_;
  if (workspace_) {
    if (*workspace_) {
      delete (*workspace_);
      *workspace_ = nullptr;
    }
    free(workspace_);
    workspace_ = nullptr;
  }
  return true;
}

PreprocSSD::Context::Context(uint32_t batch_size, int dev_id, edk::PixelFmt dst_fmt)
    : batch_size_(batch_size), dev_id_(dev_id), dst_fmt_(dst_fmt) {
  if (!Init()) {
    LOG(FATAL) << "[EasyDK Samples] [PreprocSSD] Context() init failed";
  }
}
PreprocSSD::Context::~Context() {
  if (!Destroy()) {
    LOG(FATAL) << "[EasyDK Samples] [PreprocSSD] ~Context() destroy failed";
  }
}

PreprocSSD::Context::Context(const Context &obj) {
  batch_size_ = obj.batch_size_;
  dev_id_ = obj.dev_id_;
  dst_fmt_ = obj.dst_fmt_;
  if (!Init()) {
    LOG(FATAL) << "[EasyDK Samples] [PreprocSSD] Context copy constructor: Init failed";
  }
}

PreprocSSD::Context::Context(Context &&obj) {
  batch_size_ = obj.batch_size_;
  dev_id_ = obj.dev_id_;
  dst_fmt_ = obj.dst_fmt_;
  mlu_input_y_  = obj.mlu_input_y_;
  mlu_input_uv_ = obj.mlu_input_uv_;
  mlu_output_   = obj.mlu_output_;
  cpu_input_y_  = obj.cpu_input_y_;
  cpu_input_uv_ = obj.cpu_input_uv_;
  cpu_output_   = obj.cpu_output_;
  workspace_ = obj.workspace_;
  if (obj.workspace_) {
    *workspace_ = *obj.workspace_;
  }
  workspace_size_ = obj.workspace_size_;
  queue_ = obj.queue_;
  handle_ = obj.handle_;
  mlu_input_y_  = nullptr;
  mlu_input_uv_ = nullptr;
  mlu_output_   = nullptr;
  cpu_input_y_  = nullptr;
  cpu_input_uv_ = nullptr;
  cpu_output_   = nullptr;
  if (obj.workspace_) {
    *obj.workspace_ = nullptr;
  }
  obj.workspace_ = nullptr;
  obj.workspace_size_ = nullptr;
  obj.queue_ = nullptr;
  obj.handle_ = nullptr;
}

PreprocSSD::Context& PreprocSSD::Context::operator= (const Context &obj) {
  batch_size_ = obj.batch_size_;
  dev_id_ = obj.dev_id_;
  dst_fmt_ = obj.dst_fmt_;
  if (!Init()) {
    LOG(FATAL) << "[EasyDK Samples] [PreprocSSD] Context operator= : Init failed";
  }
  return *this;
}
#endif

PreprocSSD::PreprocSSD(infer_server::ModelPtr model, int dev_id, edk::PixelFmt dst_fmt)
    : dev_id_(dev_id), model_(model), dst_fmt_(dst_fmt) {
#ifndef HAVE_CNCV
  LOG(ERROR) << "[EasyDK Samples] [PreprocSSD] Needs CNCV but not found, please install CNCV";
#endif
}


bool PreprocSSD::operator()(infer_server::ModelIO* model_input, const infer_server::BatchData& batch_infer_data,
                            const infer_server::ModelInfo* model_info,
                            const infer_server::ProcessFuncContextPtr context) {
#ifdef HAVE_CNCV
  const PreprocSSD::Context& ctx = ConvertContext(context);
  cnrtQueue_t queue = ctx.queue_;
  cncvHandle_t handle = ctx.handle_;
  size_t* workspace_size = ctx.workspace_size_;
  infer_server::Buffer** workspace = ctx.workspace_;
  std::shared_ptr<infer_server::Buffer> mlu_input_y  = ctx.mlu_input_y_;
  std::shared_ptr<infer_server::Buffer> mlu_input_uv = ctx.mlu_input_uv_;
  std::shared_ptr<infer_server::Buffer> mlu_output   = ctx.mlu_output_;
  std::shared_ptr<infer_server::Buffer> cpu_input_y  = ctx.cpu_input_y_;
  std::shared_ptr<infer_server::Buffer> cpu_input_uv = ctx.cpu_input_uv_;
  std::shared_ptr<infer_server::Buffer> cpu_output   = ctx.cpu_output_;

  if (!queue || !handle) {
    LOG(ERROR) << "[EasyDK Samples] [PreprocSSD] handle or queue is nullptr.";
    return false;
  }

  auto batch_size = batch_infer_data.size();
  if (batch_size < 1) {
    LOG(ERROR) << "[EasyDK Samples] [PreprocSSD] batch size is less than 1, no data.";
    return false;
  }
  uint32_t input_num = model_info->InputNum();
  if (input_num != 1) {
    LOG(ERROR) << "[EasyDK Samples] [PreprocSSD] model input number not supported. It should be 1, but " << input_num;
    return false;
  }
  infer_server::Shape input_shape;
  input_shape = model_info->InputShape(0);
  int h_idx = 1;
  int w_idx = 2;
  if (model_info->InputLayout(0).order == infer_server::DimOrder::NCHW) {
    h_idx = 2;
    w_idx = 3;
  }
  size_t ptr_size = batch_size * sizeof(void*);

  std::vector<cncvImageDescriptor> src_descs(batch_size);
  std::vector<cncvImageDescriptor> dst_descs(batch_size);
  std::vector<cncvRect> src_rois(batch_size);
  std::vector<cncvRect> dst_rois(batch_size);

  // init mlu buff
  void* mlu_input_y_ptr = mlu_input_y->MutableData();
  void* mlu_input_uv_ptr = mlu_input_uv->MutableData();
  void* mlu_output_ptr = mlu_output->MutableData();

  // init cpu ptr
  void** cpu_input_y_ptr = reinterpret_cast<void**>(cpu_input_y->MutableData());
  void** cpu_input_uv_ptr = reinterpret_cast<void**>(cpu_input_uv->MutableData());
  void** cpu_output_ptr = reinterpret_cast<void**>(cpu_output->MutableData());
  size_t output_offset = 0;

  // init src_decs_ and rects and cpu ptr
  for (size_t i = 0; i < batch_size; ++i) {
    edk::CnFrame& frame = batch_infer_data[i]->GetLref<edk::CnFrame>();
    if (frame.pformat != edk::PixelFmt::NV12 && frame.pformat != edk::PixelFmt::NV21) {
      LOG(ERROR) << "[EasyDK Samples] [PreprocSSD] Pixel format " << PixelFmtStr(frame.pformat)
                 << " not supported!";
      return false;
    }
    // init cpu ptr
    cpu_input_y_ptr[i] = frame.ptrs[0];
    cpu_input_uv_ptr[i] = frame.ptrs[1];
    cpu_output_ptr[i] = (model_input->buffers[0])(output_offset).MutableData();
    output_offset += dst_descs[i].stride[0] * dst_descs[i].height;

    // init src descs
    src_descs[i].pixel_fmt = GetCncvPixFmt(frame.pformat);
    src_descs[i].width = frame.width;
    src_descs[i].height = frame.height;
    src_descs[i].depth = CNCV_DEPTH_8U;
    if (frame.strides[0] > 1) {
      src_descs[i].stride[0] = frame.strides[0];
      src_descs[i].stride[1] = frame.strides[1];
      src_descs[i].stride[2] = frame.strides[2];
    } else {
      SetCncvStride(&src_descs[i]);
    }
    src_descs[i].color_space = CNCV_COLOR_SPACE_BT_601;

    // init dst descriptor
    dst_descs[i].width = input_shape[w_idx];
    dst_descs[i].height = input_shape[h_idx];
    dst_descs[i].depth = CNCV_DEPTH_8U;
    dst_descs[i].pixel_fmt = GetCncvPixFmt(dst_fmt_);
    SetCncvStride(&dst_descs[i]);
    dst_descs[i].color_space = CNCV_COLOR_SPACE_BT_601;

    // init dst rect
    dst_rois[i].x = 0;
    dst_rois[i].y = 0;
    dst_rois[i].w = dst_descs[i].width;
    dst_rois[i].h = dst_descs[i].height;

    // init src rect
    src_rois[i].x = 0;
    src_rois[i].y = 0;
    src_rois[i].w = frame.width;
    src_rois[i].h = frame.height;
  }

  // copy cpu ptr to mlu ptr
  mlu_input_y->CopyFrom(*cpu_input_y, ptr_size);
  mlu_input_uv->CopyFrom(*cpu_input_uv, ptr_size);
  mlu_output->CopyFrom(*cpu_output, ptr_size);

  size_t required_workspace_size = 0;
  // use batch_size from model to reduce remalloc times
  CNCV_SAFE_CALL(
      cncvGetResizeConvertWorkspaceSize(batch_size, src_descs.data(),
                                        src_rois.data(), dst_descs.data(),
                                        dst_rois.data(), &required_workspace_size),
      false);

  // prepare workspace
  if (!workspace) {
    LOG(ERROR) << "[EasyDK Samples] [PreprocSSD] workspace is nullptr.";
    return false;
  }
  if (*workspace_size < required_workspace_size) {
    if (*workspace) {
      delete (*workspace);
    }
    *workspace_size = required_workspace_size;
    *workspace = new infer_server::Buffer(*workspace_size, dev_id_);
  }

  // compute
  CNCV_SAFE_CALL(
      cncvResizeConvert(handle, batch_size,
                        src_descs.data(), src_rois.data(),
                        reinterpret_cast<void**>(mlu_input_y_ptr),
                        reinterpret_cast<void**>(mlu_input_uv_ptr),
                        dst_descs.data(), dst_rois.data(),
                        reinterpret_cast<void**>(mlu_output_ptr),
                        *workspace_size, (*workspace)->MutableData(),
                        CNCV_INTER_BILINEAR),
      false);
  CNRT_SAFE_CALL(cnrt::QueueSync(queue), false);
  return true;
#else
  LOG(ERROR) << "[EasyDK Samples] [PreprocSSD] Needs CNCV but not found, please install CNCV";
  return false;
#endif
}
