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
#include "resize_yuv.h"

#ifdef HAVE_CNCV
#include "cncv.h"
#endif
#include "cnrt.h"

#include "device/mlu_context.h"
#include "easycodec/vformat.h"
#include "internal/cnrt_wrap.h"

#ifdef HAVE_CNCV

#define CNCV_SAFE_CALL(func, val)                                 \
  do {                                                            \
    cncvStatus_t ret = (func);                                    \
    if (ret != CNCV_STATUS_SUCCESS) {                             \
      LOGE(SAMPLE) << "Call " #func " failed. error code: " << ret; \
      return val;                                                 \
    }                                                             \
  } while (0)
#define CNRT_SAFE_CALL(func, val)                                 \
  do {                                                            \
    cnrtRet_t ret = (func);                                       \
    if (ret != CNRT_RET_SUCCESS) {                                \
      LOGE(SAMPLE) << "Call " #func " failed. error code: " << ret; \
      return val;                                                 \
    }                                                             \
  } while (0)

CncvResizeYuv::CncvResizeYuv(int dev_id) : device_id_(dev_id) {}

bool CncvResizeYuv::Init() {
  if (initialized_) {
    LOGE(SAMPLE) << "CncvResizeYuv is initialzed. Should not init twice.";
    return false;
  }
  edk::MluContext mlu_ctx;
  mlu_ctx.SetDeviceId(device_id_);
  mlu_ctx.BindDevice();
  CNRT_SAFE_CALL(cnrt::QueueCreate(&queue_), false);
  CNCV_SAFE_CALL(cncvCreate(&handle_), false);
  CNCV_SAFE_CALL(cncvSetQueue(handle_, queue_), false);
  CNRT_SAFE_CALL(cnrtMalloc(reinterpret_cast<void**>(&mlu_input_), 2 * sizeof(void*)), false);
  CNRT_SAFE_CALL(cnrtMalloc(reinterpret_cast<void**>(&mlu_output_), 2 * sizeof(void*)), false);
  cpu_input_ = reinterpret_cast<void**>(malloc(2 * sizeof(void*)));
  cpu_output_ = reinterpret_cast<void**>(malloc(2 * sizeof(void*)));
  initialized_ = true;
  return true;
}


static cncvPixelFormat GetCncvPixFmt(edk::PixelFmt fmt) {
  switch (fmt) {
    case edk::PixelFmt::NV12:
      return CNCV_PIX_FMT_NV12;
    case edk::PixelFmt::NV21:
      return CNCV_PIX_FMT_NV21;
    default:
      LOGE(SAMPLE) << "Unsupported input format.";
      return CNCV_PIX_FMT_INVALID;
  }
}

bool CncvResizeYuv::Process(const edk::CnFrame &src, edk::CnFrame *dst) {
  if (src.pformat != dst->pformat || (src.pformat != edk::PixelFmt::NV12 && src.pformat != edk::PixelFmt::NV21)) {
    LOGE(SAMPLE) << "CncvResizeYuv unsupported pixel format. Or the src and dst pixel format do not match";
    return false;
  }
  if (src.device_id < 0 || dst->device_id < 0) {
    LOGE(SAMPLE) << "CncvResizeYuv the src and dst data should be on mlu.";
    return false;
  }
  edk::MluContext mlu_ctx;
  mlu_ctx.SetDeviceId(device_id_);
  mlu_ctx.BindDevice();
  int batch_size = 1;
  src_desc_.width = src.width;
  src_desc_.height = src.height;
  src_desc_.pixel_fmt = GetCncvPixFmt(src.pformat);
  src_desc_.stride[0] = src.strides[0];
  src_desc_.stride[1] = src.strides[1];
  src_desc_.depth = CNCV_DEPTH_8U;
  src_roi_.x = 0;
  src_roi_.y = 0;
  src_roi_.w = src.width;
  src_roi_.h = src.height;
  *(cpu_input_) = reinterpret_cast<void*>(src.ptrs[0]);
  *(cpu_input_ + 1) = reinterpret_cast<void*>(src.ptrs[1]);
  CNRT_SAFE_CALL(cnrtMemcpy(mlu_input_, cpu_input_, 2 * sizeof(void*), CNRT_MEM_TRANS_DIR_HOST2DEV), false);

  dst_desc_.width = dst->width;
  dst_desc_.height = dst->height;
  dst_desc_.pixel_fmt = GetCncvPixFmt(dst->pformat);
  dst_desc_.stride[0] = dst->strides[0];
  dst_desc_.stride[1] = dst->strides[1];
  dst_desc_.depth = CNCV_DEPTH_8U;
  dst_roi_.x = 0;
  dst_roi_.y = 0;
  dst_roi_.w = dst->width;
  dst_roi_.h = dst->height;
  *(cpu_output_) = reinterpret_cast<void*>(dst->ptrs[0]);
  *(cpu_output_ + 1) = reinterpret_cast<void*>(dst->ptrs[1]);
  CNRT_SAFE_CALL(cnrtMemcpy(mlu_output_, cpu_output_, 2 * sizeof(void*), CNRT_MEM_TRANS_DIR_HOST2DEV), false);

  size_t required_workspace_size = 0;
  CNCV_SAFE_CALL(cncvGetResizeYuvWorkspaceSize(batch_size, &src_desc_, &src_roi_, &dst_desc_, &dst_roi_,
                                               &required_workspace_size), false);
  if (required_workspace_size != workspace_size_) {
    workspace_size_ = required_workspace_size;
    if (workspace_) CNRT_SAFE_CALL(cnrtFree(workspace_), false);
    CNRT_SAFE_CALL(cnrtMalloc(&(workspace_), required_workspace_size), false);
  }

  CNCV_SAFE_CALL(cncvResizeYuv(handle_, batch_size, &(src_desc_), &(src_roi_), mlu_input_, &(dst_desc_), mlu_output_,
                               &(dst_roi_), required_workspace_size, workspace_, CNCV_INTER_BILINEAR), false);
  CNRT_SAFE_CALL(cnrt::QueueSync(queue_), false);
  return true;
}

bool CncvResizeYuv::Destroy() {
  edk::MluContext mlu_ctx;
  mlu_ctx.SetDeviceId(device_id_);
  mlu_ctx.BindDevice();
  if (handle_) {
    CNCV_SAFE_CALL(cncvDestroy(handle_), false);
    handle_ = nullptr;
  }
  if (queue_) {
    CNRT_SAFE_CALL(cnrt::QueueDestroy(queue_), false);
    queue_ = nullptr;
  }
  if (mlu_input_) {
    CNRT_SAFE_CALL(cnrtFree(mlu_input_), false);
    mlu_input_ = nullptr;
  }
  if (mlu_output_) {
    CNRT_SAFE_CALL(cnrtFree(mlu_output_), false);
    mlu_output_ = nullptr;
  }
  if (cpu_input_) {
    free(cpu_input_);
    cpu_input_ = nullptr;
  }
  if (cpu_output_) {
    free(cpu_output_);
    cpu_output_ = nullptr;
  }
  if (workspace_) {
    CNRT_SAFE_CALL(cnrtFree(workspace_), false);
    workspace_ = nullptr;
    workspace_size_ = 0;
  }
  initialized_ = false;
  return true;
}

CncvResizeYuv::~CncvResizeYuv() {
  Destroy();
}

#endif
