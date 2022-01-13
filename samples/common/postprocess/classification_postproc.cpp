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
#include <math.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "postproc.h"

bool PostprocClassification::operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                             const infer_server::ModelInfo* model) {
  const float* data = reinterpret_cast<const float*>(model_output.buffers[0].Data());
  auto len = model->OutputShape(0).DataCount();

  std::vector<DetectObject> objs;
  for (decltype(len) i = 0; i < len; ++i) {
    if (data[i] < threshold) continue;
    DetectObject obj;
    obj.label = i;
    obj.score = data[i];
    objs.emplace_back(std::move(obj));
  }

  sort(objs.begin(), objs.end(), [](const DetectObject& lsb, const DetectObject& rsb) {
    return lsb.score > rsb.score;
  });

  result->Set(std::move(objs));
  return true;
}
