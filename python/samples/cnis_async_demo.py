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

"""Async inference demo

This module demonstrates how to run asynchronous inference by using cnis python api.

First of all, we need an InferServer object. And secondly create an asynchronous Session with
preprocess (written in c++ code), inference (yolov5 network) and postprocess (written in c++ code).
Each frame is sent to InferServer by the request API, and after it is processed, the MyObserver::response API
will be called.
After all frames are sent to InferServer, call the wait_task_done API to wait all done.
The wait_task_done API will block until all frames are processed.
At last do not forget to destroy Session.

To run this script,
    python cnis_async_demo.py -dev 0

"""

import os, sys, time
import argparse
import cv2
import numpy as np

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis
import sample_utils


class AsyncDemo(object):
  """Asynchronous demo
    1. Create InferServer and Session
    2. Sent frames to InferServer
    3. Print results.
    4. Wait all task finished
    5. Destroy session
  """
  def __init__(self, device_id):
    self.tag = "stream_0"
    self.dev_id = device_id
    if cnis.get_platfrom_name() == "MLU370":
      self.model_dir = "http://video.cambricon.com/models/magicmind/v0.13.0/yolov5m_v0.13.0_4b_rgb_uint8.magicmind"
    else:
      self.model_dir = ""


  def cpp_preproc(self, session_desc):
    """Set Yolov5 preproc to session description"""
    session_desc.preproc = cnis.Preprocessor()
    self.preproc = cnis.PreprocYolov5()
    cnis.set_preproc_handler(session_desc.model.get_key(), self.preproc)

  def cpp_postproc(self, session_desc):
    """Set Yolov5 postproc to session description"""
    session_desc.postproc = cnis.Postprocessor()
    self.postproc = cnis.PostprocYolov5(0.5)
    cnis.set_postproc_handler(session_desc.model.get_key(), self.postproc)


  def prepare_mlu_data(self):
    """Read image from file. Convert OpenCV mat to BufSurface (convert color and copy data from cpu to mlu)"""
    # Prepare MLU data
    cv_image = cv2.imread(cur_file_dir + "/../data/test.jpg")
    img_width = cv_image.shape[1]
    img_height = cv_image.shape[0]
    # Create a BufSurface
    params = cnis.CnedkBufSurfaceCreateParams()
    params.mem_type = cnis.CnedkBufSurfaceMemType.DEVICE
    params.width = img_width
    params.height = img_height
    params.color_format = cnis.CnedkBufSurfaceColorFormat.NV12
    params.batch_size = 1
    params.device_id = 0
    params.force_align_1 = True
    mlu_buffer = cnis.cnedk_buf_surface_create(params)

    # Convert image from BGR24 TO YUV NV12
    data_size = int(img_width * img_height * 3 / 2)
    i420_img = cv2.cvtColor(cv_image, cv2.COLOR_BGR2YUV_I420)
    i420_img = i420_img.reshape(data_size)
    img_y = i420_img[:img_width*img_height]
    img_uv = i420_img[img_width*img_height:]
    img_uv = img_uv.reshape((int(img_width * img_height / 4), 2), order="F").reshape(int(img_width * img_height /2))
    img = np.concatenate((img_y, img_uv), axis=0)

    # Copy to mlu buffer
    mlu_buffer.get_surface_list(0).copy_from(img, data_size)
    # Set buffer wrapper to PreprocInput
    mlu_buffer_wrapper = cnis.CnedkBufSurfaceWrapper(mlu_buffer)
    preproc_input = cnis.PreprocInput()
    preproc_input.surf = mlu_buffer_wrapper
    # Create input package
    input_pak = cnis.Package(1, self.tag)
    input_pak.data[0].set(preproc_input)
    # Set user data to input data, as PostprocYolov5 need to known the image width and height
    input_pak.data[0].set_user_data({"image_width": img_width, "image_height": img_height})
    return input_pak


  class MyObserver(cnis.Observer):
    """To receive results from InferServer, we define a class MyObserver which inherits from cnis.Observer.
    After a request is sent to InferServer and is processed by InferServer, the response API will be called with
    status, results and user data.
    """
    def __init__(self):
      super().__init__()

    @staticmethod
    def print_result(output_pak):
      """Print object detection results"""
      for data in output_pak.data:
        objs = data.get_dict()["objs"]
        sample_utils.print_objs(objs)


    def response(self, status, data, user_data):
      if status == cnis.Status.SUCCESS:
        self.print_result(data)
      else:
        print("[EasyDK PyAPISamples] [AsyncDemo] MyObserver.response status: ", status)


  def execute(self):
    """Execute asynchronous demo"""
    # Create InferServer
    cnis.set_current_device(self.dev_id)
    infer_server = cnis.InferServer(self.dev_id)

    # Create session. Sync API
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session_async"
    session_desc.engine_num = 1
    session_desc.strategy = cnis.BatchStrategy.DYNAMIC

    # Load model
    session_desc.model = infer_server.load_model(self.model_dir)
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB

    # Set preproc and postproc
    self.cpp_preproc(session_desc)
    self.cpp_postproc(session_desc)

    # Create observer
    obs = self.MyObserver()

    # Create session
    session = infer_server.create_session(session_desc, obs)

    for _ in range(4):
      input_pak = self.prepare_mlu_data()
      user_data = {"user_data" : "this is user data"}

      # Request
      ret = infer_server.request(session, input_pak, user_data, timeout=20000)
      if not ret:
        print("[EasyDK PyAPISamples] [AsyncDemo] RequestAsync failed, ret: {}".format(ret))

    # Wait all tasks done
    infer_server.wait_task_done(session, self.tag)
    # Remove Preproc and Postproc handlers
    cnis.remove_preproc_handler(session_desc.model.get_key())
    cnis.remove_postproc_handler(session_desc.model.get_key())
    # Destroy Session
    infer_server.destroy_session(session)


def main():
  """Parser arguments and run the asynchronous inference demo. Set argument -dev to choose device id"""
  parser = argparse.ArgumentParser()
  parser.add_argument("-dev", dest="dev_id", type=int, required=False, default = 0, help="The device id")

  args = parser.parse_args()

  start = time.time()
  demo = AsyncDemo(args.dev_id)
  demo.execute()
  dur = time.time() - start
  print("\n[EasyDK PyAPISamples] [AsyncDemo] ## Async demo total time : {:.4f} second(s)\n".format(dur))


if __name__ == '__main__':
  main()
