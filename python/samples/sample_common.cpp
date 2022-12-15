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

#include <glog/logging.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "cnis/processor.h"

namespace py = pybind11;

namespace infer_server {

struct DetectObject {
  int label;
  float score;
  CNInferBoundingBox bbox;
};  // struct DetectObject

// ------------------------- Preproc ----------------------
CnedkTransformRect KeepAspectRatio(int src_w, int src_h, int dst_w, int dst_h) {
  float src_ratio = static_cast<float>(src_w) / src_h;
  float dst_ratio = static_cast<float>(dst_w) / dst_h;
  CnedkTransformRect res;
  memset(&res, 0, sizeof(res));
  if (src_ratio < dst_ratio) {
    int pad_lenth = dst_w - src_ratio * dst_h;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_w - pad_lenth / 2 < 0) return res;
    res.width = dst_w - pad_lenth;
    res.left = pad_lenth / 2;
    res.top = 0;
    res.height = dst_h;
  } else if (src_ratio > dst_ratio) {
    int pad_lenth = dst_h - dst_w / src_ratio;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_h - pad_lenth / 2 < 0) return res;
    res.height = dst_h - pad_lenth;
    res.top = pad_lenth / 2;
    res.left = 0;
    res.width = dst_w;
  } else {
    res.left = 0;
    res.top = 0;
    res.width = dst_w;
    res.height = dst_h;
  }
  return res;
}

class PreprocYolov5 : public IPreproc {
 public:
  int OnTensorParams(const CnPreprocTensorParams *params) override {
    params_ = *params;
    return 0;
  }

  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect> &src_rects) override {
    CnedkBufSurface* src_buf = src->GetBufSurface();
    CnedkBufSurface* dst_buf = dst->GetBufSurface();

    uint32_t batch_size = src->GetNumFilled();
    std::vector<CnedkTransformRect> src_rect(batch_size);
    std::vector<CnedkTransformRect> dst_rect(batch_size);
    CnedkTransformParams params;
    memset(&params, 0, sizeof(params));
    params.transform_flag = 0;
    if (src_rects.size()) {
      params.transform_flag |= CNEDK_TRANSFORM_CROP_SRC;
      params.src_rect = src_rect.data();
      memset(src_rect.data(), 0, sizeof(CnedkTransformRect) * batch_size);
      for (uint32_t i = 0; i < batch_size; i++) {
        CnedkTransformRect *src_bbox = &src_rect[i];
        *src_bbox = src_rects[i];
        // validate bbox
        src_bbox->left -= src_bbox->left & 1;
        src_bbox->top -= src_bbox->top & 1;
        src_bbox->width -= src_bbox->width & 1;
        src_bbox->height -= src_bbox->height & 1;
        while (src_bbox->left + src_bbox->width > src_buf->surface_list[i].width) src_bbox->width -= 2;
        while (src_bbox->top + src_bbox->height > src_buf->surface_list[i].height) src_bbox->height -= 2;
      }
    }

    // configure dst_desc
    CnedkTransformTensorDesc dst_desc;
    dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_RGB;
    dst_desc.data_type = CNEDK_TRANSFORM_UINT8;

    if (params_.input_order == infer_server::DimOrder::NHWC) {
      dst_desc.shape.n = params_.input_shape[0];
      dst_desc.shape.h = params_.input_shape[1];
      dst_desc.shape.w = params_.input_shape[2];
      dst_desc.shape.c = params_.input_shape[3];
    } else if (params_.input_order == infer_server::DimOrder::NCHW) {
      dst_desc.shape.n = params_.input_shape[0];
      dst_desc.shape.c = params_.input_shape[1];
      dst_desc.shape.h = params_.input_shape[2];
      dst_desc.shape.w = params_.input_shape[3];
    } else {
      LOG(ERROR) << "[EasyDK InferServer] [PythonAPISamples] PreprocYolov5: Unsupported input dim order";
      return -1;
    }

    params.transform_flag |= CNEDK_TRANSFORM_CROP_DST;
    params.dst_rect = dst_rect.data();
    memset(dst_rect.data(), 0, sizeof(CnedkTransformRect) * batch_size);
    for (uint32_t i = 0; i < batch_size; i++) {
      CnedkTransformRect *dst_bbox = &dst_rect[i];
      *dst_bbox = KeepAspectRatio(src_buf->surface_list[i].width, src_buf->surface_list[i].height, dst_desc.shape.w,
                                  dst_desc.shape.h);
      // validate bbox
      dst_bbox->left -= dst_bbox->left & 1;
      dst_bbox->top -= dst_bbox->top & 1;
      dst_bbox->width -= dst_bbox->width & 1;
      dst_bbox->height -= dst_bbox->height & 1;
      while (dst_bbox->left + dst_bbox->width > dst_desc.shape.w) dst_bbox->width -= 2;
      while (dst_bbox->top + dst_bbox->height > dst_desc.shape.h) dst_bbox->height -= 2;
    }

    params.dst_desc = &dst_desc;

    CnedkTransformConfigParams config;
    memset(&config, 0, sizeof(config));
    config.compute_mode = CNEDK_TRANSFORM_COMPUTE_MLU;
    CnedkTransformSetSessionParams(&config);

    CnedkBufSurfaceMemSet(dst_buf, -1, -1, 114);
    if (CnedkTransform(src_buf, dst_buf, &params) < 0) {
      LOG(ERROR) << "[EasyDK InferServer] [PythonAPISamples] PreprocYolov5: CnedkTransform failed";
      return -1;
    }

    return 0;
  }

 private:
  CnPreprocTensorParams params_;
};  // class PreprocYolov5


// ------------------------- Postproc ----------------------
inline float Clip(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

class PostprocYolov5 : public IPostproc {
 public:
  explicit PostprocYolov5(float threshold) : threshold_(threshold) {}
  int OnPostproc(const std::vector<infer_server::InferData*>& data_vec,
                 const infer_server::ModelIO& model_output,
                 const infer_server::ModelInfo* model_info) override {
    cnedk::BufSurfWrapperPtr output0 = model_output.surfs[0];  // data
    cnedk::BufSurfWrapperPtr output1 = model_output.surfs[1];  // bbox

    if (!output0->GetHostData(0)) {
      LOG(ERROR) << "[EasyDK InferServer] [PythonAPISamples] PostprocYolov5: copy data0 to host failed.";
      return -1;
    }
    if (!output1->GetHostData(0)) {
      LOG(ERROR) << "[EasyDK InferServer] [PythonAPISamples] PostprocYolov5: copy data1 to host failed.";
      return -1;
    }

    CnedkBufSurfaceSyncForCpu(output0->GetBufSurface(), -1, -1);
    CnedkBufSurfaceSyncForCpu(output1->GetBufSurface(), -1, -1);

    infer_server::DimOrder input_order = model_info->InputLayout(0).order;
    auto s = model_info->InputShape(0);
    int model_input_w, model_input_h;
    if (input_order == infer_server::DimOrder::NCHW) {
      model_input_w = s[3];
      model_input_h = s[2];
    } else if (input_order == infer_server::DimOrder::NHWC) {
      model_input_w = s[2];
      model_input_h = s[1];
    } else {
      LOG(ERROR) << "[EasyDK InferServer] [PythonAPISamples] PostprocYolov5: not supported dim order";
      return -1;
    }

    auto range_0_w = [model_input_w](float num) {
      return std::max(.0f, std::min(static_cast<float>(model_input_w), num));
    };
    auto range_0_h = [model_input_h](float num) {
      return std::max(.0f, std::min(static_cast<float>(model_input_h), num));
    };


    for (size_t batch_idx = 0; batch_idx < data_vec.size(); batch_idx++) {
      std::vector<DetectObject> objs;
      py::gil_scoped_acquire gil;
      std::shared_ptr<py::dict> user_data = data_vec[batch_idx]->GetUserData<std::shared_ptr<py::dict>>();
      int image_w = py::cast<int>((*user_data)["image_width"]);
      int image_h = py::cast<int>((*user_data)["image_height"]);
      py::gil_scoped_release release;

      float *data = static_cast<float*>(output0->GetHostData(0, batch_idx));
      int box_num = static_cast<int*>(output1->GetHostData(0, batch_idx))[0];
      if (!box_num) {
        continue;  // no bboxes
      }

      const float scaling_w = 1.0f * model_input_w / image_w;
      const float scaling_h = 1.0f * model_input_h / image_h;
      const float scaling = std::min(scaling_w, scaling_h);
      float scaling_factor_w, scaling_factor_h;
      scaling_factor_w = scaling_w / scaling;
      scaling_factor_h = scaling_h / scaling;

      for (int bi = 0; bi < box_num; ++bi) {
        if (threshold_ > 0 && data[2] < threshold_) {
          data += 7;
          continue;
        }

        float l = range_0_w(data[3]);
        float t = range_0_h(data[4]);
        float r = range_0_w(data[5]);
        float b = range_0_h(data[6]);
        l = Clip((l / model_input_w - 0.5f) * scaling_factor_w + 0.5f);
        t = Clip((t / model_input_h - 0.5f) * scaling_factor_h + 0.5f);
        r = Clip((r / model_input_w - 0.5f) * scaling_factor_w + 0.5f);
        b = Clip((b / model_input_h - 0.5f) * scaling_factor_h + 0.5f);
        if (r <= l || b <= t) {
          data += 7;
          continue;
        }

        DetectObject obj;
        obj.label = static_cast<int>(data[1]);
        obj.score = data[2];
        obj.bbox.x = l;
        obj.bbox.y = t;
        obj.bbox.w = std::min(1.0f - l, r - l);
        obj.bbox.h = std::min(1.0f - t, b - t);
        VLOG(5) << "[EasyDK InferServer] [PythonAPISamples] PostprocYolov5: obj.label " << obj.label << " obj.score "
                << obj.score << " obj.bbox.x " << obj.bbox.x << " obj.bbox.y " << obj.bbox.y << " obj.bbox.w "
                << obj.bbox.w << " obj.bbox.h " << obj.bbox.h;
        objs.emplace_back(std::move(obj));
        data += 7;
      }
      py::gil_scoped_acquire gil_dict;
      std::shared_ptr<py::dict> dict = std::shared_ptr<py::dict>(new py::dict(), [] (py::dict* t) {
        // py::dict destruct in c++ thread without gil resource, it is important to get gil
        py::gil_scoped_acquire gil;
        delete t;
      });
      (*dict)["objs"] = objs;
      data_vec[batch_idx]->Set(dict);
    }
    return 0;
  }

 private:
  float threshold_;
};  // class PostprocYolov5

void SampleWrapper(const py::module &m) {
  py::class_<DetectObject>(m, "DetectObject")
      .def(py::init<>())
      .def_readwrite("label", &DetectObject::label)
      .def_readwrite("score", &DetectObject::score)
      .def_readwrite("bbox", &DetectObject::bbox);

  py::class_<PostprocYolov5, std::shared_ptr<PostprocYolov5>, IPostproc>(m, "PostprocYolov5")
      .def(py::init<float>());
  py::class_<PreprocYolov5, std::shared_ptr<PreprocYolov5>, IPreproc>(m, "PreprocYolov5")
      .def(py::init());
}

}  //  namespace infer_server
