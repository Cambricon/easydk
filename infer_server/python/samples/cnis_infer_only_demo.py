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

First of all, we need an InferServer object. And secondly create a sync session with inference (ssd network).
Each frame that must be satisfied the model input shapes and layouts is sent to InferServer by the request_sync API,
and the request_sync API will block until it is processed. After that we could get model raw outputs and do postprocess.

At last do not forget to destroy session.

To run this script,
    python cnis_infer_only_demo.py -dev 0

"""

import os, sys, time
import argparse
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis
import sample_utils


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
    self.core_ver = cnis.get_device_core_version(self.dev_id)
    self.model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"
    if self.core_ver == cnis.CoreVersion.MLU220:
      self.model_dir = "http://video.cambricon.com/models/MLU220/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu220.cambricon"
    elif self.core_ver == cnis.CoreVersion.MLU370:
      self.model_dir = "http://video.cambricon.com/models/magicmind/v1.1.0/yolov3_v1.1.0_4b_rgb_uint8.magicmind"


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

    img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
    if self.core_ver == cnis.CoreVersion.MLU220 or self.core_ver == cnis.CoreVersion.MLU270:
      resized_img = cv2.resize(img, (width, height))
      result_img = cv2.cvtColor(resized_img, cv2.COLOR_BGR2BGRA)
    else:
      ratio = min(width / img.shape[1], height / img.shape[0])
      resized_w = int(ratio * img.shape[1])
      resized_h = int(ratio * img.shape[0])
      resized_img = cv2.resize(img, (resized_w, resized_h))
      delta_w = width - resized_w
      delta_h = height - resized_h
      top, bottom = delta_h // 2, delta_h - (delta_h // 2)
      left, right = delta_w // 2, delta_w - (delta_w // 2)
      color = [0, 0, 0]
      new_img = cv2.copyMakeBorder(resized_img, top, bottom, left, right, cv2.BORDER_CONSTANT,
                                   value=color)
      result_img = cv2.cvtColor(new_img, cv2.COLOR_BGR2RGB)
      input_pak.data[0].set_user_data({"image_width": img.shape[1], "image_height": img.shape[0]})

    input_pak.data[0].set(result_img)
    return input_pak


  def postproc_and_print(self, output_pak, model, threshold):
    """Do postproc and print detected objects"""
    if self.core_ver == cnis.CoreVersion.MLU220 or self.core_ver == cnis.CoreVersion.MLU270:
      """Do SSD postprocess and print object detection results"""
      for data in output_pak.data:
        model_io = data.get_model_io()
        objs = sample_utils.ssd_postproc(model_io, model, threshold)
        sample_utils.print_objs(objs)
    else:
      for data in output_pak.data:
        model_io = data.get_model_io()
        image_size = data.get_user_data()
        objs = sample_utils.yolov3mm_postproc(model_io, model, image_size, threshold)
        sample_utils.print_objs(objs)


  def execute(self):
    """Execyte Inference only demo"""
    # Create InferServer
    cnis.bind_device(self.dev_id)
    infer_server = cnis.InferServer(self.dev_id)

    # Create session. Sync API
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session_sync"
    session_desc.engine_num = 1
    session_desc.strategy = cnis.BatchStrategy.STATIC
    session_desc.model = infer_server.load_model(self.model_dir)
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
