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

#ifndef INFER_SERVER_MODEL_H_
#define INFER_SERVER_MODEL_H_

#include <cnrt.h>
#include <glog/logging.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "cnis/shape.h"

// for magicmind
#ifdef HAVE_MM_COMMON_HEADER
#include "mm_common.h"
#include "mm_runtime.h"
#else
#include "common.h"
#include "interface_runtime.h"
#endif

#include "mm_helper.h"

namespace infer_server {

class Model;
class ModelRunner {
 public:
  explicit ModelRunner(int device_id) : device_id_(device_id) {}
  ModelRunner(const ModelRunner& other) = delete;
  ModelRunner& operator=(const ModelRunner& other) = delete;
  ModelRunner(ModelRunner&& other) = default;
  ModelRunner& operator=(ModelRunner&& other) = default;
  ~ModelRunner();

  bool Init(MModel* model, mm_unique_ptr<MContext> ctx, const std::vector<Shape>& in_shape = {}) noexcept;
  std::vector<Shape> InferOutputShape(const std::vector<Shape>& input) noexcept;
  bool CanInferOutputShape() noexcept { return !outputs_.empty(); }
  Status Run(ModelIO* input, ModelIO* output) noexcept;  // NOLINT

 private:
  bool FixedShape(const std::vector<Shape>& shapes) noexcept {
    for (auto &shape : shapes) {
      auto vectorized_shape = shape.Vectorize();
      if (!std::all_of(vectorized_shape.begin(), vectorized_shape.end(), [](int64_t v) { return v > 0; })) {
        return false;
      }
    }
    return !shapes.empty();
  }
  mm_unique_ptr<MContext> ctx_{nullptr};
  std::vector<MTensor*> inputs_;
  std::vector<MTensor*> outputs_;
  std::vector<Shape> i_shapes_;
  std::vector<Shape> o_shapes_;
  std::vector<DataLayout> i_layouts_;
  std::vector<DataLayout> o_layouts_;
  bool fixed_input_shape_{true};
  bool fixed_output_shape_{true};
  cnrtQueue_t task_queue_{nullptr};
#ifdef PERF_HARDWARE_TIME
  cnrtNotifier_t notifier_start_{nullptr}, notifier_end_{nullptr};
#endif
  uint32_t input_num_{0};
  uint32_t output_num_{0};
  int device_id_{0};
};  // class RuntimeContext

class Model : public ModelInfo {
 public:
  Model() = default;
  bool Init(void* mem_ptr, size_t size, const std::vector<Shape>& i_shape = {}) noexcept;
  bool Init(const std::string& model_path, const std::vector<Shape>& i_shape = {}) noexcept;
  ~Model();

  bool HasInit() const noexcept { return has_init_; }

  const Shape& InputShape(int index) const noexcept override {
    CHECK(index < i_num_ || index >= 0) << "[EasyDK InferServer] [Model] Input shape index overflow";
    return input_shapes_[index];
  }
  const Shape& OutputShape(int index) const noexcept override {
    CHECK(index < o_num_ || index >= 0) << "[EasyDK InferServer] [Model] Output shape index overflow";
    return output_shapes_[index];
  }
  const DataLayout& InputLayout(int index) const noexcept override {
    CHECK(index < i_num_ || index >= 0) << "[EasyDK InferServer] [Model] Input shape index overflow";
    return i_mlu_layouts_[index];
  }
  const DataLayout& OutputLayout(int index) const noexcept override {
    CHECK(index < o_num_ || index >= 0) << "[EasyDK InferServer] [Model] Input shape index overflow";
    return o_mlu_layouts_[index];
  }
  uint32_t InputNum() const noexcept override { return i_num_; }
  uint32_t OutputNum() const noexcept override { return o_num_; }
  uint32_t BatchSize() const noexcept override { return model_batch_size_; }

  bool FixedOutputShape() noexcept override { return FixedShape(output_shapes_); }

  MEngine* GetEngine(int device_id) noexcept {
    MEngine* engine{nullptr};
    std::unique_lock<std::mutex> lk(engine_map_mutex_);
    auto iter = engine_map_.find(device_id);
    if (iter == engine_map_.end()) {
       if (!SetCurrentDevice(device_id)) return nullptr;
      MModel::EngineConfig config;
#if MM_MAJOR_VERSION <= 0 && MM_MINOR_VERSION < 13
      config.device_type = "MLU";
#else
      config.SetDeviceType("MLU");
#endif
      engine = model_->CreateIEngine(config);
      if (!engine) return nullptr;
      engine_map_[device_id].reset(engine);
    } else {
      engine = iter->second.get();
    }
    return engine;
  }
  std::shared_ptr<ModelRunner> GetRunner(int device_id) noexcept {
    MEngine* engine = GetEngine(device_id);
    auto runner = std::make_shared<ModelRunner>(device_id);
    MContext* ctx = engine->CreateIContext();
    if (!ctx || !runner->Init(model_.get(), mm_unique_ptr<MContext>(ctx), input_shapes_)) return nullptr;
    return runner;
  }
  MModel* GetModel() noexcept { return model_.get(); }
  std::string GetKey() const noexcept override { return model_file_; }

 private:
  bool GetModelInfo(const std::vector<Shape>& in_shape) noexcept;
  bool FixedShape(const std::vector<Shape>& shapes) noexcept {
    for (auto &shape : shapes) {
      auto vectorized_shape = shape.Vectorize();
      if (!std::all_of(vectorized_shape.begin(), vectorized_shape.end(), [](int64_t v) { return v > 0; })) {
        return false;
      }
    }
    return !shapes.empty();
  }
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

 private:
  mm_unique_ptr<MModel> model_{nullptr};
  std::map<int, mm_unique_ptr<MEngine>> engine_map_;
  std::mutex engine_map_mutex_;
  std::string model_file_;

  std::vector<DataLayout> i_mlu_layouts_, o_mlu_layouts_;
  std::vector<Shape> input_shapes_, output_shapes_;
  int i_num_{0}, o_num_{0};
  uint32_t model_batch_size_{1};
  bool has_init_{false};
};  // class Model

// use environment CNIS_MODEL_CACHE_LIMIT to control cache limit
class ModelManager {
 public:
  static ModelManager* Instance() noexcept;

  void SetModelDir(const std::string& model_dir) noexcept { model_dir_ = model_dir; }

  ModelPtr Load(const std::string& model_file, const std::vector<Shape>& in_shape = {}) noexcept;
  ModelPtr Load(void* mem_cache, size_t size, const std::vector<Shape>& in_shape = {}) noexcept;
  bool Unload(ModelPtr model) noexcept;

  void ClearCache() noexcept;

  int CacheSize() noexcept;

  std::shared_ptr<Model> GetModel(const std::string& name) noexcept;

 private:
  std::string DownloadModel(const std::string& url) noexcept;
  void CheckAndCleanCache() noexcept;

  std::string model_dir_{"."};

  static std::unordered_map<std::string, std::shared_ptr<Model>> model_cache_;
  static std::mutex model_cache_mutex_;
};  // class ModelManager

}  // namespace infer_server

#endif  // INFER_SERVER_MODEL_H_
