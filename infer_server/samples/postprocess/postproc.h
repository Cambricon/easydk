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
#ifndef INFER_SERVER_SAMPLES_POSTPROCESS_POSTPROC_H_
#define INFER_SERVER_SAMPLES_POSTPROCESS_POSTPROC_H_

#include "cnis/contrib/video_helper.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"

struct DetectObject {
  int label;
  float score;
  infer_server::video::BoundingBox bbox;
};

struct FrameSize {
  int width;
  int height;
};  // struct FrameSize

inline float Clip(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

struct PostprocSSD {
  float threshold;

  explicit PostprocSSD(float _threshold) : threshold(_threshold) {}

  bool operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                  const infer_server::ModelInfo* model);
};  // struct PostprocSSD

struct PostprocYolov3MM {
  float threshold;

  explicit PostprocYolov3MM(float _threshold) : threshold(_threshold) {}

  bool operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                  const infer_server::ModelInfo* model);
  void SetFrameSize(FrameSize size) {
    size_ = size;
    set_frame_size_ = true;
  }

  FrameSize GetFrameSize() {
    return size_;
  }

 private:
  bool set_frame_size_ = false;
  FrameSize size_{0, 0};
};  // struct PostprocYolov3MM

struct PostprocYolov5 {
  float threshold;

  explicit PostprocYolov5(float _threshold) : threshold(_threshold) {}

  bool operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                  const infer_server::ModelInfo* model);
  void SetFrameSize(FrameSize size) {
    size_ = size;
    set_frame_size_ = true;
  }

  FrameSize GetFrameSize() {
    return size_;
  }

 private:
  bool set_frame_size_ = false;
  FrameSize size_{0, 0};
};  // struct PostprocYolov5

#endif  // INFER_SERVER_SAMPLES_POSTPROCESS_POSTPROC_H_
