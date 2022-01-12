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

#include <algorithm>
#include <utility>
#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "postproc.h"

bool PostprocYolov5::operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                                const infer_server::ModelInfo* model) {
  std::vector<DetectObject> objs;
  int image_w, image_h;
  if (set_frame_size_) {
    image_w = size_.width;
    image_h = size_.height;
  } else {
    FrameSize size = result->GetUserData<FrameSize>();
    image_w = size.width;
    image_h = size.height;
  }
  int model_input_w = model->InputShape(0)[2];
  int model_input_h = model->InputShape(0)[1];
  if (model->InputLayout(0).order == infer_server::DimOrder::NCHW) {
    model_input_w = model->InputShape(0)[3];
    model_input_h = model->InputShape(0)[2];
  }

  float scaling_factors = std::min(1.0 * model_input_w / image_w, 1.0 * model_input_h / image_h);

  // scaled size
  const int scaled_w = scaling_factors * image_w;
  const int scaled_h = scaling_factors * image_h;


  const float* data = reinterpret_cast<const float*>(model_output.buffers[0].Data());
  int box_num = data[0];
  constexpr int box_step = 7;
  for (int bi = 0; bi < box_num; ++bi) {
    float left = data[64 + bi * box_step + 3];
    float right = data[64 + bi * box_step + 5];
    float top = data[64 + bi * box_step + 4];
    float bottom = data[64 + bi * box_step + 6];

    // rectify
    left = (left - (model_input_w - scaled_w) / 2) / scaled_w;
    right = (right - (model_input_w - scaled_w) / 2) / scaled_w;
    top = (top - (model_input_h - scaled_h) / 2) / scaled_h;
    bottom = (bottom - (model_input_h - scaled_h) / 2) / scaled_h;
    left = Clip(left);
    right = Clip(right);
    top = Clip(top);
    bottom = Clip(bottom);

    DetectObject obj;
    obj.label = static_cast<int>(data[64 + bi * box_step + 1]);
    obj.score = data[64 + bi * box_step + 2];
    obj.bbox.x = left;
    obj.bbox.y = top;
    obj.bbox.w = std::min(1.0f - obj.bbox.x, right - left);
    obj.bbox.h = std::min(1.0f - obj.bbox.y, bottom - top);

    if ((threshold > 0 && obj.score < threshold) || obj.bbox.w <= 0 || obj.bbox.h <= 0) continue;
    objs.emplace_back(std::move(obj));
  }

  result->Set(std::move(objs));
  return true;
}
