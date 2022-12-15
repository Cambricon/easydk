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

#include <memory>

#include "sample_osd.hpp"

#include "libyuv.h"


SampleOsd::~SampleOsd() {
}

int SampleOsd::Open() {
  return 0;
}

int SampleOsd::Close() {
  return 0;
}

int SampleOsd::Process(std::shared_ptr<EdkFrame> frame) {
  std::unique_lock<std::mutex> guard(cnosd_mutex_);
  if (!osd_ctx_.count(frame->stream_id)) {
    osd_ctx_[frame->stream_id] = std::make_shared<CnOsd>();
    osd_ctx_[frame->stream_id]->LoadLabels(lable_path_);
  }
  guard.unlock();

  if (!frame->is_eos) {
    cnedk::BufSurfWrapperPtr surf = frame->surf;
    CnedkBufSurfaceSyncForCpu(surf->GetBufSurface(), -1, -1);
    uint8_t* y_plane = static_cast<uint8_t*>(surf->GetHostData(0));
    uint8_t* uv_plane = static_cast<uint8_t*>(surf->GetHostData(1));

    int width = surf->GetWidth();
    int height = surf->GetHeight();
    if (width <= 0 || height <= 1) {
      LOG(ERROR) << "[EasyDK Sample] [Osd] Invalid width or height, width = " << width << ", height = " << height;
      return -1;
    }
    height = height & (~static_cast<int>(1));

    int y_stride = surf->GetStride(0);
    int uv_stride = surf->GetStride(1);
    cv::Mat bgr(height, width, CV_8UC3);
    uint8_t* dst_bgr24 = bgr.data;
    int dst_stride = width * 3;

    CnedkBufSurfaceColorFormat fmt = surf->GetColorFormat();
    // kYvuH709Constants make it to BGR
    if (fmt == CNEDK_BUF_COLOR_FORMAT_NV21)
      libyuv::NV21ToRGB24Matrix(y_plane, y_stride, uv_plane, uv_stride, dst_bgr24, dst_stride,
                                &libyuv::kYvuH709Constants, width, height);
    else
      libyuv::NV12ToRGB24Matrix(y_plane, y_stride, uv_plane, uv_stride, dst_bgr24, dst_stride,
                               &libyuv::kYvuH709Constants, width, height);


    osd_ctx_[frame->stream_id]->DrawLabel(bgr, frame->objs);

    if (fmt == CNEDK_BUF_COLOR_FORMAT_NV21) {
      libyuv::RGB24ToNV21(bgr.data, width * 3, y_plane, y_stride, uv_plane, uv_stride, width, height);
    } else if (fmt == CNEDK_BUF_COLOR_FORMAT_NV12) {
      libyuv::RGB24ToNV12(bgr.data, width * 3, y_plane, y_stride, uv_plane, uv_stride, width, height);
    } else {
      LOG(ERROR) << "[EasyDK Sample] [Osd] fmt not supported yet.";
    }
    frame->surf->SyncHostToDevice();
  }

  Transmit(frame);

  return 0;
}
