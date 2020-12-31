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
#include <glog/logging.h>
#include <algorithm>  // sort
#include <cstring>    // memset
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "cnpostproc.h"

using std::pair;
using std::vector;
using std::to_string;

namespace edk {

#define CLIP(x) ((x) < 0 ? 0 : ((x) > 1 ? 1 : (x)))

void CnPostproc::set_threshold(const float threshold) { threshold_ = threshold; }

vector<DetectObject> CnPostproc::Execute(const vector<pair<float*, uint64_t>>& net_outputs) {
  return Postproc(net_outputs);
}

vector<DetectObject> ClassificationPostproc::Postproc(const vector<pair<float*, uint64_t>>& net_outputs) {
  if (net_outputs.size() != 1) {
    LOG(WARNING) << "Classification neuron network only has one output but get "
                    + to_string(net_outputs.size());
  }

  float* data = net_outputs[0].first;
  uint64_t len = net_outputs[0].second;

  std::list<DetectObject> objs;
  for (decltype(len) i = 0; i < len; ++i) {
    if (data[i] < threshold_) continue;
    DetectObject obj;
    memset(&obj.bbox, 0, sizeof(BoundingBox));
    obj.label = i;
    obj.score = data[i];
    objs.emplace_back(std::move(obj));
  }

  objs.sort([](const DetectObject& a, const DetectObject& b) { return a.score > b.score; });

  return std::vector<DetectObject>(objs.begin(), objs.end());
}

vector<DetectObject> SsdPostproc::Postproc(const vector<pair<float*, uint64_t>>& net_outputs) {
  if (net_outputs.size() != 1) {
    LOG(WARNING) << "Ssd neuron network only has one output, but get "
                                        + to_string(net_outputs.size());
  }
  vector<DetectObject> objs;
  float* data = net_outputs[0].first;
  // auto len = net_outputs[0].second;
  float box_num = data[0];  // get box num by batch index
  data += 64;               // skip box num of all batch

  for (decltype(box_num) bi = 0; bi < box_num; ++bi) {
    DetectObject obj;
    if (data[1] == 0) continue;
    obj.label = data[1] - 1;
    obj.score = data[2];
    if (threshold_ > 0 && obj.score < threshold_) continue;
    obj.bbox.x = CLIP(data[3]);
    obj.bbox.y = CLIP(data[4]);
    obj.bbox.width = CLIP(data[5]) - obj.bbox.x;
    obj.bbox.height = CLIP(data[6]) - obj.bbox.y;
    objs.push_back(obj);
    data += 7;
  }

  return objs;
}

namespace detail {
template <typename dtype>
struct Clip {
  Clip(dtype _down, dtype _up) : down(_down), up(_up) {}
  inline dtype operator()(dtype val) {
    return std::min(up, std::max(down, val));
  }
  dtype down;
  dtype up;
};
}  // namespace detail

detail::Clip<float> Clip0_1_float(0, 1);

vector<DetectObject> Yolov3Postproc::Postproc(const vector<pair<float*, uint64_t>>& net_outputs) {
  vector<DetectObject> objs;
  float* data = net_outputs[0].first;
  uint64_t len = net_outputs[0].second;
  constexpr int box_step = 7;
  const int box_num = static_cast<int>(data[0]);
  CHECK_LE(static_cast<uint64_t>(64 + box_num * box_step), len);

  for (int bi = 0; bi < box_num; ++bi) {
    DetectObject obj;
    obj.label = static_cast<int>(data[64 + bi * box_step + 1]);
    obj.score = data[64 + bi * box_step + 2];
    if (obj.label == 0) continue;
    if (threshold_ > 0 && obj.score < threshold_) continue;
    obj.bbox.x = Clip0_1_float(data[64 + bi * box_step + 3]);
    obj.bbox.y = Clip0_1_float(data[64 + bi * box_step + 4]);
    obj.bbox.width = Clip0_1_float(data[64 + bi * box_step + 5]) - obj.bbox.x;
    obj.bbox.height = Clip0_1_float(data[64 + bi * box_step + 6]) - obj.bbox.y;

    obj.bbox.x = (obj.bbox.x - padl_ratio_) / (1 - padl_ratio_ - padr_ratio_);
    obj.bbox.y = (obj.bbox.y - padt_ratio_) / (1 - padb_ratio_ - padt_ratio_);
    obj.bbox.width /= (1 - padl_ratio_ - padr_ratio_);
    obj.bbox.height /= (1 - padb_ratio_ - padt_ratio_);

    obj.track_id = -1;
    if (obj.bbox.width <= 0) continue;
    if (obj.bbox.height <= 0) continue;
    objs.push_back(obj);
  }
  return objs;
}

}  // namespace edk
