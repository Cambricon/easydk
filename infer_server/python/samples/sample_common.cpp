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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "cnis/contrib/video_helper.h"
#include "cxxutil/log.h"

namespace py = pybind11;

namespace infer_server {

struct DetectObject {
  int label;
  float score;
  infer_server::video::BoundingBox bbox;
};  // struct DetectObject

struct PostprocSSD {
  float threshold;

  explicit PostprocSSD(float _threshold) : threshold(_threshold) {}

  inline float Clip(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

  bool Execute(InferData* result, const ModelIO& model_output, const ModelInfo* model) {
    LOGD(PostprocSSD) << "PostprcessSSD::Execute()";
    std::vector<DetectObject> objs;
    const float* data = reinterpret_cast<const float*>(model_output.buffers[0].Data());
    int box_num = data[0];
    data += 64;

    LOGD(PostprocSSD) << "--------------------------box num: " << box_num;
    for (int bi = 0; bi < box_num; ++bi) {
      DetectObject obj;
      if (data[1] == 0) continue;
      obj.label = data[1] - 1;
      obj.score = data[2];
      if (threshold > 0 && obj.score < threshold) continue;
      obj.bbox.x = Clip(data[3]);
      obj.bbox.y = Clip(data[4]);
      obj.bbox.w = Clip(data[5]) - obj.bbox.x;
      obj.bbox.h = Clip(data[6]) - obj.bbox.y;
      LOGD(PostprocSSD) << "obj.label " << obj.label
                        << " obj.score " << obj.score
                        << " obj.bbox.x " << obj.bbox.x
                        << " obj.bbox.y " << obj.bbox.y
                        << " obj.bbox.w " << obj.bbox.w
                        << " obj.bbox.h " << obj.bbox.h;
      objs.emplace_back(std::move(obj));
      data += 7;
    }

    py::gil_scoped_acquire gil;
    std::shared_ptr<py::dict> dict = std::shared_ptr<py::dict>(new py::dict(), [] (py::dict* t) {
      // py::dict destruct in c++ thread without gil resource, it is important to get gil
      py::gil_scoped_acquire gil;
      delete t;
    });
    (*dict)["objs"] = objs;
    result->Set(dict);
    LOGD(PostprocSSD) << "PostprcessSSD::Execute() done";
    return true;
  }
};  // struct PostprocSSD

struct PostprocYolov3 {
  float threshold;

  explicit PostprocYolov3(float _threshold) : threshold(_threshold) {}

  inline float Clip(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

  bool Execute(InferData* result, const ModelIO& model_output, const ModelInfo* model) {
    LOGD(PostprocYolov3) << "PostprcessYolov3:Execute()";
    std::vector<DetectObject> objs;
    py::gil_scoped_acquire gil;
    std::shared_ptr<py::dict> user_data = result->GetUserData<std::shared_ptr<py::dict>>();
    int image_w = py::cast<int>((*user_data)["image_width"]);
    int image_h = py::cast<int>((*user_data)["image_height"]);
    py::gil_scoped_release release;
    int model_input_w = model->InputShape(0)[2];
    int model_input_h = model->InputShape(0)[1];
    if (model->InputLayout(0).order == DimOrder::NCHW) {
      model_input_w = model->InputShape(0)[3];
      model_input_h = model->InputShape(0)[2];
    }

    float scaling_factors = std::min(1.0 * model_input_w / image_w, 1.0 * model_input_h / image_h);

    // scaled size
    const int scaled_w = scaling_factors * image_w;
    const int scaled_h = scaling_factors * image_h;


    const float* data = reinterpret_cast<const float*>(model_output.buffers[0].Data());
    int box_num = data[0];
    LOGD(PostprocYolov3) << "--------------------------box num: " << box_num;
    constexpr int box_step = 7;
    for (int bi = 0; bi < box_num; ++bi) {
      float left = Clip(data[64 + bi * box_step + 3]);
      float right = Clip(data[64 + bi * box_step + 5]);
      float top = Clip(data[64 + bi * box_step + 4]);
      float bottom = Clip(data[64 + bi * box_step + 6]);

      // rectify
      left = (left * model_input_w - (model_input_w - scaled_w) / 2) / scaled_w;
      right = (right * model_input_w - (model_input_w - scaled_w) / 2) / scaled_w;
      top = (top * model_input_h - (model_input_h - scaled_h) / 2) / scaled_h;
      bottom = (bottom * model_input_h - (model_input_h - scaled_h) / 2) / scaled_h;
      left = std::max(0.0f, left);
      right = std::max(0.0f, right);
      top = std::max(0.0f, top);
      bottom = std::max(0.0f, bottom);

      DetectObject obj;
      obj.label = static_cast<int>(data[64 + bi * box_step + 1]);
      obj.score = data[64 + bi * box_step + 2];
      obj.bbox.x = left;
      obj.bbox.y = top;
      obj.bbox.w = std::min(1.0f - obj.bbox.x, right - left);
      obj.bbox.h = std::min(1.0f - obj.bbox.y, bottom - top);
      LOGD(PostprocYolov3) << "obj.label " << obj.label
                           << " obj.score " << obj.score
                           << " obj.bbox.x " << obj.bbox.x
                           << " obj.bbox.y " << obj.bbox.y
                           << " obj.bbox.w " << obj.bbox.w
                           << " obj.bbox.h " << obj.bbox.h;
      if ((threshold > 0 && obj.score < threshold) || obj.bbox.w <= 0 || obj.bbox.h <= 0) continue;
      objs.emplace_back(std::move(obj));
    }
    py::gil_scoped_acquire gil_dict;
    std::shared_ptr<py::dict> dict = std::shared_ptr<py::dict>(new py::dict(), [] (py::dict* t) {
      // py::dict destruct in c++ thread without gil resource, it is important to get gil
      py::gil_scoped_acquire gil;
      delete t;
    });
    (*dict)["objs"] = objs;
    result->Set(dict);
    // LOGD(PostprocYolov3) << "PostprcessYolov3::Execute() done";
    return true;
  }
};  // struct PostprocYolov3

void SampleWrapper(const py::module &m) {
  py::class_<DetectObject>(m, "DetectObject")
      .def(py::init<>())
      .def_readwrite("label", &DetectObject::label)
      .def_readwrite("score", &DetectObject::score)
      .def_readwrite("bbox", &DetectObject::bbox);

  py::class_<PostprocSSD>(m, "PostprocSSD")
      .def(py::init<float>())
      .def("execute", [] (PostprocSSD postproc, InferData* result, std::reference_wrapper<ModelIO> model_output,
                          const ModelInfo* model) {
          return postproc.Execute(result, model_output.get(), model);
      }, py::call_guard<py::gil_scoped_release>());

  py::class_<PostprocYolov3>(m, "PostprocYolov3")
      .def(py::init<float>())
      .def("execute", [] (PostprocYolov3 postproc, InferData* result, std::reference_wrapper<ModelIO> model_output,
                          const ModelInfo* model) {
          return postproc.Execute(result, model_output.get(), model);
      }, py::call_guard<py::gil_scoped_release>());
}

}  //  namespace infer_server
