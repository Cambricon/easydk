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

#ifndef SAMPLE_PRE_PROCESS_YOLOV3_HPP_
#define SAMPLE_PRE_PROCESS_YOLOV3_HPP_

#include <vector>

#include "cnis/processor.h"
#include "cnis/infer_server.h"

#include "glog/logging.h"


static CnedkTransformRect KeepAspectRatio(int src_w, int src_h, int dst_w, int dst_h) {
  float src_ratio = static_cast<float>(src_w) / src_h;
  float dst_ratio = static_cast<float>(dst_w) / dst_h;
  CnedkTransformRect res;
  memset(&res, 0, sizeof(res));
  if (src_ratio < dst_ratio) {
    int pad_lenth = dst_w - src_ratio * dst_h;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_w - pad_lenth / 2 < 0) return res;
    res.width = dst_w - pad_lenth;
    res.left = pad_lenth / 2;
    res.top = 0;
    res.height = dst_h;
  } else if (src_ratio > dst_ratio) {
    int pad_lenth = dst_h - dst_w / src_ratio;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_h - pad_lenth / 2 < 0) return res;
    res.height = dst_h - pad_lenth;
    res.top = pad_lenth / 2;
    res.left = 0;
    res.width = dst_w;
  } else {
    res.left = 0;
    res.top = 0;
    res.width = dst_w;
    res.height = dst_h;
  }
  return res;
}

class PreprocYolov3 : public infer_server::IPreproc {
 public:
  PreprocYolov3() = default;
  ~PreprocYolov3() = default;

 private:
  int OnTensorParams(const infer_server::CnPreprocTensorParams *params) override {
    params_ = *params;
    return 0;
  }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect> &src_rects) override {
    CnedkBufSurface* src_buf = src->GetBufSurface();
    CnedkBufSurface* dst_buf = dst->GetBufSurface();

    uint32_t batch_size = src->GetNumFilled();
    std::vector<CnedkTransformRect> src_rect(batch_size);
    std::vector<CnedkTransformRect> dst_rect(batch_size);
    CnedkTransformParams params;
    memset(&params, 0, sizeof(params));
    params.transform_flag = 0;
    if (src_rects.size()) {
      params.transform_flag |= CNEDK_TRANSFORM_CROP_SRC;
      params.src_rect = src_rect.data();
      memset(src_rect.data(), 0, sizeof(CnedkTransformRect) * batch_size);
      for (uint32_t i = 0; i < batch_size; i++) {
        CnedkTransformRect *src_bbox = &src_rect[i];
        *src_bbox = src_rects[i];
        // validate bbox
        src_bbox->left -= src_bbox->left & 1;
        src_bbox->top -= src_bbox->top & 1;
        src_bbox->width -= src_bbox->width & 1;
        src_bbox->height -= src_bbox->height & 1;
        while (src_bbox->left + src_bbox->width > src_buf->surface_list[i].width) src_bbox->width -= 2;
        while (src_bbox->top + src_bbox->height > src_buf->surface_list[i].height) src_bbox->height -= 2;
      }
    }

    // configur dst_desc
    CnedkTransformTensorDesc dst_desc;
    dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_RGB;
    dst_desc.data_type = CNEDK_TRANSFORM_UINT8;

    if (params_.input_order == infer_server::DimOrder::NHWC) {
      dst_desc.shape.n = params_.input_shape[0];
      dst_desc.shape.h = params_.input_shape[1];
      dst_desc.shape.w = params_.input_shape[2];
      dst_desc.shape.c = params_.input_shape[3];
    } else if (params_.input_order == infer_server::DimOrder::NCHW) {
      dst_desc.shape.n = params_.input_shape[0];
      dst_desc.shape.c = params_.input_shape[1];
      dst_desc.shape.h = params_.input_shape[2];
      dst_desc.shape.w = params_.input_shape[3];
    } else {
      LOG(ERROR) << "[EasyDK Samples] [PreprocYolov3] OnPreproc(): Unsupported input dim order";
      return -1;
    }

    params.transform_flag |= CNEDK_TRANSFORM_CROP_DST;
    params.dst_rect = dst_rect.data();
    memset(dst_rect.data(), 0, sizeof(CnedkTransformRect) * batch_size);
    for (uint32_t i = 0; i < batch_size; i++) {
      CnedkTransformRect *dst_bbox = &dst_rect[i];
      *dst_bbox = KeepAspectRatio(src_buf->surface_list[i].width, src_buf->surface_list[i].height, dst_desc.shape.w,
                                  dst_desc.shape.h);
      // validate bbox
      dst_bbox->left -= dst_bbox->left & 1;
      dst_bbox->top -= dst_bbox->top & 1;
      dst_bbox->width -= dst_bbox->width & 1;
      dst_bbox->height -= dst_bbox->height & 1;
      while (dst_bbox->left + dst_bbox->width > dst_desc.shape.w) dst_bbox->width -= 2;
      while (dst_bbox->top + dst_bbox->height > dst_desc.shape.h) dst_bbox->height -= 2;
    }

    params.dst_desc = &dst_desc;

    CnedkTransformConfigParams config;
    memset(&config, 0, sizeof(config));
    config.compute_mode = CNEDK_TRANSFORM_COMPUTE_MLU;
    CnedkTransformSetSessionParams(&config);
    CnedkBufSurfaceMemSet(dst_buf, -1, -1, 0x80);
    if (CnedkTransform(src_buf, dst_buf, &params) < 0) {
      LOG(ERROR) << "[EasyDK Samples] [PreprocYolov3] OnPreproc(): CnedkTransform failed";
      return -1;
    }
    return 0;
  }

 private:
  infer_server::CnPreprocTensorParams params_;
};

#endif
