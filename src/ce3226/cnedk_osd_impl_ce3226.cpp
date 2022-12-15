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

#include "cnedk_osd_impl_ce3226.hpp"

#include <cstring>  // for memset
#include <tuple>
#include <utility>
#include <vector>

#include "glog/logging.h"

#include "ce3226_helper.hpp"
#include "mps_service/mps_service.hpp"

namespace cnedk {

int DrawRectCe3226(CnedkBufSurface *surf, CnedkOsdRectParams *params, uint32_t num) {
  cnVideoFrameInfo_t info;
  if (BufSurfaceToVideoFrameInfo(surf, &info) < 0) {
    LOG(ERROR) << "[EasyDK] DrawRectCe3226(): Convert BufSurface to VideoFrameInfo failed";
    return -1;
  }

  std::vector<std::tuple<Bbox, cnU32_t, cnU32_t>> bboxes;
  for (uint32_t i = 0; i < num; i++) {
    CnedkOsdRectParams &param = params[i];
    Bbox bbox(param.x, param.y, param.w, param.h);
    bboxes.push_back({bbox, param.line_width, param.color});
  }

  return MpsService::Instance().OsdDrawBboxes(&info, bboxes);
}

int FillRectCe3226(CnedkBufSurface *surf, CnedkOsdRectParams *params, uint32_t num) {
  cnVideoFrameInfo_t info;
  if (BufSurfaceToVideoFrameInfo(surf, &info) < 0) {
    LOG(ERROR) << "[EasyDK] FillRectCe3226(): Convert BufSurface to VideoFrameInfo failed";
    return -1;
  }

  std::vector<std::pair<Bbox, cnU32_t>> bboxes;
  for (uint32_t i = 0; i < num; i++) {
    CnedkOsdRectParams &param = params[i];
    Bbox bbox(param.x, param.y, param.w, param.h);
    bboxes.push_back({bbox, param.color});
  }

  return MpsService::Instance().OsdFillBboxes(&info, bboxes);
}

int DrawBitmapCe3226(CnedkBufSurface *surf, CnedkOsdBitmapParams *params, uint32_t num) {
  cnVideoFrameInfo_t info;
  if (BufSurfaceToVideoFrameInfo(surf, &info) < 0) {
    LOG(ERROR) << "[EasyDK] DrawBitmapCe3226(): Convert BufSurface to VideoFrameInfo failed";
    return -1;
  }

  std::vector<std::tuple<Bbox, void *, cnU32_t, cnU32_t>> texts;
  for (uint32_t i = 0; i < num; i++) {
    CnedkOsdBitmapParams &param = params[i];
    Bbox bbox(param.x, param.y, param.w, param.h);
    texts.push_back({bbox, param.bitmap_argb1555, param.bg_color, param.pitch});
  }

  return MpsService::Instance().OsdPutText(&info, texts);
}

}  // namespace cnedk
