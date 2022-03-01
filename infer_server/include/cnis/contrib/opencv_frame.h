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

#include <map>
#include <utility>
#include <vector>

#include "video_helper.h"

// a header only implementation

namespace infer_server {
namespace video {

struct OpencvFrame {
  cv::Mat img;
  PixelFmt fmt;
  BoundingBox roi;
};

namespace detail {
void ClipBoundingBox(BoundingBox* box) noexcept;
}  // namespace detail

struct OpencvPreproc {
 public:
  OpencvPreproc(PixelFmt dst_fmt, std::vector<float> mean, std::vector<float> std, bool normalize,
                bool keep_aspect_ratio, int pad_value, bool transpose, DataType src_depth)
      : dst_fmt_(dst_fmt),
        mean_(std::move(mean)),
        std_(std::move(std)),
        normalize_(normalize),
        keep_aspect_ratio_(keep_aspect_ratio),
        transpose_(transpose),
        pad_value_(pad_value),
        depth_(src_depth) {
    if (normalize_) {
      if (mean_.empty()) mean_.resize(GetChannelNum(), 0.f);
      if (std_.empty()) std_.resize(GetChannelNum(), 1.f);
      for (auto& m : mean_) {
        m *= 255;
      }
      for (auto& s : std_) {
        s *= 255;
      }
    }
    if (mean_.empty() && !std_.empty()) mean_ = {0.f, 0.f, 0.f, 0.f};
    if (!mean_.empty() && std_.empty()) std_ = {1.f, 1.f, 1.f, 1.f};
    mean_std_ = (!mean_.empty()) && (!std_.empty());
  }

  bool operator()(ModelIO* model_input, const InferData& in, const ModelInfo* m) {
    const auto& frame = in.GetLref<OpencvFrame>();
    cv::Mat tmp_cvt;
    auto src_fmt = frame.fmt;
    if (src_fmt != dst_fmt_) {
      int code = GetCvtCode(src_fmt);
      if (code == -1) {
        return false;
      }
      cv::cvtColor(frame.img, tmp_cvt, code);
    } else {
      tmp_cvt = frame.img;
    }

    cv::Mat img;
    auto roi = frame.roi;
    if (roi.w == 0 || roi.h == 0) {
      img = tmp_cvt;
    } else {
      detail::ClipBoundingBox(&roi);
      img = tmp_cvt(
          cv::Rect(roi.x * tmp_cvt.cols, roi.y * tmp_cvt.rows, roi.w * tmp_cvt.cols, roi.h * tmp_cvt.rows));
    }

    cv::Mat tmp_transpose;
    if (transpose_) {
      cv::transpose(img, tmp_transpose);
    } else {
      tmp_transpose = img;
    }

    auto src_width = tmp_transpose.cols;
    auto src_height = tmp_transpose.rows;

    DimOrder input_order = m->InputLayout(0).order;
    auto s = m->InputShape(0);
    int dst_width, dst_height;
    if (input_order == DimOrder::NCHW) {
      dst_width = s[3];
      dst_height = s[2];
    } else if (input_order == DimOrder::NHWC) {
      dst_width = s[2];
      dst_height = s[1];
    } else {
      std::cerr << "not supported dim order";
      return false;
    }

    Buffer& b = model_input->buffers[0];

    cv::Mat tmp_resize;
    if (src_height != dst_height || src_width != dst_width) {
      if (keep_aspect_ratio_) {
        if (dst_fmt_ == PixelFmt::RGBA || dst_fmt_ == PixelFmt::BGRA) {
          tmp_resize = cv::Mat(dst_height, dst_width, CV_8UC4, cv::Scalar(pad_value_));
        } else if (dst_fmt_ == PixelFmt::RGB24 || dst_fmt_ == PixelFmt::BGR24) {
          tmp_resize = cv::Mat(dst_height, dst_width, CV_8UC3, cv::Scalar(pad_value_));
        } else {
          std::cerr << "unsupported format for model input." << std::endl;
        }
        auto rect = KeepAspectRatio(src_width, src_height, dst_width, dst_height);
        cv::Mat resize_keepaspectratio = tmp_resize(rect);
        cv::resize(tmp_transpose, resize_keepaspectratio, cv::Size(rect.width, rect.height));
      } else {
        cv::resize(tmp_transpose, tmp_resize, cv::Size(dst_width, dst_height));
      }
    } else {
      tmp_resize = tmp_transpose;
    }

    uint32_t channel_num = GetChannelNum();
    if (channel_num == 0) {
      std::cerr << "unsupported format for model input." << std::endl;
      return false;
    }
    DataType dst_dtype = m->InputLayout(0).dtype;
    int cv_code = CV_MAKETYPE(dst_dtype == DataType::UINT8 ? CV_8U : CV_32F, channel_num);
    cv::Mat dst(dst_height, dst_width, cv_code, b.MutableData());
    if (mean_std_) {
      if (dst_dtype == DataType::UINT8) {
        std::cerr << "Opencv preproc not support normalization, means or std with UINT8 data type" << std::endl;
        return false;
      }
      if (channel_num != mean_.size() || channel_num != std_.size()) {
        std::cerr << "expect the size of mean or std is " << channel_num << " but mean_ size is " << mean_.size()
                  << " and std_ size is " << std_.size() << "." << std::endl;
        return false;
      }
      MeanStd(tmp_resize, dst);
    } else if (dst_dtype != DataType::UINT8) {
      tmp_resize.convertTo(dst, cv_code, 1.0/255.0);
    } else {
      tmp_resize.copyTo(dst);
    }

    return true;
  }

 private:
  uint32_t GetChannelNum();
  cv::Rect KeepAspectRatio(int src_w, int src_h, int dst_w, int dst_h);
  bool MeanStd(cv::Mat img, cv::Mat dst);
  int GetCvtCode(PixelFmt src_fmt);

 private:
  PixelFmt dst_fmt_;
  std::vector<float> mean_;
  std::vector<float> std_;

  bool mean_std_ = false;
  bool normalize_ = false;
  bool keep_aspect_ratio_ = true;
  bool transpose_ = false;
  int pad_value_ = 0;
  DataType depth_ = DataType::UINT8;

 public:
  static PreprocessorHost::ProcessFunction GetFunction(PixelFmt dst_fmt, std::vector<float> mean = {},
                                                       std::vector<float> std = {}, bool normalize = false,
                                                       bool keep_aspect_ratio = true, int pad_value = 0,
                                                       bool transpose = false, DataType src_depth = DataType::UINT8) {
    return PreprocessorHost::ProcessFunction(
        OpencvPreproc(dst_fmt, std::move(mean), std::move(std), normalize,
                      keep_aspect_ratio, pad_value, transpose, src_depth));
  }
};

inline uint32_t OpencvPreproc::GetChannelNum() {
  switch (dst_fmt_) {
    case PixelFmt::RGB24:
    case PixelFmt::BGR24:
      return 3;
    case PixelFmt::RGBA:
    case PixelFmt::BGRA:
      return 4;
    default:
      std::cerr << "unsupported destination format in OpencvPreproc." << std::endl;
  }
  return 0;
}

inline cv::Rect OpencvPreproc::KeepAspectRatio(int src_w, int src_h, int dst_w, int dst_h) {
  float src_ratio = static_cast<float>(src_w) / src_h;
  float dst_ratio = static_cast<float>(dst_w) / dst_h;
  cv::Rect res;
  if (src_ratio < dst_ratio) {
    int pad_lenth = dst_w - src_ratio * dst_h;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_w - pad_lenth / 2 < 0) return {};
    res.width = dst_w - pad_lenth;
    res.x = pad_lenth / 2;
    res.y = 0;
    res.height = dst_h;
  } else if (src_ratio > dst_ratio) {
    int pad_lenth = dst_h - dst_w / src_ratio;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_h - pad_lenth / 2 < 0) return {};
    res.height = dst_h - pad_lenth;
    res.y = pad_lenth / 2;
    res.x = 0;
    res.width = dst_w;
  } else {
    res.x = 0;
    res.y = 0;
    res.width = dst_w;
    res.height = dst_h;
  }
  return res;
}

inline bool OpencvPreproc::MeanStd(cv::Mat img, cv::Mat dst) {
  float* input_data = reinterpret_cast<float*>(dst.data);
  size_t len = img.rows * img.cols;
  uint32_t channel_num = GetChannelNum();
  if (depth_ == DataType::UINT8) {
    const uint8_t* iimg = reinterpret_cast<const uint8_t*>(img.data);
    for (uint32_t idx = 0; idx < len; ++idx) {
      for (uint32_t ch = 0; ch < channel_num; ++ch) {
        input_data[idx * channel_num + ch] = (static_cast<float>(iimg[idx * channel_num + ch]) - mean_[ch]) / std_[ch];
      }
    }
  } else {
    const float* fimg = reinterpret_cast<const float*>(img.data);
    for (uint32_t idx = 0; idx < len; ++idx) {
      for (uint32_t ch = 0; ch < channel_num; ++ch) {
        input_data[idx * channel_num + ch] = (fimg[idx * channel_num + ch] - mean_[ch]) / std_[ch];
      }
    }
  }
  return true;
}

inline int OpencvPreproc::GetCvtCode(PixelFmt src_fmt) {
  // clang-format off
  // dst_fmt: {src_fmt : cvt_code}
  static std::map<PixelFmt, std::map<PixelFmt, int>> color_cvt_map{
    {PixelFmt::BGR24,
      {
        {PixelFmt::RGB24, cv::COLOR_RGB2BGR},
        {PixelFmt::RGBA, cv::COLOR_RGBA2BGR},
        {PixelFmt::BGRA, cv::COLOR_BGRA2BGR},
        {PixelFmt::NV12, cv::COLOR_YUV2BGR_NV12},
        {PixelFmt::NV21, cv::COLOR_YUV2BGR_NV21},
        {PixelFmt::I420, cv::COLOR_YUV2BGR_I420}
      },
    },
    {PixelFmt::RGB24,
      {
        {PixelFmt::BGR24, cv::COLOR_BGR2RGB},
        {PixelFmt::RGBA, cv::COLOR_RGBA2RGB},
        {PixelFmt::BGRA, cv::COLOR_BGRA2RGB},
        {PixelFmt::NV12, cv::COLOR_YUV2RGB_NV12},
        {PixelFmt::NV21, cv::COLOR_YUV2RGB_NV21},
        {PixelFmt::I420, cv::COLOR_YUV2RGB_I420}
      },
    },
    {PixelFmt::RGBA,
      {
        {PixelFmt::BGR24, cv::COLOR_BGR2RGBA},
        {PixelFmt::RGB24, cv::COLOR_RGB2RGBA},
        {PixelFmt::BGRA, cv::COLOR_BGRA2RGBA},
        {PixelFmt::NV12, cv::COLOR_YUV2RGBA_NV12},
        {PixelFmt::NV21, cv::COLOR_YUV2RGBA_NV21},
        {PixelFmt::I420, cv::COLOR_YUV2RGBA_I420}
      },
    },
    {PixelFmt::BGRA,
      {
        {PixelFmt::BGR24, cv::COLOR_BGR2BGRA},
        {PixelFmt::RGB24, cv::COLOR_RGB2BGRA},
        {PixelFmt::RGBA, cv::COLOR_RGBA2BGRA},
        {PixelFmt::NV12, cv::COLOR_YUV2BGRA_NV12},
        {PixelFmt::NV21, cv::COLOR_YUV2BGRA_NV21},
        {PixelFmt::I420, cv::COLOR_YUV2BGRA_I420}
      }
    }
  };
  // clang-format on
  try {
    return color_cvt_map.at(dst_fmt_).at(src_fmt);
  } catch (std::out_of_range& e) {
    std::cerr << "Unsupport code for cvtcolor." << std::endl;
    return -1;
  }
}

}  // namespace video
}  // namespace infer_server

#endif  // INFER_SERVER_OPENCV_FRAME_H_
