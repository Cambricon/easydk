/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

#ifndef EDK_SAMPLES_CNOSD_H_
#define EDK_SAMPLES_CNOSD_H_

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "easytrack/easy_track.h"

using cv::Mat;
using cv::Point;
using cv::Scalar;
using std::string;
using std::vector;

class CnOsd {
 private:
  int box_thickness_ = 2;
  vector<string> labels_;
  vector<Scalar> colors_;
  int font_ = cv::FONT_HERSHEY_SIMPLEX;
  cv::Size bm_size_ = {1920, 1080};  // benchmark size,used to calculate scale.
  float bm_rate_ = 1.0f;             // benchmark rate, used to calculate scale.
  inline float CalScale(uint64_t area) const {
    float c = 0.3f;
    float a = (c - bm_rate_) / std::pow(bm_size_.width * bm_size_.height, 2);
    float b = 2 * (bm_rate_ - c) / (bm_size_.width * bm_size_.height);
    float scale = a * area * area + b * area + c;
    if (scale < 0) return 0;
    return scale;
  }

 public:
  CnOsd() = default;

  explicit CnOsd(const vector<std::string>& labels);

  explicit CnOsd(const std::string& label_fname);

  void LoadLabels(const std::string& fname);
  inline const std::vector<std::string> labels() const { return labels_; }

  void DrawId(Mat image, string text) const;
  void DrawFps(Mat image, float fps) const;
  void DrawLabel(Mat image, const vector<edk::DetectObject>& objects) const;

  void set_font(int font);

  inline void set_benchmark_size(cv::Size size) { bm_size_ = size; }
  inline cv::Size benchmark_size() const { return bm_size_; }
  inline void set_benchmark_rate(float rate) { bm_rate_ = rate; }
  inline float benchmark_rate() const { return bm_rate_; }

  inline void set_box_thickness(int box_thickness) { box_thickness_ = box_thickness; }
  inline int get_box_thickness() const { return box_thickness_; }
};

#endif  // EDK_SAMPLES_CNOSD_H_
