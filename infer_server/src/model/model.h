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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "buffer.h"
#include "infer_server.h"
#include "shape.h"

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

  bool Init(Model* model) noexcept;
  bool ForkFrom(const ModelRunner& other) noexcept;
  Status Run(std::vector<Buffer>& input, std::vector<Buffer>& output) noexcept;  // NOLINT

 private:
  uint32_t input_num_{0};
  uint32_t output_num_{0};
  cnrtRuntimeContext_t ctx_{nullptr};
  cnrtQueue_t task_queue_{nullptr};
  void** params_{nullptr};
#ifdef PERF_HARDWARE_TIME
  cnrtNotifier_t notifier_start_{nullptr}, notifier_end_{nullptr};
#endif
  int device_id_{0};
};  // class RuntimeContext

class Model : public ModelInfo {
 public:
  Model() = default;
  bool Init(const std::string& model_path, const std::string& func_name) noexcept;
  bool Init(void* mem_ptr, const std::string& func_name) noexcept;
  ~Model();

  bool HasInit() const noexcept { return has_init_; }

  const Shape& InputShape(int index) const noexcept override {
    CHECK(index < i_num_ || index >= 0) << "input shape index overflow";
    return input_shapes_[index];
  }
  const Shape& OutputShape(int index) const noexcept override {
    CHECK(index < o_num_ || index >= 0) << "output shape index overflow";
    return output_shapes_[index];
  }
  const DataLayout& InputLayout(int index) const noexcept override {
    CHECK(index < i_num_ || index >= 0) << "input shape index overflow";
    return i_mlu_layouts_[index];
  }
  const DataLayout& OutputLayout(int index) const noexcept override {
    CHECK(index < o_num_ || index >= 0) << "input shape index overflow";
    return o_mlu_layouts_[index];
  }
  uint32_t InputNum() const noexcept override { return i_num_; }
  uint32_t OutputNum() const noexcept override { return o_num_; }
  uint32_t BatchSize() const noexcept override { return model_batch_size_; }

  std::shared_ptr<ModelRunner> GetRunner(int device_id) noexcept {
    std::unique_lock<std::mutex> lk(runner_map_mutex_);
    if (!runner_map_.count(device_id)) {
      auto runner = std::make_shared<ModelRunner>(device_id);
      if (!runner->Init(this)) {
        return nullptr;
      } else {
        runner_map_[device_id] = runner;
        return runner;
      }
    } else {
      auto ret = std::make_shared<ModelRunner>(device_id);
      if (!ret->ForkFrom(*runner_map_[device_id])) {
        return nullptr;
      } else {
        return ret;
      }
    }
  }

  std::vector<Buffer> AllocMluInput(int device_id) const noexcept override {
    std::vector<Buffer> input;
    input.reserve(InputNum());
    for (uint32_t idx = 0; idx < InputNum(); ++idx) {
      input.emplace_back(InputShape(idx).BatchDataCount() * GetTypeSize(InputLayout(idx).dtype), device_id);
      (void)input[idx].MutableData();
    }
    return input;
  }

  std::vector<Buffer> AllocMluOutput(int device_id) const noexcept override {
    std::vector<Buffer> output;
    output.reserve(OutputNum());
    for (uint32_t idx = 0; idx < OutputNum(); ++idx) {
      output.emplace_back(OutputShape(idx).BatchDataCount() * GetTypeSize(OutputLayout(idx).dtype), device_id);
      (void)output[idx].MutableData();
    }
    return output;
  }

  cnrtFunction_t GetFunction() noexcept { return function_; }

  cnrtModel_t GetModel() noexcept { return model_; }

  const std::string& Path() const noexcept override { return path_; }
  const std::string& FunctionName() const noexcept override { return func_name_; }
  std::string GetKey() const noexcept override { return Path() + FunctionName(); }

 private:
  bool LoadFunction(const std::string& func_name) noexcept;
  bool GetModelInfo() noexcept;
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

 private:
  cnrtModel_t model_{nullptr};
  cnrtFunction_t function_{nullptr};
  std::map<int, std::shared_ptr<ModelRunner>> runner_map_;
  std::mutex runner_map_mutex_;

  std::vector<DataLayout> i_mlu_layouts_, o_mlu_layouts_;
  std::vector<Shape> input_shapes_, output_shapes_;
  std::vector<int64_t> i_data_sizes_, o_data_sizes_;
  std::string path_, func_name_;
  int i_num_{0}, o_num_{0};
  uint32_t model_batch_size_{1};
  bool has_init_{false};
};  // class InferModelInternal

// use environment CNIS_MODEL_CACHE_LIMIT to control cache limit
class ModelManager {
 public:
  static ModelManager* Instance() noexcept;

  void SetModelDir(const std::string& model_dir) noexcept { model_dir_ = model_dir; }

  ModelPtr Load(const std::string& model_path, const std::string& func_name) noexcept;
  ModelPtr Load(void* mem_cache, const std::string& func_name) noexcept;

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
