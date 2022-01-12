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
#include <utility>
#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "postproc.h"

bool PostprocSSD::operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                             const infer_server::ModelInfo* model) {
  std::vector<DetectObject> objs;
  const float* data = reinterpret_cast<const float*>(model_output.buffers[0].Data());
  int box_num = data[0];
  data += 64;

  for (int bi = 0; bi < box_num; ++bi) {
    DetectObject obj;
    if (data[1] == 0) continue;
    obj.label = data[1] - 1;
    obj.score = data[2];
    if (threshold > 0 && obj.score < threshold) continue;
    obj.bbox.x = Clip(data[3]);
    obj.bbox.y = Clip(data[4]);
    obj.bbox.w = Clip(data[5]) - obj.bbox.x;
    obj.bbox.h = Clip(data[6]) - obj.bbox.y;
    objs.emplace_back(std::move(obj));
    data += 7;
  }

  result->Set(std::move(objs));
  return true;
}
