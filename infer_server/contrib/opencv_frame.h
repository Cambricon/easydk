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

#ifndef INFER_SERVER_OPENCV_FRAME_H_
#define INFER_SERVER_OPENCV_FRAME_H_

#include <opencv2/opencv.hpp>

#include "video_helper.h"

// a header only implementation

namespace infer_server {
namespace video {

struct OpencvFrame {
  cv::Mat img;
  PixelFmt fmt;
  BoundingBox roi;
};

template <PixelFmt dst_fmt>
struct OpencvResizeConvertBase {};

namespace detail {
void ClipBoundingBox(BoundingBox* box) noexcept;
}  // namespace detail

template <>
struct OpencvResizeConvertBase<PixelFmt::RGBA> {
  bool operator()(ModelIO* model_input, const InferData& in, const ModelInfo&) {
    const auto& frame = in.GetLref<OpencvFrame>();
    if (model_input->buffers.size() != 1) {
      std::cerr << "expect input number == 1, but get " << model_input->buffers.size() << std::endl;
      return false;
    }
    Shape& s = model_input->shapes[0];
    if (s.Size() != 4) {
      std::cerr << "expect input shape dimension == 4, but get " << s.Size() << std::endl;
      return false;
    }
    Buffer& b = model_input->buffers[0];
    int dst_width = s.GetW();
    int dst_height = s.GetH();

    cv::Mat img;
    auto roi = frame.roi;
    if (roi.w == 0 || roi.h == 0) {
      img = frame.img;
    } else {
      detail::ClipBoundingBox(&roi);
      img = frame.img(cv::Rect(roi.x * frame.img.cols, roi.y * frame.img.rows,
                               roi.w * frame.img.cols, roi.h * frame.img.rows));
    }

    cv::Mat tmp;
    switch (frame.fmt) {
      case PixelFmt::I420:
        cv::cvtColor(img, tmp, cv::COLOR_YUV2RGBA_I420);
        break;
      case PixelFmt::NV12:
        cv::cvtColor(img, tmp, cv::COLOR_YUV2RGBA_NV12);
        break;
      case PixelFmt::NV21:
        cv::cvtColor(img, tmp, cv::COLOR_YUV2RGBA_NV21);
        break;
      case PixelFmt::RGB24:
        cv::cvtColor(img, tmp, cv::COLOR_RGB2RGBA);
        break;
      case PixelFmt::BGR24:
        cv::cvtColor(img, tmp, cv::COLOR_BGR2RGBA);
        break;
      default:
        std::cerr << "unsupport pixel format";
        return false;
    }

    cv::Mat dst(dst_height, dst_width, CV_8UC4, b.MutableData());
    cv::resize(tmp, dst, cv::Size(dst_width, dst_height));
    return true;
  }

  static PreprocessorHost::ProcessFunction GetFunction() {
    return PreprocessorHost::ProcessFunction(OpencvResizeConvertBase<PixelFmt::RGBA>());
  }
};

using DefaultOpencvPreproc = OpencvResizeConvertBase<PixelFmt::RGBA>;

}  // namespace video
}  // namespace infer_server

#endif  // INFER_SERVER_OPENCV_FRAME_H_
