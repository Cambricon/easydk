# ==============================================================================
# Copyright (C) [2022] by Cambricon, Inc. All rights reserved
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# ==============================================================================

"""Sample utilities

  This module defines SSD postproc, Yolov3MM postproc APIs.

"""
import cnis

def clip(x):
  """Limit the number in range [0, 1].
  if x < 0, x = 0
      x > 1, x = 1
  otherwise x = x
  """
  return max(0, min(1, x))


def ssd_postproc(model_outputs, model_info, threshold):
  """SSD postproc"""
  data = model_outputs.buffers[0].data(model_info.output_shape(0), model_info.output_layout(0))
  data = data.reshape(model_info.output_shape(0)[3])
  box_num = int(data[0])
  objs = []
  for i in range(box_num):
    obj = cnis.DetectObject()
    if data[64 + i * 7 + 1] == 0:
      continue
    obj.label = int(data[64 + i * 7 + 1] - 1)
    obj.score = data[64 + i * 7 + 2]
    if threshold > 0 and obj.score < threshold:
      continue
    # clip to 0-1
    obj.bbox.x = clip(data[64 + i * 7 + 3])
    obj.bbox.y = clip(data[64 + i * 7 + 4])
    obj.bbox.w = clip(data[64 + i * 7 + 5]) - obj.bbox.x
    obj.bbox.h = clip(data[64 + i * 7 + 6]) - obj.bbox.y
    objs.append(obj)
  return objs

def yolov3mm_postproc(model_outputs, model_info, image_size, threshold):
  """Yolov3mm postproc"""
  image_w = int(image_size["image_width"])
  image_h = int(image_size["image_height"])

  model_input_w = model_info.input_shape(0)[2]
  model_input_h = model_info.input_shape(0)[1]
  if model_info.input_layout(0).order == cnis.DimOrder.NCHW:
    model_input_w = model_info.input_shape(0)[3]
    model_input_h = model_info.input_shape(0)[2]

  scaling_factors = min(1.0 * model_input_w / image_w, 1.0 * model_input_h / image_h)

  scaled_w = scaling_factors * image_w
  scaled_h = scaling_factors * image_h

  box_num = model_outputs.buffers[1].data(dtype=cnis.DataType.INT32)[0]

  data = model_outputs.buffers[0].data(dtype=cnis.DataType.FLOAT32)
  objs = []
  box_step = 7
  for i in range(box_num):
    left = clip(data[i * box_step + 3])
    right = clip(data[i * box_step + 5])
    top = clip(data[i * box_step + 4])
    bottom = clip(data[i * box_step + 6])

    # rectify
    left = (left * model_input_w - (model_input_w - scaled_w) / 2) / scaled_w
    right = (right * model_input_w - (model_input_w - scaled_w) //2) / scaled_w
    top = (top * model_input_h - (model_input_h - scaled_h) / 2) / scaled_h
    bottom = (bottom * model_input_h - (model_input_h - scaled_h) / 2) / scaled_h
    left = max(0, left)
    right = max(0, right)
    top = max(0, top)
    bottom = max(0, bottom)

    obj = cnis.DetectObject()
    obj.label = int(data[i * box_step + 1])
    obj.score = data[i * box_step + 2]
    obj.bbox.x = left
    obj.bbox.y = top
    obj.bbox.w = min(1 - obj.bbox.x, right - left)
    obj.bbox.h = min(1 - obj.bbox.y, bottom - top)
    if (threshold > 0 and obj.score < threshold) or obj.bbox.w <= 0 or obj.bbox.h <= 0:
      continue
    objs.append(obj)
  return objs

def print_objs(objs):
  if len(objs) == 0:
    print("[EasyDK PyAPISamples] @@@@@@@@@@@ No objects detected in frame ")
    return
  print("[EasyDK PyAPISamples] objects number: ", len(objs))
  for obj in objs:
    print("[EasyDK PyAPISamples] obj label: {}  score: {:.4f}  bbox : {:.4f}, {:.4f}, {:.4f}, {:.4f}".format(
        obj.label, obj.score, obj.bbox.x, obj.bbox.y, obj.bbox.w, obj.bbox.h))