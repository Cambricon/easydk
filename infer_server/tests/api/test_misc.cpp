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

#include <gtest/gtest.h>

#include <string>

#include "cnis/infer_server.h"
#include "cnis/processor.h"

#ifdef CNIS_WITH_CONTRIB
#include "cnis/contrib/opencv_frame.h"
#include "cnis/contrib/video_helper.h"

namespace infer_server {
using video::PixelFmt;
using video::VideoFrame;
TEST(InferServer, PlaneNum) {
  EXPECT_EQ(GetPlaneNum(PixelFmt::NV12), 2u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::NV21), 2u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::I420), 3u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::RGBA), 1u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::BGRA), 1u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::ARGB), 1u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::ABGR), 1u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::BGR24), 1u);
  EXPECT_EQ(GetPlaneNum(PixelFmt::RGB24), 1u);
}

TEST(InferServer, PlaneSize) {
  VideoFrame frame;
  frame.width = 224;
  frame.height = 224;
  size_t base_size = frame.width * frame.height;

  frame.format = PixelFmt::NV12;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size);
  EXPECT_EQ(frame.GetPlaneSize(1), base_size / 2);
  EXPECT_EQ(frame.GetPlaneSize(2), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 3 / 2);

  frame.format = PixelFmt::NV21;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size);
  EXPECT_EQ(frame.GetPlaneSize(1), base_size / 2);
  EXPECT_EQ(frame.GetPlaneSize(2), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 3 / 2);

  frame.format = PixelFmt::I420;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size);
  EXPECT_EQ(frame.GetPlaneSize(1), base_size / 4);
  EXPECT_EQ(frame.GetPlaneSize(2), base_size / 4);
  EXPECT_EQ(frame.GetPlaneSize(3), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 3 / 2);

  frame.format = PixelFmt::RGBA;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size * 4);
  EXPECT_EQ(frame.GetPlaneSize(1), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 4);

  frame.format = PixelFmt::ARGB;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size * 4);
  EXPECT_EQ(frame.GetPlaneSize(1), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 4);

  frame.format = PixelFmt::BGRA;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size * 4);
  EXPECT_EQ(frame.GetPlaneSize(1), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 4);

  frame.format = PixelFmt::ABGR;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size * 4);
  EXPECT_EQ(frame.GetPlaneSize(1), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 4);

  frame.format = PixelFmt::RGB24;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size * 3);
  EXPECT_EQ(frame.GetPlaneSize(1), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 3);

  frame.format = PixelFmt::BGR24;
  EXPECT_EQ(frame.GetPlaneSize(0), base_size * 3);
  EXPECT_EQ(frame.GetPlaneSize(1), 0u);
  EXPECT_EQ(frame.GetTotalSize(), base_size * 3);
}
}  // namespace infer_server
#endif  // CNIS_WITH_CONTRIB

namespace infer_server {

TEST(InferServer, DataTypeSize) {
  EXPECT_EQ(GetTypeSize(DataType::UINT8), 1u);
  EXPECT_EQ(GetTypeSize(DataType::FLOAT16), 2u);
  EXPECT_EQ(GetTypeSize(DataType::FLOAT32), 4u);
  EXPECT_EQ(GetTypeSize(DataType::INT32), 4u);
  EXPECT_EQ(GetTypeSize(DataType::INT16), 2u);
}

TEST(InferServer, PredictorBackend) {
#ifdef CNIS_USE_MAGICMIND
  EXPECT_EQ(Predictor::Backend(), std::string("magicmind"));
#else
  EXPECT_EQ(Predictor::Backend(), std::string("cnrt"));
#endif
}

}  // namespace infer_server
