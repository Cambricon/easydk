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

#ifndef SAMPLE_OSD_HPP_
#define SAMPLE_OSD_HPP_


#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "edk_frame.hpp"
#include "easy_module.hpp"

#include "cnosd.h"

class SampleOsd : public EasyModule {
 public:
  explicit SampleOsd(std::string name, int parallelism, std::string label_path) : EasyModule(name, parallelism) {
    lable_path_ = label_path;
  }

  ~SampleOsd();

  int Open() override;

  int Close() override;

  int Process(std::shared_ptr<EdkFrame> frame) override;

 private:
  std::string lable_path_;
  std::map<int, std::shared_ptr<CnOsd>> osd_ctx_;
  std::mutex cnosd_mutex_;

  // CnOsd osd_;
};


#endif
