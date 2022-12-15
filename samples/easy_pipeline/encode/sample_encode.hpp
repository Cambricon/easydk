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

#ifndef SAMPLE_ENCODE_HPP_
#define SAMPLE_ENCODE_HPP_

#include <condition_variable>
#include <string>
#include <map>
#include <memory>

#include "easy_module.hpp"

#include "cnedk_encode.h"
#include "encode_handler_mlu.hpp"

class SampleEncode : public EasyModule {
 public:
  explicit SampleEncode(std::string name, int parallelism, int device_id, std::string filename)
            : EasyModule(name, parallelism) {
    device_id_ = device_id;
    filename_ = filename;
  }

  ~SampleEncode();

  int Open() override;

  int Close() override;

  int Process(std::shared_ptr<EdkFrame> frame) override;

 private:
  int device_id_;
  VEncHandlerParam param_;
  std::mutex venc_mutex_;
  std::string filename_;
  std::map<int, std::shared_ptr<VencMluHandler>> ivenc_;
};

#endif
