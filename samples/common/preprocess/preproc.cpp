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
#ifdef HAVE_CNCV
#include "cncv.h"
#endif
#include "cnis/contrib/video_helper.h"
#include "cnis/infer_server.h"
#include "cxxutil/log.h"

#include "preproc.h"

#ifdef HAVE_CNCV
cncvPixelFormat GetCncvPixFmt(edk::PixelFmt fmt) {
  switch (fmt) {
    case edk::PixelFmt::I420:
      return CNCV_PIX_FMT_I420;
    case edk::PixelFmt::NV12:
      return CNCV_PIX_FMT_NV12;
    case edk::PixelFmt::NV21:
      return CNCV_PIX_FMT_NV21;
    case edk::PixelFmt::BGR24:
      return CNCV_PIX_FMT_BGR;
    case edk::PixelFmt::RGB24:
      return CNCV_PIX_FMT_RGB;
    case edk::PixelFmt::BGRA:
      return CNCV_PIX_FMT_BGRA;
    case edk::PixelFmt::RGBA:
      return CNCV_PIX_FMT_RGBA;
    case edk::PixelFmt::ABGR:
      return CNCV_PIX_FMT_ABGR;
    case edk::PixelFmt::ARGB:
      return CNCV_PIX_FMT_ARGB;
    default:
      LOGE(SAMPLE) << "Unsupported input format.";
      return CNCV_PIX_FMT_INVALID;
  }
}

uint32_t GetCncvDepthSize(cncvDepth_t depth) {
  switch (depth) {
    case CNCV_DEPTH_8U:
    case CNCV_DEPTH_8S:
      return 1;
    case CNCV_DEPTH_16U:
    case CNCV_DEPTH_16S:
    case CNCV_DEPTH_16F:
      return 2;
    case CNCV_DEPTH_32U:
    case CNCV_DEPTH_32S:
    case CNCV_DEPTH_32F:
      return 4;
    default:
      LOGE(SAMPLE) << "Unsupported Depth, Size = 0 by default.";
      return 0;
  }
}

void SetCncvStride(cncvImageDescriptor* desc) {
  int depth = GetCncvDepthSize(desc->depth);
  switch (desc->pixel_fmt) {
    case CNCV_PIX_FMT_I420:
      desc->stride[0] = depth * desc->width;
      desc->stride[1] = depth * desc->width / 2;
      desc->stride[2] = depth * desc->width / 2;
      break;
    case CNCV_PIX_FMT_NV12:
    case CNCV_PIX_FMT_NV21:
      desc->stride[0] = depth * desc->width;
      desc->stride[1] = depth * desc->width;
      break;
    case CNCV_PIX_FMT_BGR:
    case CNCV_PIX_FMT_RGB:
      desc->stride[0] = depth * desc->width * 3;
      break;
    case CNCV_PIX_FMT_BGRA:
    case CNCV_PIX_FMT_RGBA:
    case CNCV_PIX_FMT_ABGR:
    case CNCV_PIX_FMT_ARGB:
      desc->stride[0] = depth * desc->width * 4;
      break;
    default:
      LOGE(SAMPLE) << "Unsupported input format.";
      return;
  }
}

#endif
