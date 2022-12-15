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

#include "cnedk_transform_cncv.hpp"

#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "glog/logging.h"
#include "cncv.h"
#include "../common/utils.hpp"

namespace cnedk {

static CnedkBufSurfaceColorFormat GetColorFormatFromTensor(CnedkTransformColorFormat format) {
  static std::map<CnedkTransformColorFormat, CnedkBufSurfaceColorFormat> color_map{
      {CNEDK_TRANSFORM_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_BGR},
      {CNEDK_TRANSFORM_COLOR_FORMAT_RGB, CNEDK_BUF_COLOR_FORMAT_RGB},
      {CNEDK_TRANSFORM_COLOR_FORMAT_BGRA, CNEDK_BUF_COLOR_FORMAT_BGRA},
      {CNEDK_TRANSFORM_COLOR_FORMAT_RGBA, CNEDK_BUF_COLOR_FORMAT_RGBA},
      {CNEDK_TRANSFORM_COLOR_FORMAT_ABGR, CNEDK_BUF_COLOR_FORMAT_ABGR},
      {CNEDK_TRANSFORM_COLOR_FORMAT_ARGB, CNEDK_BUF_COLOR_FORMAT_ARGB},
  };
  if (color_map.find(format) != color_map.end()) return color_map[format];
  return CNEDK_BUF_COLOR_FORMAT_LAST;
}

static int GetChannelNumFromColor(const CnedkBufSurfaceColorFormat &corlor_format) {
  if (corlor_format == CNEDK_BUF_COLOR_FORMAT_RGB || corlor_format == CNEDK_BUF_COLOR_FORMAT_BGR) {
    return 3;
  } else if (corlor_format == CNEDK_BUF_COLOR_FORMAT_BGRA || corlor_format == CNEDK_BUF_COLOR_FORMAT_RGBA ||
             corlor_format == CNEDK_BUF_COLOR_FORMAT_ABGR || corlor_format == CNEDK_BUF_COLOR_FORMAT_ARGB) {
    return 4;
  }
  return -1;
}

int YuvResizeCncvCtx::Process(const CnedkBufSurface &src, CnedkBufSurface *dst,
                               CnedkTransformParams *transform_params) {
  size_t batch_size = src.batch_size;
  if (batch_size_ < batch_size) {
    batch_size_ = batch_size;
    if (mlu_input_) cnrtFree(mlu_input_);
    CNRT_SAFECALL(cnrtMalloc(reinterpret_cast<void **>(&mlu_input_), plane_number_ * batch_size * sizeof(void *)),
                  "[YuvResizeCncvCtx] Process(): malloc mlu input pointers failed.", -1);
    if (mlu_output_) cnrtFree(mlu_output_);
    CNRT_SAFECALL(cnrtMalloc(reinterpret_cast<void **>(&mlu_output_), plane_number_ * batch_size * sizeof(void *)),
                  "[YuvResizeCncvCtx] Process(): malloc mlu output pointers failed.", -1);

    cpu_input_.resize(batch_size * plane_number_);
    cpu_output_.resize(batch_size * plane_number_);
    src_rois_.resize(batch_size);
    src_descs_.resize(batch_size);
    dst_descs_.resize(batch_size);
    dst_rois_.resize(batch_size);
  }

  for (size_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
    // configure src desc
    cncvImageDescriptor src_desc;
    src_desc.width = src.surface_list[batch_idx].width;
    src_desc.height = src.surface_list[batch_idx].height;

    src_desc.pixel_fmt = GetPixFormat(src.surface_list[batch_idx].color_format);
    src_desc.stride[0] = src.surface_list[batch_idx].plane_params.pitch[0];
    src_desc.stride[1] = src.surface_list[batch_idx].plane_params.pitch[1];
    src_desc.depth = CNCV_DEPTH_8U;

    src_descs_[batch_idx] = src_desc;

    // configure src roi
    cncvRect src_roi;
    if (transform_params->transform_flag & CNEDK_TRANSFORM_CROP_SRC) {
      CnedkTransformRect *rect = &transform_params->src_rect[batch_idx];
      src_roi.x = rect->left >= src.surface_list[batch_idx].width ? 0 : rect->left;
      src_roi.y = rect->top >= src.surface_list[batch_idx].height ? 0 : rect->top;
      src_roi.w = rect->width <= 0 ? (src.surface_list[batch_idx].width - rect->left) : rect->width;
      src_roi.h = rect->height <= 0 ? (src.surface_list[batch_idx].height - rect->top) : rect->height;
    } else {
      src_roi.x = 0;
      src_roi.y = 0;
      src_roi.w = src.surface_list[batch_idx].width;
      src_roi.h = src.surface_list[batch_idx].height;
    }
    src_rois_[batch_idx] = src_roi;

    // configure dst desc
    cncvImageDescriptor dst_desc;
    dst_desc.width = dst->surface_list[batch_idx].width;
    dst_desc.height = dst->surface_list[batch_idx].height;

    dst_desc.pixel_fmt = GetPixFormat(dst->surface_list[batch_idx].color_format);
    dst_desc.stride[0] = dst->surface_list[batch_idx].plane_params.pitch[0];
    dst_desc.stride[1] = dst->surface_list[batch_idx].plane_params.pitch[1];

    dst_desc.depth = CNCV_DEPTH_8U;

    dst_descs_[batch_idx] = dst_desc;

    cncvRect dst_roi;
    if (transform_params->transform_flag & CNEDK_TRANSFORM_CROP_DST) {
      CnedkTransformRect *rect = &transform_params->dst_rect[batch_idx];
      dst_roi.x = rect->left >= dst->surface_list[batch_idx].width ? 0 : rect->left;
      dst_roi.y = rect->top >= dst->surface_list[batch_idx].height ? 0 : rect->top;
      dst_roi.w = rect->width <= 0 ? (dst->surface_list[batch_idx].width - rect->left) : rect->width;
      dst_roi.h = rect->height <= 0 ? (dst->surface_list[batch_idx].height - rect->top) : rect->height;
    } else {
      dst_roi.x = 0;
      dst_roi.y = 0;
      dst_roi.w = dst->surface_list[batch_idx].width;
      dst_roi.h = dst->surface_list[batch_idx].height;
    }
    dst_rois_[batch_idx] = dst_roi;

    // copy one input frame addr to device
    cpu_input_[plane_number_ * batch_idx] =
        reinterpret_cast<void **>(reinterpret_cast<uint64_t>(src.surface_list[batch_idx].data_ptr) +
                                  src.surface_list[batch_idx].plane_params.offset[0]);
    cpu_input_[plane_number_ * batch_idx + 1] =
        reinterpret_cast<void **>(reinterpret_cast<uint64_t>(src.surface_list[batch_idx].data_ptr) +
                                  src.surface_list[batch_idx].plane_params.offset[1]);
    cpu_output_[plane_number_ * batch_idx] =
        reinterpret_cast<void **>(reinterpret_cast<uint64_t>(dst->surface_list[batch_idx].data_ptr) +
                                  dst->surface_list[batch_idx].plane_params.offset[0]);
    cpu_output_[plane_number_ * batch_idx + 1] =
        reinterpret_cast<void **>(reinterpret_cast<uint64_t>(dst->surface_list[batch_idx].data_ptr) +
                                  dst->surface_list[batch_idx].plane_params.offset[1]);
  }

  CNRT_SAFECALL(cnrtMemcpy(mlu_input_, cpu_input_.data(), sizeof(void *) * batch_size * plane_number_,
                           CNRT_MEM_TRANS_DIR_HOST2DEV),
                "[YuvResizeCncvCtx] Process(): Copy input pointers H2D failed.", -1);
  CNRT_SAFECALL(cnrtMemcpy(mlu_output_, cpu_output_.data(), sizeof(void *) * batch_size * plane_number_,
                           CNRT_MEM_TRANS_DIR_HOST2DEV),
                "[YuvResizeCncvCtx] Process(): Copy output pointers H2D failed.", -1);

  size_t required_workspace_size = 0;
  CNCV_SAFECALL(cncvGetResizeYuvWorkspaceSize(batch_size, src_descs_.data(), src_rois_.data(), dst_descs_.data(),
                                              dst_rois_.data(), &required_workspace_size),
                "[YuvResizeCncvCtx] Process(): failed", -1);

  if (required_workspace_size != workspace_size_) {
    workspace_size_ = required_workspace_size;
    if (workspace_) cnrtFree(workspace_);
    CNRT_SAFECALL(cnrtMalloc(&(workspace_), required_workspace_size),
                  "[YuvResizeCncvCtx] Process(): malloc workspace failed.", -1);
  }

  CNCV_SAFECALL(cncvResizeYuv_AdvancedROI(handle_, batch_size, src_descs_.data(), src_rois_.data(), mlu_input_,
                                          dst_descs_.data(), dst_rois_.data(), mlu_output_, required_workspace_size,
                                          workspace_, CNCV_INTER_BILINEAR),
                "[YuvResizeCncvCtx] Process():", -1);
  CNRT_SAFECALL(cnrtQueueSync(params_.cnrt_queue),
                "[YuvResizeCncvCtx] Process(): cncvResizeYuv_AdvancedROI failed.", -1);
  return 0;
}

int Yuv2RgbxResizeCncvCtx::Process(const CnedkBufSurface &src, CnedkBufSurface *dst,
                                   CnedkTransformParams *transform_params) {
  size_t batch_size = src.batch_size;
  if (batch_size_ < batch_size) {
    batch_size_ = batch_size;
    if (mlu_input_) cnrtFree(mlu_input_);
    CNRT_SAFECALL(cnrtMalloc(reinterpret_cast<void **>(&mlu_input_), plane_number_ * batch_size * sizeof(void *)),
                  "[Yuv2RgbxResizeCncvCtx] Process(): malloc mlu input pointers failed.", -1);
    if (mlu_output_) cnrtFree(mlu_output_);
    CNRT_SAFECALL(cnrtMalloc(reinterpret_cast<void **>(&mlu_output_), batch_size * sizeof(void *)),
                  "[Yuv2RgbxResizeCncvCtx] Process(): malloc mlu output pointers failed.", -1);

    cpu_input_.resize(plane_number_ * batch_size);
    cpu_output_.resize(batch_size);
    src_rois_.resize(batch_size);
    src_descs_.resize(batch_size);
    dst_descs_.resize(batch_size);
    dst_rois_.resize(batch_size);
  }

  for (size_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
    // configure src desc
    cncvImageDescriptor src_desc;
    src_desc.width = src.surface_list[batch_idx].width;
    src_desc.height = src.surface_list[batch_idx].height;
    src_desc.pixel_fmt = GetPixFormat(src.surface_list[batch_idx].color_format);
    src_desc.stride[0] = src.surface_list[batch_idx].plane_params.pitch[0];
    src_desc.stride[1] = src.surface_list[batch_idx].plane_params.pitch[1];
    src_desc.depth = CNCV_DEPTH_8U;
    src_desc.color_space = CNCV_COLOR_SPACE_BT_601;

    src_descs_[batch_idx] = src_desc;

    // configure src roi
    cncvRect src_roi;
    if (transform_params->transform_flag & CNEDK_TRANSFORM_CROP_SRC) {
      CnedkTransformRect *rect = &transform_params->src_rect[batch_idx];
      src_roi.x = rect->left >= src.surface_list[batch_idx].width ? 0 : rect->left;
      src_roi.y = rect->top >= src.surface_list[batch_idx].height ? 0 : rect->top;
      src_roi.w = rect->width <= 0 ? (src.surface_list[batch_idx].width - rect->left) : rect->width;
      src_roi.h = rect->height <= 0 ? (src.surface_list[batch_idx].height - rect->top) : rect->height;
    } else {
      src_roi.x = 0;
      src_roi.y = 0;
      src_roi.w = src.surface_list[batch_idx].width;
      src_roi.h = src.surface_list[batch_idx].height;
    }
    src_rois_[batch_idx] = src_roi;

    // configure dst desc
    cncvImageDescriptor dst_desc;
    dst_desc.width = dst->surface_list[batch_idx].width;
    dst_desc.height = dst->surface_list[batch_idx].height;
    dst_desc.pixel_fmt = GetPixFormat(dst->surface_list[batch_idx].color_format);
    dst_desc.stride[0] = dst->surface_list[batch_idx].pitch;
    dst_desc.stride[1] = dst->surface_list[batch_idx].pitch;
    dst_desc.depth = CNCV_DEPTH_8U;
    dst_desc.color_space = CNCV_COLOR_SPACE_BT_601;

    dst_descs_[batch_idx] = dst_desc;

    // configure dst roi
    cncvRect dst_roi;
    if (transform_params->transform_flag & CNEDK_TRANSFORM_CROP_DST) {
      CnedkTransformRect *rect = &transform_params->dst_rect[batch_idx];
      dst_roi.x = rect->left >= dst->surface_list[batch_idx].width ? 0 : rect->left;
      dst_roi.y = rect->top >= dst->surface_list[batch_idx].height ? 0 : rect->top;
      dst_roi.w = rect->width <= 0 ? (dst->surface_list[batch_idx].width - rect->left) : rect->width;
      dst_roi.h = rect->height <= 0 ? (dst->surface_list[batch_idx].height - rect->top) : rect->height;
    } else {
      dst_roi.x = 0;
      dst_roi.y = 0;
      dst_roi.w = dst->surface_list[batch_idx].width;
      dst_roi.h = dst->surface_list[batch_idx].height;
    }
    dst_rois_[batch_idx] = dst_roi;

    // copy one input frame addr to device
    cpu_input_[plane_number_ * batch_idx] =
        reinterpret_cast<void **>(reinterpret_cast<uint64_t>(src.surface_list[batch_idx].data_ptr) +
                                  src.surface_list[batch_idx].plane_params.offset[0]);
    cpu_input_[plane_number_ * batch_idx + 1] =
        reinterpret_cast<void **>(reinterpret_cast<uint64_t>(src.surface_list[batch_idx].data_ptr) +
                                  src.surface_list[batch_idx].plane_params.offset[1]);
    cpu_output_[batch_idx] = reinterpret_cast<void **>(dst->surface_list[batch_idx].data_ptr);
  }

  CNRT_SAFECALL(cnrtMemcpy(mlu_input_, cpu_input_.data(), sizeof(void *) * batch_size * plane_number_,
                           CNRT_MEM_TRANS_DIR_HOST2DEV),
                "[Yuv2RgbxResizeCncvCtx] Process(): Copy input pointers H2D failed.", -1);
  CNRT_SAFECALL(cnrtMemcpy(mlu_output_, cpu_output_.data(), sizeof(void *) * batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV),
                "[Yuv2RgbxResizeCncvCtx] Process(): Copy output pointers H2D failed.", -1);

  size_t required_workspace_size = 0;
  CNCV_SAFECALL(cncvGetResizeConvertWorkspaceSize(batch_size, src_descs_.data(), src_rois_.data(), dst_descs_.data(),
                                                  dst_rois_.data(), &required_workspace_size),
                "[Yuv2RgbxResizeCncvCtx] Process():", -1);

  if (required_workspace_size != workspace_size_) {
    workspace_size_ = required_workspace_size;
    if (workspace_) cnrtFree(workspace_);
    CNRT_SAFECALL(cnrtMalloc(&workspace_, required_workspace_size),
                  "[Yuv2RgbxResizeCncvCtx] Process(): malloc workspace failed.", -1);
  }

  CNCV_SAFECALL(cncvResizeConvert_AdvancedROI(handle_, batch_size, src_descs_.data(), src_rois_.data(), mlu_input_,
                dst_descs_.data(), dst_rois_.data(), mlu_output_, workspace_size_, workspace_, CNCV_INTER_BILINEAR),
                "[Yuv2RgbxResizeCncvCtx] Process(): failed", -1);

  CNRT_SAFECALL(cnrtQueueSync(params_.cnrt_queue),
                "[Yuv2RgbxResizeCncvCtx] Process(): cncvResizeConvert_AdvancedROI failed.", -1);
  return 0;
}

int RgbxToYuvCncvCtx::Process(const CnedkBufSurface &src, CnedkBufSurface *dst,
                              CnedkTransformParams *transform_params) {
  for (size_t i = 0; i < src.batch_size; ++i) {
    src_desc_.width = src.surface_list[i].width;
    src_desc_.height = src.surface_list[i].height;
    src_desc_.pixel_fmt = GetPixFormat(src.surface_list[i].color_format);
    src_desc_.stride[0] = src.surface_list[i].pitch;
    src_desc_.depth = CNCV_DEPTH_8U;
    src_desc_.color_space = CNCV_COLOR_SPACE_BT_601;
    if (transform_params->transform_flag & CNEDK_TRANSFORM_CROP_SRC) {
      CnedkTransformRect *rect = &transform_params->src_rect[i];
      src_roi_.x = rect->left >= src.surface_list[i].width ? 0 : rect->left;
      src_roi_.y = rect->top >= src.surface_list[i].height ? 0 : rect->top;
      src_roi_.w = rect->width <= 0 ? (src.surface_list[i].width - rect->left) : rect->width;
      src_roi_.h = rect->height <= 0 ? (src.surface_list[i].height - rect->top) : rect->height;
    } else {
      src_roi_.x = 0;
      src_roi_.y = 0;
      src_roi_.w = src.surface_list[i].width;
      src_roi_.h = src.surface_list[i].height;
    }

    if (transform_params->transform_flag & CNEDK_TRANSFORM_CROP_DST) {  // not supported
    }

    dst_desc_.width = dst->surface_list[i].width;
    dst_desc_.height = dst->surface_list[i].height;
    dst_desc_.pixel_fmt = GetPixFormat(dst->surface_list[i].color_format);
    dst_desc_.stride[0] = dst->surface_list[i].plane_params.pitch[0];
    dst_desc_.stride[1] = dst->surface_list[i].plane_params.pitch[1];
    dst_desc_.depth = CNCV_DEPTH_8U;
    dst_desc_.color_space = CNCV_COLOR_SPACE_BT_601;

    CNCV_SAFECALL(cncvRgbxToYuv_BasicROIP2(handle_, src_desc_, src_roi_, src.surface_list[0].data_ptr, dst_desc_,
                                           reinterpret_cast<char *>(dst->surface_list[i].data_ptr) +
                                           dst->surface_list[i].plane_params.offset[0],
                                           reinterpret_cast<char *>(dst->surface_list[i].data_ptr) +
                                           dst->surface_list[i].plane_params.offset[1]),
                  "[RgbxToYuvCncvCtx] Process(): failed", -1);
    CNRT_SAFECALL(cnrtQueueSync(params_.cnrt_queue),
                  "[RgbxToYuvCncvCtx] Process(): cncvRgbxToYuv_BasicROIP2 failed.", -1);
  }
  return 0;
}

int MeanStdCncvCtx::Process(const CnedkBufSurface &src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) {
  if (dst == nullptr || transform_params == nullptr) {
    LOG(ERROR) << "[EasyDK] [MeanStdCncvCtx] Process(): input is nullptr";
    return -1;
  }

  int channel_num = GetChannelNumFromColor(src.surface_list[0].color_format);
  if (channel_num < 0) {
    LOG(ERROR) << "[EasyDK] [MeanStdCncvCtx] Process(): Unsupported color format";
    return -1;
  }

  if (!(transform_params->transform_flag & CNEDK_TRANSFORM_MEAN_STD)) {
    LOG(ERROR) << "[EasyDK] [MeanStdCncvCtx] Process(): Transform flag is not set, but use mean std operator";
    return -1;
  }

  if ((!transform_params->mean_std_params) || !(transform_params->mean_std_params->mean) ||
      !(transform_params->mean_std_params->std)) {
    LOG(ERROR) << "[EasyDK] [MeanStdCncvCtx] Process(): Mean std parameter is not set";
    return -1;
  }

  mean_ = transform_params->mean_std_params->mean;
  std_ = transform_params->mean_std_params->std;

  src_desc_.width = src.surface_list[0].width;
  src_desc_.height = src.surface_list[0].height;
  src_desc_.pixel_fmt = GetPixFormat(src.surface_list[0].color_format);
  src_desc_.stride[0] = src.surface_list[0].width * channel_num;
  src_desc_.depth = CNCV_DEPTH_8U;

  if (dst->surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_TENSOR) {
    dst_desc_.pixel_fmt = GetPixFormat(GetColorFormatFromTensor(transform_params->dst_desc->color_format));
  } else {
    dst_desc_.pixel_fmt = GetPixFormat(dst->surface_list[0].color_format);
  }

  size_t depth = 0;
  if (transform_params->dst_desc->data_type == CNEDK_TRANSFORM_FLOAT16) {
    dst_desc_.depth = CNCV_DEPTH_16F;
    depth = 2;
  } else if (transform_params->dst_desc->data_type == CNEDK_TRANSFORM_FLOAT32) {
    dst_desc_.depth = CNCV_DEPTH_32F;
    depth = 4;
  } else {
    LOG(ERROR) << "[EasyDK] [MeanStdCncvCtx] Process(): Unsupported data type : "
               << int(transform_params->dst_desc->data_type);
    return -1;
  }

  dst_desc_.width = dst->surface_list[0].width;
  dst_desc_.height = dst->surface_list[0].height;
  dst_desc_.stride[0] = dst->surface_list[0].width * channel_num * depth;

  size_t batch_size = src.batch_size;
  if (batch_size_ < batch_size) {
    batch_size_ = batch_size;
    if (mlu_input_) cnrtFree(mlu_input_);
    CNRT_SAFECALL(cnrtMalloc(reinterpret_cast<void **>(&mlu_input_), batch_size * sizeof(void *)),
                  "[MeanStdCncvCtx] Process(): malloc mlu input pointers failed.", -1);
    if (mlu_output_) cnrtFree(mlu_output_);
    CNRT_SAFECALL(cnrtMalloc(reinterpret_cast<void **>(&mlu_output_), batch_size * sizeof(void *)),
                  "[MeanStdCncvCtx] Process(): malloc mlu output pointers failed.", -1);

    cpu_input_.resize(batch_size);
    cpu_output_.resize(batch_size);
  }

  for (size_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
    // copy one input frame addr to device
    cpu_input_[batch_idx] = reinterpret_cast<void **>(src.surface_list[batch_idx].data_ptr);
    cpu_output_[batch_idx] = reinterpret_cast<void **>(dst->surface_list[batch_idx].data_ptr);
  }

  CNRT_SAFECALL(cnrtMemcpy(mlu_input_, cpu_input_.data(), sizeof(void *) * batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV),
                "[MeanStdCncvCtx] Process(): Copy input pointers H2D failed.", -1);
  CNRT_SAFECALL(cnrtMemcpy(mlu_output_, cpu_output_.data(), sizeof(void *) * batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV),
                "[MeanStdCncvCtx] Process(): Copy output pointers H2D failed.", -1);

  size_t required_workspace_size = 0;

  CNCV_SAFECALL(cncvGetMeanStdWorkspaceSize(channel_num, &required_workspace_size),
                "[MeanStdCncvCtx] Process(): failed", -1);
  if (required_workspace_size != workspace_size_) {
    workspace_size_ = required_workspace_size;
    if (workspace_) cnrtFree(workspace_);
    CNRT_SAFECALL(cnrtMalloc(&workspace_, required_workspace_size),
                  "[EasyDK] [MeanStdCncvCtx] Process(): malloc workspace failed.", -1);
  }

  CNCV_SAFECALL(cncvMeanStd(handle_, batch_size, src_desc_, mlu_input_, mean_, std_, dst_desc_, mlu_output_,
                            required_workspace_size, workspace_),
                "[MeanStdCncvCtx] Process(): failed", -1);
  CNRT_SAFECALL(cnrtQueueSync(params_.cnrt_queue),
                "[MeanStdCncvCtx] Process(): queue sync failed.", -1);
  return true;
}

int Yuv2RgbxResizeWithMeanStdCncv::Process(const CnedkBufSurface &src, CnedkBufSurface *dst,
                                           CnedkTransformParams *transform_params) {
  CnedkBufSurface transform_dst;
  std::vector<CnedkBufSurfaceParams> dst_params;
  dst_params.resize(dst->batch_size);

  CnedkBufSurfaceColorFormat color_format = GetColorFormatFromTensor(transform_params->dst_desc->color_format);

  int channel_num = GetChannelNumFromColor(color_format);
  if (channel_num < 0) {
    LOG(ERROR) << "[EasyDK] [Yuv2RgbxResizeWithMeanStdCncv] Process(): Unsupported color format";
    return -1;
  }

  transform_dst.batch_size = dst->batch_size;
  transform_dst.device_id = dst->device_id;
  transform_dst.is_contiguous = 0;
  transform_dst.mem_type = dst->mem_type;
  transform_dst.num_filled = dst->num_filled;
  transform_dst.pts = dst->pts;

  size_t data_size = 0;
  for (size_t i = 0; i < dst->batch_size; ++i) {
    data_size += dst->surface_list[i].width * dst->surface_list[i].height * channel_num;
  }

  if (mlu_ptr_ == nullptr || mlu_size_ < data_size) {
    if (mlu_ptr_) {
      cnrtFree(mlu_ptr_);
    }
    CNRT_SAFECALL(cnrtMalloc(&mlu_ptr_, data_size),
                  "[Yuv2RgbxResizeWithMeanStdCncv] Process(): malloc mlu temp pointers failed.", -1);
    mlu_size_ = data_size;
  }

  size_t data_offset = 0;
  for (size_t i = 0; i < dst->batch_size; ++i) {
    size_t data_size = dst->surface_list[i].width * dst->surface_list[i].height * channel_num;
    dst_params[i].width = dst->surface_list[i].width;
    dst_params[i].height = dst->surface_list[i].height;
    dst_params[i].pitch = dst->surface_list[i].width * channel_num;
    dst_params[i].color_format = color_format;
    dst_params[i].data_size = data_size;
    dst_params[i].data_ptr = reinterpret_cast<char *>(mlu_ptr_) + data_offset;
    dst_params[i].plane_params = dst->surface_list[i].plane_params;
    data_offset += data_size;
  }

  transform_dst.surface_list = dst_params.data();
  if (resize_convert_->Process(src, &transform_dst, transform_params) < 0) {
    LOG(ERROR) << "[EasyDK] [Yuv2RgbxResizeWithMeanStdCncv] Process(): resize covert failed";
    return -1;
  }
  if (mean_std_->Process(transform_dst, dst, transform_params) < 0) {
    LOG(ERROR) << "[EasyDK] [Yuv2RgbxResizeWithMeanStdCncv] Process(): mean std failed";
    return -1;
  }
  return 0;
}

int Rgbx2YuvResizeAndConvert::Process(const CnedkBufSurface& src,
                                      CnedkBufSurface* dst,
                                      CnedkTransformParams* transform_params) {
  if (dst == nullptr || transform_params == nullptr) {
    LOG(ERROR) << "[EasyDK] [Rgbx2YuvResizeAndConvert] Process(): input is nullptr";
    return -1;
  }

  int channel_num = GetChannelNumFromColor(src.surface_list[0].color_format);
  if (channel_num < 0) {
    LOG(ERROR) << "[EasyDK] [Rgbx2YuvResizeAndConvert] Process(): Unsupported color format";
    return -1;
  }

  if (src.surface_list[0].width != dst->surface_list[0].width ||
      src.surface_list[0].height != dst->surface_list[0].height) {   // rgb2yuv with resize
    // alloc source src yuv mlu addr
    size_t yuv_size = src.surface_list[0].width * src.surface_list[0].height * 3 / 2;
    if (src_yuv_size_ < yuv_size) {
      if (src_yuv_mlu_) {  // realloc
        cnrtFree(src_yuv_mlu_);
      }

      CNRT_SAFECALL(cnrtMalloc(&src_yuv_mlu_, yuv_size),
                    "[Rgbx2YuvResizeAndConvert] Process(): malloc src yuv memory failed.", -1);
      src_yuv_size_ = yuv_size;
    }
    CnedkBufSurface transform_dst;
    CnedkBufSurfaceParams dst_param;
    memset(&transform_dst, 0, sizeof(CnedkBufSurface));
    memset(&dst_param, 0, sizeof(CnedkBufSurfaceParams));

    dst_param.data_ptr = src_yuv_mlu_;
    dst_param.plane_params.num_planes = 2;
    dst_param.plane_params.offset[0] = 0;
    dst_param.plane_params.offset[1] = src.surface_list[0].width * src.surface_list[0].height;

    dst_param.pitch = src.surface_list[0].width;
    dst_param.width = src.surface_list[0].width;
    dst_param.height = src.surface_list[0].height;
    dst_param.plane_params.num_planes = 2;
    dst_param.plane_params.pitch[0] = src.surface_list[0].width;
    dst_param.plane_params.pitch[1] = src.surface_list[0].width;
    dst_param.color_format = dst->surface_list[0].color_format;
    dst_param.data_size = dst_param.pitch * dst_param.height * 3 / 2;

    transform_dst.batch_size = 1;
    transform_dst.num_filled = 1;
    transform_dst.surface_list = &dst_param;
    transform_dst.device_id = dev_id_;
    transform_dst.mem_type = CNEDK_BUF_MEM_DEVICE;
    CnedkTransformParams temp_transform_params;
    memset(&temp_transform_params, 0, sizeof(temp_transform_params));
    if (rgbx_yuv_->Process(src, &transform_dst, &temp_transform_params) < 0) {
      LOG(ERROR) << "[EasyDK] [Rgbx2YuvResizeAndConvert] Process(): rgb2yuv failed";
      return -1;
    }
    if (yuv_resize_->Process(transform_dst, dst, transform_params) < 0) {
      LOG(ERROR) << "[EasyDK] [Rgbx2YuvResizeAndConvert] Process(): yuv resize failed";
      return -1;
    }
  } else {   // bgr2yuv without resize
    if (rgbx_yuv_->Process(src, dst, transform_params) < 0) {
      LOG(ERROR) << "[EasyDK] [Rgbx2YuvResizeAndConvert] Process(): rgbx convert to yuv failed";
      return -1;
    }
  }
  return 0;
}

int GetBufSurfaceFromTensor(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformTensorDesc *tensor_desc) {
  if (GetColorFormatFromTensor(tensor_desc->color_format) == CNEDK_BUF_COLOR_FORMAT_LAST) return -1;

  dst->num_filled = src->num_filled;
  dst->batch_size = src->batch_size;  // tensor_desc->shape.n;
  dst->pts = src->pts;
  dst->device_id = src->device_id;
  dst->mem_type = src->mem_type;

  for (size_t i = 0; i < src->batch_size; ++i) {
    dst->surface_list[i].width = tensor_desc->shape.w;
    dst->surface_list[i].height = tensor_desc->shape.h;
    dst->surface_list[i].pitch = tensor_desc->shape.w * tensor_desc->shape.c;
    dst->surface_list[i].color_format = GetColorFormatFromTensor(tensor_desc->color_format);
    dst->surface_list[i].data_size = src->surface_list[i].data_size;
    dst->surface_list[i].data_ptr = src->surface_list[i].data_ptr;
    dst->surface_list[i].plane_params = src->surface_list[i].plane_params;
  }
  return 0;
}

bool IsYuv420sp(CnedkBufSurfaceColorFormat fmt) {
  if (fmt == CNEDK_BUF_COLOR_FORMAT_NV12 || fmt == CNEDK_BUF_COLOR_FORMAT_NV21) {
    return true;
  }
  return false;
}

bool IsRgbx(CnedkBufSurfaceColorFormat fmt) {
  if (fmt == CNEDK_BUF_COLOR_FORMAT_RGB || fmt == CNEDK_BUF_COLOR_FORMAT_BGR ||
      fmt == CNEDK_BUF_COLOR_FORMAT_RGBA || fmt == CNEDK_BUF_COLOR_FORMAT_BGRA ||
      fmt == CNEDK_BUF_COLOR_FORMAT_ABGR || fmt == CNEDK_BUF_COLOR_FORMAT_ARGB) {
    return true;
  }
  return false;
}

int DoCncvTransform(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) {
  if (src->surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_TENSOR) {
    LOG(ERROR) << "[EasyDK] DoCncvTransform(): The type of src is not supported as tensor";
    return -1;
  }
  CnedkBufSurface* dst_buf = dst;
  std::unique_ptr<CnedkBufSurface> dst_buf_for_tensor = nullptr;
  std::vector<CnedkBufSurfaceParams> dst_params_for_tensor;

  if (dst->surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_TENSOR) {
    if (transform_params->transform_flag & CNEDK_TRANSFORM_MEAN_STD) {
      auto dst_fmt = GetColorFormatFromTensor(transform_params->dst_desc->color_format);
      if (IsYuv420sp(src->surface_list[0].color_format) && IsRgbx(dst_fmt)) {
        static thread_local std::shared_ptr<Yuv2RgbxResizeWithMeanStdCncv> resize_meanstd_ctx = nullptr;
        if (!resize_meanstd_ctx) {
          int dev_id = 0;
          cnrtGetDevice(&dev_id);
          resize_meanstd_ctx = std::make_shared<Yuv2RgbxResizeWithMeanStdCncv>(dev_id);
        }
        if (resize_meanstd_ctx->Process(*src, dst, transform_params) < 0) {
          LOG(ERROR) << "[EasyDK] DoCncvTransform(): Yuv2RgbxResizeWithMeanStd failed";
          return -1;
        }
      } else if (src->surface_list[0].color_format == dst_fmt) {
        static thread_local std::shared_ptr<CncvContext> cncv_ctx = nullptr;
        if (!cncv_ctx) {
          int dev_id = 0;
          cnrtGetDevice(&dev_id);
          cncv_ctx = std::make_shared<MeanStdCncvCtx>(dev_id);
        }
        if (cncv_ctx->Process(*src, dst, transform_params) < 0) {
          LOG(ERROR) << "[EasyDK] DoCncvTransform(): MeanStd failed";
          return -1;
        }
      } else {
        LOG(ERROR) << "[EasyDK] DoCncvTransform(): Unsupported transform type";
        return -1;
      }
      return 0;
    }

    dst_buf_for_tensor.reset(new CnedkBufSurface());
    memset(dst_buf_for_tensor.get(), 0, sizeof(CnedkBufSurface));

    int dst_batch_size = transform_params->dst_desc->shape.n;
    dst_params_for_tensor.resize(dst_batch_size);
    dst_buf_for_tensor->surface_list = dst_params_for_tensor.data();

    if (GetBufSurfaceFromTensor(dst, dst_buf_for_tensor.get(), transform_params->dst_desc) < 0) {
      LOG(ERROR) << "[EasyDK] DoCncvTransform(): Get BufSurface according to tensor failed";
      return -1;
    }
    dst_buf = dst_buf_for_tensor.get();
  }

  if (IsRgbx(src->surface_list[0].color_format) &&
      IsYuv420sp(dst_buf->surface_list[0].color_format)) {
    static thread_local std::shared_ptr<Rgbx2YuvResizeAndConvert> cncv_ctx = nullptr;
    if (!cncv_ctx) {
      int dev_id = 0;
      cnrtGetDevice(&dev_id);
      cncv_ctx = std::make_shared<Rgbx2YuvResizeAndConvert>(dev_id);
    }
    if (cncv_ctx->Process(*src, dst_buf, transform_params) < 0) {
      LOG(ERROR) << "[EasyDK] DoCncvTransform(): RgbxToYuv failed";
      return -1;
    }
  } else if (IsYuv420sp(src->surface_list[0].color_format) &&
             IsRgbx(dst_buf->surface_list[0].color_format)) {
    static thread_local std::shared_ptr<CncvContext> cncv_ctx = nullptr;
    if (!cncv_ctx) {
      int dev_id = 0;
      cnrtGetDevice(&dev_id);
      cncv_ctx = std::make_shared<Yuv2RgbxResizeCncvCtx>(dev_id);
    }
    if (cncv_ctx->Process(*src, dst_buf, transform_params) < 0) {
      LOG(ERROR) << "[EasyDK] DoCncvTransform(): RgbxToYuvResize failed";
      return -1;
    }
  } else if (IsYuv420sp(src->surface_list[0].color_format) &&
             src->surface_list[0].color_format == dst_buf->surface_list[0].color_format) {
    static thread_local std::shared_ptr<CncvContext> cncv_ctx = nullptr;
    if (!cncv_ctx) {
      int dev_id = 0;
      cnrtGetDevice(&dev_id);
      cncv_ctx = std::make_shared<YuvResizeCncvCtx>(dev_id);
    }
    if (cncv_ctx->Process(*src, dst_buf, transform_params) < 0) {
      LOG(ERROR) << "[EasyDK] DoCncvTransform(): YuvResize failed";
      return -1;
    }
  } else {
    LOG(ERROR) << "[EasyDK] DoCncvTransform(): Unsupported transform type src: "
               << static_cast<int>(src->surface_list[0].color_format)
               << ", dst: " << static_cast<int>(dst->surface_list[0].color_format);
    return -1;
  }
  return 0;
}

int CncvTransform(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) {
  return DoCncvTransform(src, dst, transform_params);
}

}  // namespace cnedk
