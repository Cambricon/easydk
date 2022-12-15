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
#include "../mps_service.hpp"

#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "glog/logging.h"

#include "cn_api.h"
#include "cn_comm_g2d.h"
#include "cn_comm_vgu.h"
#include "cn_g2d.h"
#include "cn_vgu.h"

#include "mps_service_impl.hpp"

namespace cnedk {

MpsService::MpsService() { impl_.reset(new MpsServiceImpl()); }

MpsService::~MpsService() {
  if (impl_) {
    impl_->Destroy();
  }
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  cnrtVBExit();
#else
  cnVBExit();
#endif
}

int MpsService::Init(const MpsServiceConfig &config) {
  if (impl_) {
    cnInit(0);
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
    cnrtVBExit();
#else
    cnVBExit();
#endif
    cnsampleCommSysPrintVersion();
    return impl_->Init(config);
  }
  return -1;
}

void MpsService::Destroy() {
  if (impl_) {
    impl_->Destroy();
    impl_.reset();
    impl_ = nullptr;
  }
}

int MpsService::GetVoutSize(int *w, int *h) {
  if (impl_) {
    return impl_->GetVoutSize(w, h);
  }
  return -1;
}

int MpsService::VoutSendFrame(cnVideoFrameInfo_t *info) {
  if (impl_) {
    return impl_->VoutSendFrame(info);
  }
  return -1;
}

int MpsService::GetVinSize(int sensor_id, int *w, int *h) {
  if (impl_) {
    return impl_->GetVinOutSize(sensor_id, w, h);
  }
  return -1;
}

int MpsService::VinCaptureFrame(int sensor_id, cnVideoFrameInfo_t *info, int timeout_ms) {
  if (impl_) {
    return impl_->VinCaptureFrame(sensor_id, info, timeout_ms);
  }
  return -1;
}
int MpsService::VinCaptureFrameRelease(int sensor_id, cnVideoFrameInfo_t *info) {
  if (impl_) {
    return impl_->VinCaptureFrameRelease(sensor_id, info);
  }
  return -1;
}

// vdecs (vdec + vpps or vdec only)
void *MpsService::CreateVDec(IVDecResult *result, cnEnPayloadType_t type, int max_width, int max_height, int buf_num,
                             cnEnPixelFormat_t pix_fmt) {
  if (impl_) {
    return impl_->CreateVDec(result, type, max_width, max_height, buf_num, pix_fmt);
  }
  return nullptr;
}
int MpsService::DestroyVDec(void *handle) {
  if (impl_) {
    return impl_->DestroyVDec(handle);
  }
  return -1;
}
int MpsService::VDecSendStream(void *handle, const cnvdecStream_t *pst_stream, cnS32_t milli_sec) {
  if (impl_) {
    return impl_->VDecSendStream(handle, pst_stream, milli_sec);
  }
  return -1;
}

int MpsService::VDecReleaseFrame(void *handle, const cnVideoFrameInfo_t *info) {
  if (impl_) {
    return impl_->VDecReleaseFrame(handle, info);
  }
  return -1;
}

// vencs
void *MpsService::CreateVEnc(IVEncResult *result, VencCreateParam *params) {
  if (impl_) {
    return impl_->CreateVEnc(result, params);
  }
  return nullptr;
}
int MpsService::DestroyVEnc(void *handle) {
  if (impl_) {
    return impl_->DestroyVEnc(handle);
  }
  return -1;
}

int MpsService::VEncSendFrame(void *handle, const cnVideoFrameInfo_t *pst_frame, cnS32_t milli_sec) {
  if (impl_) {
    return impl_->VEncSendFrame(handle, pst_frame, milli_sec);
  }
  return -1;
}

// vgu resize-convert
//
static std::mutex vgu_mutex;  // there is only one VGU instance.
int MpsService::VguScaleCsc(const cnVideoFrameInfo_t *src, cnVideoFrameInfo_t *dst) {
  if (!src || !dst) {
    return -1;
  }

  int ret = 0;
  vguTaskAttr_t vgu_scale_task;
  memset(&vgu_scale_task, 0, sizeof(vgu_scale_task));
  memcpy(&vgu_scale_task.stImgIn, src, sizeof(cnVideoFrameInfo_t));
  memcpy(&vgu_scale_task.stImgOut, dst, sizeof(cnVideoFrameInfo_t));

  // std::unique_lock<std::mutex> lock(vgu_mutex);
  vguHandle_t vgu_handle;
  cnS32_t cn_ret = cnvguBeginJob(&vgu_handle);
  if (cn_ret != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsService] VguScaleCsc(): cnvguBeginJob failed, ret = " << cn_ret;
    return -1;
  }

  cn_ret = cnvguAddScaleTask(vgu_handle, &vgu_scale_task);
  if (cn_ret != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsService] VguScaleCsc(): cnvguAddScaleTask failed, ret = " << cn_ret;
    ret = -1;
  }

  cn_ret = cnvguEndJob(vgu_handle);
  if (cn_ret != CN_SUCCESS) {
    cnvguCancelJob(vgu_handle);
    LOG(ERROR) << "[EasyDK] [MpsService] VguScaleCsc(): cnvguEndJob failed, ret = " << cn_ret;
    return -1;
  }
  return ret;
}

// static std::mutex g2d_mutex;
int MpsService::OsdDrawBboxes(const cnVideoFrameInfo_t *info,
                              const std::vector<std::tuple<Bbox, cnU32_t, cnU32_t>> &bboxes) {
  cnS32_t cn_ret = CN_SUCCESS;

  cng2dSurface_s dst_surface;
  memset(&dst_surface, 0, sizeof(dst_surface));
  switch (info->stVFrame.enPixelFormat) {
    case PIXEL_FORMAT_YUV420_8BIT_SEMI_UV:
      dst_surface.enColorFmt = CN_G2D_COLOR_FMT_NV12;
      break;
    case PIXEL_FORMAT_YUV420_8BIT_SEMI_VU:
      dst_surface.enColorFmt = CN_G2D_COLOR_FMT_NV21;
      break;
    default:
      LOG(ERROR) << "[EasyDK] [MpsService] OsdDrawBboxes(): Unsupported pixel format, only NV12/nv21 is supported";
      return -1;
  }
  dst_surface.u32Height = info->stVFrame.u32Height;
  dst_surface.u32Width = info->stVFrame.u32Width;
  dst_surface.u64DevAddr = info->stVFrame.u64PhyAddr[0];
  dst_surface.u32Stride = info->stVFrame.u32Stride[0];
  dst_surface.u64UDevAddr = info->stVFrame.u64PhyAddr[1];
  dst_surface.u32UStride = info->stVFrame.u32Stride[1];

  Bbox bbox;
  cnU32_t line_width, color;

  std::map<cnU32_t, std::vector<cng2dRect_s>> rects_map;
  for (auto &it : bboxes) {
    std::tie(bbox, line_width, color) = it;

    cnU32_t x1 = bbox.x;
    cnU32_t y1 = bbox.y;
    cnU32_t w = bbox.w;
    cnU32_t h = bbox.h;
    // FIXME, validate rect for hw
    x1 -= x1 & 1;
    y1 -= y1 & 1;
    // FIXME, validate rect for hw
    w -= w & 1;
    h -= h & 1;
    while (x1 + w >= info->stVFrame.u32Width) w -= 2;
    while (y1 + h >= info->stVFrame.u32Height) h -= 2;
    if (w == 0 || h == 0) {
      continue;
    }
    // linewidth must be multiple of 2 for NV12/NV21
    line_width -= line_width & 1;

    cng2dRect_s rect;
    memset(&rect, 0, sizeof(rect));
    rect.u32Xpos = x1;
    rect.u32Ypos = y1;
    rect.u32Width = w;
    rect.u32Height = h;

    if (rects_map.count(color)) {
      rects_map[color].push_back(rect);
    } else {
      std::vector<cng2dRect_s> info;
      info.push_back(rect);
      rects_map[color] = info;
    }
  }

  constexpr uint32_t kMaxOsdNum = 32;
  cng2dRect_s rect[kMaxOsdNum];

  for (auto &it : rects_map) {
    auto color = it.first;
    uint32_t num = 0;
    for (auto &v : it.second) {
      if (num < kMaxOsdNum) rect[num++] = v;
      if (num == kMaxOsdNum) {
        // std::unique_lock<std::mutex> guard(g2d_mutex);
        cn_ret = cng2dDrawRect(&dst_surface, rect, num, line_width, color);
        if (cn_ret != CN_SUCCESS) {
          LOG(ERROR) << "[EasyDK] [MpsService] OsdDrawBboxes(): cng2dDrawRect failed, ret = " << cn_ret;
          return -1;
        }
        // Return until all commands submitted this time finish or timeout if boolBlock is CN_TRUE
        // Return immediately if boolBlock is CN_FALSE
        cn_ret = cng2dSubmit(0, CN_TRUE, 1000);
        if (cn_ret != CN_SUCCESS) {
          LOG(ERROR) << "[EasyDK] [MpsService] OsdDrawBboxes(): cng2dSubmit failed, ret = " << cn_ret;
          return -1;
        }

        num = 0;
      }
    }

    if (num > 0) {
      // std::unique_lock<std::mutex> guard(g2d_mutex);
      cn_ret = cng2dDrawRect(&dst_surface, rect, num, line_width, color);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdDrawBboxes(): cng2dDrawRect failed, ret = " << cn_ret;
        return -1;
      }
      // Return until all commands submitted this time finish or timeout if boolBlock is CN_TRUE
      // Return immediately if boolBlock is CN_FALSE
      cn_ret = cng2dSubmit(0, CN_TRUE, 1000);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdDrawBboxes(): cng2dSubmit failed, ret = " << cn_ret;
        return -1;
      }
    }
  }

#if 1
  // Return until all commands finish
  cn_ret = cng2dWaitAllDone();
  if (cn_ret != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsService] OsdDrawBboxes(): cng2dWaitAllDone failed, ret = " << cn_ret;
    return -1;
  }
#endif
  return 0;
}

int MpsService::OsdFillBboxes(const cnVideoFrameInfo_t *info, const std::vector<std::pair<Bbox, cnU32_t>> &bboxes) {
  cnS32_t cn_ret = CN_SUCCESS;
  if (info->stVFrame.enPixelFormat != PIXEL_FORMAT_YUV420_8BIT_SEMI_UV ||
      info->stVFrame.enPixelFormat != PIXEL_FORMAT_YUV420_8BIT_SEMI_VU) {
    LOG(ERROR) << "[EasyDK] [MpsService] OsdFillBboxes(): Unsupported pixel format, only NV12/NV21 is supported";
    return -1;
  }

  constexpr uint32_t kMaxOsdNum = 32;
  vguDrawLineArrayAttr_t vgu_draw_line_attrs;
  memset(&vgu_draw_line_attrs, 0, sizeof(vguDrawLineArrayAttr_t));

  for (auto &points : bboxes) {
    Bbox bbox = points.first;

    cnU32_t x1 = bbox.x;
    cnU32_t y1 = bbox.y;
    cnU32_t w = bbox.w;
    cnU32_t h = bbox.h;
    // FIXME, validate rect for hw
    x1 -= x1 & 1;
    y1 -= y1 & 1;
    w -= w & 1;
    h -= h & 1;
    while (x1 + w >= info->stVFrame.u32Width) w -= 2;
    while (y1 + h >= info->stVFrame.u32Height) h -= 2;
    if (w == 0 || h == 0) {
      continue;
    }

    if (vgu_draw_line_attrs.u32Num < kMaxOsdNum) {
      vgu_draw_line_attrs.astRect[vgu_draw_line_attrs.u32Num].s32X = x1;
      vgu_draw_line_attrs.astRect[vgu_draw_line_attrs.u32Num].s32Y = y1;
      vgu_draw_line_attrs.astRect[vgu_draw_line_attrs.u32Num].u32Width = w;
      vgu_draw_line_attrs.astRect[vgu_draw_line_attrs.u32Num].u32Height = h;
      vgu_draw_line_attrs.u32Color[vgu_draw_line_attrs.u32Num] = points.second;
      vgu_draw_line_attrs.u32Num++;
    }

    if (vgu_draw_line_attrs.u32Num == kMaxOsdNum) {
      vguHandle_t vgu_handle;
      vguTaskAttr_t vgu_osd_task;
      memset(&vgu_osd_task, 0, sizeof(vgu_osd_task));
      memcpy(&vgu_osd_task.stImgIn, info, sizeof(cnVideoFrameInfo_t));
      memcpy(&vgu_osd_task.stImgOut, info, sizeof(cnVideoFrameInfo_t));

      cn_ret = cnvguBeginJob(&vgu_handle);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdFillBboxes(): cnvguBeginJob failed, ret = " << cn_ret;
        return -1;
      }

      cn_ret = cnvguAddDrawLineTaskArray(vgu_handle, &vgu_osd_task, &vgu_draw_line_attrs);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdFillBboxes(): cnvguAddDrawLineTaskArray failed, ret = " << cn_ret;
        return -1;
      }

      cn_ret = cnvguEndJob(vgu_handle);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdFillBboxes(): cnvguEndJob failed, ret = " << cn_ret;
        return -1;
      }
      vgu_draw_line_attrs.u32Num = 0;
    }
  }

  if (vgu_draw_line_attrs.u32Num > 0) {
    vguHandle_t vgu_handle;
    vguTaskAttr_t vgu_osd_task;
    memset(&vgu_osd_task, 0, sizeof(vgu_osd_task));
    memcpy(&vgu_osd_task.stImgIn, info, sizeof(cnVideoFrameInfo_t));
    memcpy(&vgu_osd_task.stImgOut, info, sizeof(cnVideoFrameInfo_t));

    cn_ret = cnvguBeginJob(&vgu_handle);
    if (cn_ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsService] OsdFillBboxes(): cnvguBeginJob failed, ret = " << cn_ret;
      return -1;
    }

    cn_ret = cnvguAddDrawLineTaskArray(vgu_handle, &vgu_osd_task, &vgu_draw_line_attrs);
    if (cn_ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsService] OsdFillBboxes(): cnvguAddDrawLineTaskArray failed, ret = " << cn_ret;
      return -1;
    }

    cn_ret = cnvguEndJob(vgu_handle);
    if (cn_ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsService] OsdFillBboxes(): cnvguEndJob failed, ret = " << cn_ret;
      return -1;
    }
  }
  return 0;
}

int MpsService::OsdPutText(const cnVideoFrameInfo_t *info,
                           const std::vector<std::tuple<Bbox, void *, cnU32_t, cnU32_t>> &texts) {
  if (info->stVFrame.enPixelFormat != PIXEL_FORMAT_YUV420_8BIT_SEMI_UV ||
      info->stVFrame.enPixelFormat != PIXEL_FORMAT_YUV420_8BIT_SEMI_VU) {
    LOG(ERROR) << "[EasyDK] [MpsService] OsdPutText(): Unsupported pixel format, only NV12/NV21 is supported";
    return -1;
  }

  cnS32_t cn_ret = CN_SUCCESS;
  constexpr uint32_t kMaxOsdNum = 32;
  vguOsdAttr_t vgu_osd_attrs[kMaxOsdNum];

  Bbox dst_rect;
  void *argb1555;
  cnU32_t bg_color;
  cnU32_t pitch;
  cnU32_t idx = 0;

  for (auto &it : texts) {
    std::tie(dst_rect, argb1555, bg_color, pitch) = it;

    cnU32_t x1 = dst_rect.x;
    cnU32_t y1 = dst_rect.y;
    cnU32_t w = dst_rect.w;
    cnU32_t h = dst_rect.h;
    // FIXME, validate rect for hw;
    //    w & x : assumed that have been aligned
    y1 -= y1 & 1;
    h -= h & 1;
    while (y1 + h >= info->stVFrame.u32Height) h -= 2;
    if (w == 0 || h == 0) {
      LOG(WARNING) << "[EasyDK] [MpsService] OsdPutText(): rect is null";
      continue;
    }

    cnRect_t st_dst_rect;
    st_dst_rect.s32X = x1;
    st_dst_rect.s32Y = y1;
    st_dst_rect.u32Width = w;
    st_dst_rect.u32Height = h;

    if (idx < kMaxOsdNum) {
      vguOsdAttr_t *st_osd_attr = &vgu_osd_attrs[idx++];
      memset(st_osd_attr, 0, sizeof(vguOsdAttr_t));
      st_osd_attr->enPixelFmt = PIXEL_FORMAT_ARGB1555_PACKED;
      st_osd_attr->u32BgColor = bg_color;
      st_osd_attr->u32FgAlpha = 128;
      st_osd_attr->u32BgAlpha = 0;
      st_osd_attr->u64PhyAddr = reinterpret_cast<cnU64_t>(argb1555);
      st_osd_attr->u32Stride = pitch;
      st_osd_attr->stRect = st_dst_rect;
    }

    if (idx == kMaxOsdNum) {
      vguHandle_t vgu_handle;
      vguTaskAttr_t vgu_osd_task;
      memset(&vgu_osd_task, 0, sizeof(vgu_osd_task));
      memcpy(&vgu_osd_task.stImgIn, info, sizeof(cnVideoFrameInfo_t));
      memcpy(&vgu_osd_task.stImgOut, info, sizeof(cnVideoFrameInfo_t));

      cn_ret = cnvguBeginJob(&vgu_handle);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdPutText(): cnvguBeginJob failed, ret = " << cn_ret;
        return -1;
      }
      cn_ret = cnvguAddOsdTaskArray(vgu_handle, &vgu_osd_task, vgu_osd_attrs, idx);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdPutText(): cnvguEncnvguAddOsdTaskArraydJob failed, ret = " << cn_ret;
        return -1;
      }
      cn_ret = cnvguEndJob(vgu_handle);
      if (cn_ret != CN_SUCCESS) {
        LOG(ERROR) << "[EasyDK] [MpsService] OsdPutText(): cnvguEndJob failed, ret = " << cn_ret;
        return -1;
      }
      idx = 0;
    }
  }

  if (idx > 0) {
    vguHandle_t vgu_handle;
    vguTaskAttr_t vgu_osd_task;
    memset(&vgu_osd_task, 0, sizeof(vgu_osd_task));
    memcpy(&vgu_osd_task.stImgIn, info, sizeof(cnVideoFrameInfo_t));
    memcpy(&vgu_osd_task.stImgOut, info, sizeof(cnVideoFrameInfo_t));

    cn_ret = cnvguBeginJob(&vgu_handle);
    if (cn_ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsService] OsdPutText(): cnvguBeginJob failed, ret = " << cn_ret;
      return -1;
    }
    cn_ret = cnvguAddOsdTaskArray(vgu_handle, &vgu_osd_task, vgu_osd_attrs, idx);
    if (cn_ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsService] OsdPutText(): cnvguAddOsdTaskArray failed, ret = " << cn_ret;
      cnvguEndJob(vgu_handle);
      return -1;
    }
    cn_ret = cnvguEndJob(vgu_handle);
    if (cn_ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsService] OsdPutText(): cnvguEndJob failed, ret = " << cn_ret;
      return -1;
    }
  }

  return 0;
}

}  // namespace cnedk
