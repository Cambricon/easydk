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
#include <vector>

#ifdef HAVE_CNCV
#include "cncv.h"
#endif

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "cxxutil/log.h"
#include "internal/cnrt_wrap.h"

#include "preproc.h"

#ifdef HAVE_CNCV
#define CNCV_SAFE_CALL(func, val)                                 \
  do {                                                            \
    cncvStatus_t ret = (func);                                    \
    if (ret != CNCV_STATUS_SUCCESS) {                             \
      LOGE(SAMPLE) << "Call " #func " failed. error code: " << ret; \
      return val;                                                 \
    }                                                             \
  } while (0)
#endif

PreprocSSD::PreprocSSD(infer_server::ModelPtr model, int dev_id, edk::PixelFmt dst_fmt)
    : dev_id_(dev_id), model_(model) {
#ifdef HAVE_CNCV
  size_t batch_size = model->BatchSize();
  dst_descs_.resize(batch_size);
  src_descs_.resize(batch_size);
  src_rois_.resize(batch_size);
  dst_rois_.resize(batch_size);

  // init descriptor params
  for (size_t i = 0; i < batch_size; ++i) {
    // dst desc
    dst_descs_[i].pixel_fmt = GetCncvPixFmt(dst_fmt);
    dst_descs_[i].height = model_->InputShape(0)[1];
    dst_descs_[i].width = model_->InputShape(0)[2];
    dst_descs_[i].depth = CNCV_DEPTH_8U;
    SetCncvStride(&dst_descs_[i]);
    dst_descs_[i].color_space = CNCV_COLOR_SPACE_BT_601;

    // src desc
    src_descs_[i].color_space = CNCV_COLOR_SPACE_BT_601;
    src_descs_[i].depth = CNCV_DEPTH_8U;
  }

  size_t ptr_size = batch_size * sizeof(void*);
  mlu_input_y_ = infer_server::Buffer(ptr_size, dev_id_);
  mlu_input_uv_ = infer_server::Buffer(ptr_size, dev_id_);
  mlu_output_ = infer_server::Buffer(ptr_size, dev_id_);
  cpu_input_y_ = infer_server::Buffer(ptr_size);
  cpu_input_uv_ = infer_server::Buffer(ptr_size);
  cpu_output_ = infer_server::Buffer(ptr_size);

  if (CNRT_RET_SUCCESS != cnrt::QueueCreate(&queue_)) {
    LOGE(SAMPLE) << "Create cnrtQueue failed";
    return;
  }
  if (CNCV_STATUS_SUCCESS != cncvCreate(&handle_)) {
    LOGE(SAMPLE) << "Create cncvHandle failed";
    return;
  }
  if (CNCV_STATUS_SUCCESS != cncvSetQueue(handle_, queue_)) {
    LOGE(SAMPLE) << "Set cnrtQueue to cncvHandle failed";
    return;
  }
#else
  LOGE(SAMPLE) << "PreprocSSD needs CNCV but not found, please install CNCV";
#endif
}


bool PreprocSSD::operator()(infer_server::ModelIO* model_input, const infer_server::BatchData& batch_infer_data,
                            const infer_server::ModelInfo* model) {
#ifdef HAVE_CNCV
if (!queue_ || !handle_) {
  LOGE(SAMPLE) << "[PreprocSSD] handle or queue is nullptr.";
  return false;
}
  auto batch_size = batch_infer_data.size();
  size_t ptr_size = batch_size * sizeof(void*);
  // init mlu buff
  void* mlu_input_y_ptr = mlu_input_y_.MutableData();
  void* mlu_input_uv_ptr = mlu_input_uv_.MutableData();
  void* mlu_output_ptr = mlu_output_.MutableData();

  // init cpu ptr
  void** cpu_input_y_ptr = reinterpret_cast<void**>(cpu_input_y_.MutableData());
  void** cpu_input_uv_ptr = reinterpret_cast<void**>(cpu_input_uv_.MutableData());
  void** cpu_output_ptr = reinterpret_cast<void**>(cpu_output_.MutableData());
  size_t output_offset = 0;

  // init src_decs_ and rects and cpu ptr
  for (size_t i = 0; i < batch_size; ++i) {
    edk::CnFrame& frame = batch_infer_data[i]->GetLref<edk::CnFrame>();
    if (frame.pformat != edk::PixelFmt::NV12 && frame.pformat != edk::PixelFmt::NV21) {
      LOGE(SAMPLE) << "[PreprocSSD] Pixel format " << static_cast<int>(frame.pformat) << " not supported!";
      return false;
    }
    // init cpu ptr
    cpu_input_y_ptr[i] = frame.ptrs[0];
    cpu_input_uv_ptr[i] = frame.ptrs[1];
    cpu_output_ptr[i] = (model_input->buffers[0])(output_offset).MutableData();
    output_offset += dst_descs_[i].stride[0] * dst_descs_[i].height;

    // init src descs
    src_descs_[i].pixel_fmt = GetCncvPixFmt(frame.pformat);
    src_descs_[i].width = frame.width;
    src_descs_[i].height = frame.height;
    if (frame.strides[0] > 1) {
      src_descs_[i].stride[0] = frame.strides[0];
      src_descs_[i].stride[1] = frame.strides[1];
      src_descs_[i].stride[2] = frame.strides[2];
    } else {
      SetCncvStride(&src_descs_[i]);
    }

    // init dst rect
    dst_rois_[i].x = 0;
    dst_rois_[i].y = 0;
    dst_rois_[i].w = dst_descs_[i].width;
    dst_rois_[i].h = dst_descs_[i].height;

    // init src rect
    src_rois_[i].x = 0;
    src_rois_[i].y = 0;
    src_rois_[i].w = frame.width;
    src_rois_[i].h = frame.height;
  }

  // copy cpu ptr to mlu ptr
  mlu_input_y_.CopyFrom(cpu_input_y_, ptr_size);
  mlu_input_uv_.CopyFrom(cpu_input_uv_, ptr_size);
  mlu_output_.CopyFrom(cpu_output_, ptr_size);

  size_t required_workspace_size = 0;
  // use batch_size from model to reduce remalloc times
  CNCV_SAFE_CALL(
      cncvGetResizeConvertWorkspaceSize(batch_size, src_descs_.data(),
                                        src_rois_.data(), dst_descs_.data(),
                                        dst_rois_.data(), &required_workspace_size),
      false);

  // prepare workspace
  if (!workspace_.OwnMemory() || workspace_size_ < required_workspace_size) {
    workspace_size_ = required_workspace_size;
    workspace_ = infer_server::Buffer(workspace_size_, dev_id_);
  }

  // compute
  CNCV_SAFE_CALL(
      cncvResizeConvert(handle_, batch_size,
                        src_descs_.data(), src_rois_.data(),
                        reinterpret_cast<void**>(mlu_input_y_ptr),
                        reinterpret_cast<void**>(mlu_input_uv_ptr),
                        dst_descs_.data(), dst_rois_.data(),
                        reinterpret_cast<void**>(mlu_output_ptr),
                        workspace_size_, workspace_.MutableData(),
                        CNCV_INTER_BILINEAR),
      false);
  return true;
#else
  LOGE(SAMPLE) << "PreprocSSD needs CNCV but not found, please install CNCV";
  return false;
#endif
}
