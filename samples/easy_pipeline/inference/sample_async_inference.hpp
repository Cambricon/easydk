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

#ifndef SAMPLE_ASYNC_INFERENCE_HPP_
#define SAMPLE_ASYNC_INFERENCE_HPP_

#include <memory>
#include <string>

#include "cnis/processor.h"
#include "cnis/infer_server.h"

#include "easy_module.hpp"


class SampleAsyncInference : public EasyModule {
 public:
  SampleAsyncInference(std::string name, int parallelism, int device_id, const std::string& model_path,
                       std::string model_name) : EasyModule(name, parallelism) {
    model_path_ = model_path;
    device_id_ = device_id;
    model_name_ = model_name;
  }

  ~SampleAsyncInference() = default;

  int Open() override;

  int Process(std::shared_ptr<EdkFrame> frame) override;

  int Close() override;

 private:
  std::string model_path_;
  int device_id_;
  std::string model_name_;
  std::shared_ptr<infer_server::IPreproc> preproc_;
  std::shared_ptr<infer_server::IPostproc> postproc_;
  infer_server::CnPreprocTensorParams params_;
  std::unique_ptr<infer_server::InferServer> infer_server_;
  infer_server::Session_t session_;
};

#endif
