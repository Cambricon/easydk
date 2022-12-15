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

"""Do inference only demo

This module demonstrates how to run inference only by using cnis python api.
(The preprocess should be done before sent frame to InferServer, likewise the model raw results will be sent out)

First of all, we need an InferServer object. And secondly create a sync session with inference (yolov5).
Each frame that must be satisfied the model input shapes and layouts is sent to InferServer by the request_sync API,
and the request_sync API will block until it is processed. After that we could get model raw outputs and do postprocess.

At last do not forget to destroy session.

To run this script,
    python cnis_infer_only_demo.py -dev 0

"""

import os, sys, time
import argparse
import cv2
import math

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis
import sample_utils


def letterbox(img, dst_shape, pad_val):
  src_h, src_w = img.shape[0], img.shape[1]
  dst_h, dst_w = dst_shape
  ratio = min(dst_h / src_h, dst_w / src_w)
  unpad_h, unpad_w = int(math.floor(src_h * ratio)), int(math.floor(src_w * ratio))
  if ratio != 1:
    interp = cv2.INTER_AREA if ratio < 1 else cv2.INTER_LINEAR
    img = cv2.resize(img, (unpad_w, unpad_h), interp)
  # padding
  pad_t = int(math.floor((dst_h - unpad_h) / 2))
  pad_b = dst_h - unpad_h - pad_t
  pad_l = int(math.floor((dst_w - unpad_w) / 2))
  pad_r = dst_w - unpad_w - pad_l
  img = cv2.copyMakeBorder(img, pad_t, pad_b, pad_l, pad_r, cv2.BORDER_CONSTANT, value=(pad_val, pad_val, pad_val))
  return img

def to_range(val : float, min_val, max_val):
  return min(max(val, min_val), max_val)

class SyncDefaultPreprocPostprocDemo(object):
  """Inference only demo API
  1. Create InferServer and Session (with default preprocess and postprocess)
  2. Sent frames to InferServer
  3. Do postprocess and print results
  4. Destroy session
  """
  def __init__(self, device_id):
    self.tag = "stream_0"
    self.dev_id = device_id
    if cnis.get_platfrom_name() == "MLU370":
      self.model_dir = "http://video.cambricon.com/models/magicmind/v0.13.0/yolov5m_v0.13.0_4b_rgb_uint8.magicmind"
    else:
      self.model_dir = ""
    self.threshold = 0.5


  @staticmethod
  def get_model_input_wh(model):
    """Get the input width and height of the model"""
    width = 0
    height = 0
    order = model.input_layout(0).order
    if order == cnis.DimOrder.NHWC:
      width = model.input_shape(0)[2]
      height = model.input_shape(0)[1]
    elif order == cnis.DimOrder.NCHW:
      width = model.input_shape(0)[1]
      height =model.input_shape(0)[0]
    else:
      print("[EasyDK PythonAPISamples] [InferOnlyDemo] Unsupported dim order")
    return width, height


  def prepare_input_and_preproc(self, width, height):
    """Read image from file. Convert color and resize the image to satisfy the model input"""
    input_pak = cnis.Package(1, self.tag)

    src_img = cv2.imread(cur_file_dir + "/../data/test.jpg")
    src_img = letterbox(src_img, (height, width), 114)
    # convert color to model input pixel format
    src_img = cv2.cvtColor(src_img, cv2.COLOR_BGR2RGB)

    preproc_input = cnis.PreprocInput()
    preproc_input.surf = cnis.convert_to_buf_surf(src_img, cnis.CnedkBufSurfaceColorFormat.RGB)
    # Create input package and set PreprocInput to input package
    input_pak = cnis.Package(1, self.tag)
    input_pak.data[0].set(preproc_input)
    input_pak.data[0].set_user_data({"image_width": src_img.shape[1], "image_height": src_img.shape[0]})

    return input_pak


  def postproc_and_print(self, output_pak, model_info, threshold):
    """Do postproc and print detected objects"""
    cnis.set_current_device(self.dev_id)
    for data in output_pak.data:
      model_io = data.get_model_io()
      image_size = data.get_user_data()

      output0 = model_io.surfs[0]
      output1 = model_io.surfs[1]

      model_input_w = model_info.input_shape(0)[2]
      model_input_h = model_info.input_shape(0)[1]
      if model_info.input_layout(0).order == cnis.DimOrder.NCHW:
        model_input_w = model_info.input_shape(0)[3]
        model_input_h = model_info.input_shape(0)[2]

      cnis.set_current_device(self.dev_id)
      data = output0.get_data(plane_idx = 0, batch_idx = 0, dtype=cnis.DataType.FLOAT32)
      box_num = output1.get_data(plane_idx = 0, batch_idx = 0)[0]
      if box_num == 0:
        continue

      image_w = int(image_size["image_width"])
      image_h = int(image_size["image_height"])

      scaling_w = model_input_w / image_w
      scaling_h = model_input_h / image_h
      scaling = min(scaling_w, scaling_h)
      scaling_factor_w = scaling_w / scaling
      scaling_factor_h = scaling_h / scaling

      objs = []
      box_step = 7
      for i in range(box_num):
        left = to_range(data[i * box_step + 3], 0, model_input_w)
        top = to_range(data[i * box_step + 4], 0, model_input_h)
        right = to_range(data[i * box_step + 5], 0, model_input_w)
        bottom = to_range(data[i * box_step + 6], 0, model_input_h)

        # rectify
        left = to_range((left / model_input_w - 0.5) * scaling_factor_w + 0.5, 0, 1)
        top = to_range((top / model_input_h - 0.5) * scaling_factor_h + 0.5, 0, 1)
        right = to_range((right / model_input_w - 0.5) * scaling_factor_w + 0.5, 0, 1)
        bottom = to_range((bottom / model_input_h - 0.5) * scaling_factor_h + 0.5, 0, 1)

        if right <= left or bottom <= top:
          continue

        obj = cnis.DetectObject()
        obj.label = int(data[i * box_step + 1])
        obj.score = data[i * box_step + 2]
        obj.bbox.x = left
        obj.bbox.y = top
        obj.bbox.w = min(1 - obj.bbox.x, right - left)
        obj.bbox.h = min(1 - obj.bbox.y, bottom - top)
        if (self.threshold > 0 and obj.score < self.threshold) or obj.bbox.w <= 0 or obj.bbox.h <= 0:
          continue
        objs.append(obj)

      sample_utils.print_objs(objs)


  def execute(self):
    """Execute Inference only demo"""
    # Create InferServer
    cnis.set_current_device(self.dev_id)
    infer_server = cnis.InferServer(self.dev_id)

    # Create session. Sync API
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session_sync"
    session_desc.engine_num = 1
    session_desc.strategy = cnis.BatchStrategy.STATIC
    session_desc.model = infer_server.load_model(self.model_dir)
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB
    session = infer_server.create_sync_session(session_desc)

    width, height = self.get_model_input_wh(session_desc.model)

    for _ in range(4):
      # Prepare input and output
      input_pak = self.prepare_input_and_preproc(width, height)
      output_pak = cnis.Package(1)

      # Request
      status = cnis.Status.SUCCESS
      ret = infer_server.request_sync(session, input_pak, status, output_pak, timeout=20000)
      if not ret:
        print("[EasyDK PythonAPISamples] [InferOnlyDemo] RequestSync failed, ret: {}, status: {}".format(
            ret, status))

      # Postproc and print results
      if status == cnis.Status.SUCCESS:
        self.postproc_and_print(output_pak, session_desc.model, 0.6)

    # Destroy Session
    infer_server.destroy_session(session)


def main():
  """Parser arguments and run the inference only demo. Set argument -dev to choose device id"""
  parser = argparse.ArgumentParser()
  parser.add_argument("-dev", dest="dev_id", type=int, required=False, default = 0, help="The device id")

  args = parser.parse_args()

  # Infer server will do inference only, users should do preproc and postproc before and after request
  start = time.time()
  demo = SyncDefaultPreprocPostprocDemo(args.dev_id)
  demo.execute()
  dur = time.time() - start
  print("\n##[EasyDK PythonAPISamples] [InferOnlyDemo] Sync demo (default preproc/postproc) total time : {:.4f} second(s)\n".format(dur))


if __name__ == '__main__':
  main()
