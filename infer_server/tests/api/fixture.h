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

#ifndef INFER_SERVER_API_TEST_FIXTURE_H_
#define INFER_SERVER_API_TEST_FIXTURE_H_

#include "infer_server.h"
#include "test_base.h"

class InferServerTestAPI : public InferServerTest {
 public:
  InferServerTestAPI() : server_(new infer_server::InferServer(device_id_)) {}

 protected:
  void SetUp() override { SetMluContext(); }
  void TearDown() override {}
  std::unique_ptr<infer_server::InferServer> server_;
};

#ifdef CNIS_WITH_CONTRIB
#include <opencv2/opencv.hpp>

#include "video_helper.h"
bool cvt_bgr_to_yuv420sp(const cv::Mat& bgr_image, uint32_t alignment, infer_server::video::PixelFmt pixel_fmt,
                         uint8_t* yuv_2planes_data);
#endif

#endif  // INFER_SERVER_API_TEST_FIXTURE_H_
