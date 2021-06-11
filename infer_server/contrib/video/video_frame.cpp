/*************************************************************************
 * Copyright (C) [2020] by Cambricon, Inc. All rights reserved
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
#include <algorithm>

#include "video_helper.h"

namespace infer_server {
namespace video {

size_t GetPlaneNum(PixelFmt format) noexcept {
  switch (format) {
    case PixelFmt::NV12:
    case PixelFmt::NV21:
      return 2;
    case PixelFmt::BGR24:
    case PixelFmt::RGB24:
    case PixelFmt::ABGR:
    case PixelFmt::ARGB:
    case PixelFmt::BGRA:
    case PixelFmt::RGBA:
      return 1;
    case PixelFmt::I420:
      return 3;
    default:
      return 0;
  }
}

size_t VideoFrame::GetPlaneSize(size_t plane_idx) const noexcept {
  if (plane_idx >= GetPlaneNum(format)) return 0;
  size_t stride0 = stride[0] < width ? width : stride[0];
  switch (format) {
    case PixelFmt::BGR24:
    case PixelFmt::RGB24:
      return height * stride0 * 3;
    case PixelFmt::NV12:
    case PixelFmt::NV21:
      if (plane_idx == 0) {
        return height * stride0;
      } else if (plane_idx == 1) {
        size_t stride1 = stride[1] < width ? width : stride[1];
        return height * stride1 / 2;
      } else {
        LOG(FATAL) << "plane index wrong.";
      }
    case PixelFmt::ABGR:
    case PixelFmt::ARGB:
    case PixelFmt::BGRA:
    case PixelFmt::RGBA:
      return height * stride0 * 4;
    case PixelFmt::I420:
      if (plane_idx == 0) {
        return height * stride0;
      } else if (plane_idx == 1 || plane_idx == 2) {
        size_t stride1 = stride[1] < width / 2 ? width / 2 : stride[1];
        return height * stride1 / 2;
      } else {
        LOG(FATAL) << "plane index wrong.";
      }
    default:
      return 0;
  }
  return 0;
}

size_t VideoFrame::GetTotalSize() const noexcept {
  size_t bytes = 0;
  for (size_t i = 0; i < GetPlaneNum(format); ++i) {
    bytes += GetPlaneSize(i);
  }
  return bytes;
}

namespace detail {
void ClipBoundingBox(BoundingBox* box) noexcept {
  // roi out of frame
  if (box->x >= 1 || box->y >= 1 ||
      box->w <= 0 || box->h <= 0 ||
      box->w > 1 || box->h > 1) {
    box->x = 0, box->y = 0, box->w = 0, box->h = 0;
    return;
  }
  // make roi totally inside of frame
  box->w = std::max(std::min(box->x + box->w, box->w), 0.f);
  box->h = std::max(std::min(box->y + box->h, box->h), 0.f);
  box->x = std::max(0.f, box->x);
  box->y = std::max(0.f, box->y);
  box->w = (box->x + box->w) > 1 ? (1 - box->x) : box->w;
  box->h = (box->y + box->h) > 1 ? (1 - box->y) : box->h;
}
}  // namespace detail

}  // namespace video
}  // namespace infer_server
