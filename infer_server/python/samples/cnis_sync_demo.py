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

"""Sync inference demo

This module demonstrates how to run synchronous inference by using cnis python api.

First of all, we need an InferServer object. And secondly create a synchronous session with inference (ssd network).
inference (ssd network) and postprocess (written in python code).
Each frame is sent to InferServer by the request_sync API, and the request_sync API will block until it is processed.
At last do not forget to destroy session.

To run this script,
    python cnis_sync_demo.py -dev 0

"""

import os, sys, time
import argparse
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis
import sample_utils


class SyncDemo(object):
  """Synchronous demo API
  1. Create InferServer and Session
  2. Sent frames to InferServer
  3. Print results.
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


  def opencv_preproc(self, session_desc):
    """Set OpenCV preproc to session description"""
    session_desc.preproc = cnis.PreprocessorHost()
    if self.core_ver == cnis.CoreVersion.MLU220 or self.core_ver == cnis.CoreVersion.MLU270:
      session_desc.set_preproc_func(
          cnis.OpencvPreproc(dst_fmt=cnis.VideoPixelFmt.BGRA, keep_aspect_ratio=False).execute)
    else:
      session_desc.set_preproc_func(
          cnis.OpencvPreproc(dst_fmt=cnis.VideoPixelFmt.RGB24, keep_aspect_ratio=True).execute)


  def python_postproc(self, session_desc):
    """Define postprocess and set it to session description"""
    class CustomSSDPostprocess(cnis.Postprocess):
      """To use custom postprocess, we define a class CustomPostprocess which inherits from cnis.Postprocess.
      The execute_func API will be called by InferServer to do postprocess.
      """
      def __init__(self, threshold):
        super().__init__()
        self.threshold = threshold

      def execute_func(self, result, model_output, model_info):
        objs = sample_utils.ssd_postproc(model_output, model_info, self.threshold)
        result.set({"objs":objs})
        return True


    class CustomYolov3MMPostprocess(cnis.Postprocess):
      """To use custom postprocess, we define a class CustomPostprocess which inherits from cnis.Postprocess.
      The execute_func API will be called by InferServer to do postprocess.
      """
      def __init__(self, threshold):
        super().__init__()
        self.threshold = threshold

      def execute_func(self, result, model_output, model_info):
        image_size = result.get_user_data()
        objs = sample_utils.yolov3mm_postproc(model_output, model_info, image_size, self.threshold)
        result.set({"objs":objs})
        return True


    session_desc.postproc = cnis.Postprocessor()
    if self.core_ver == cnis.CoreVersion.MLU220 or self.core_ver == cnis.CoreVersion.MLU270:
      session_desc.set_postproc_func(CustomSSDPostprocess(0.6).execute)
    else:
      session_desc.set_postproc_func(CustomYolov3MMPostprocess(0.6).execute)


  def prepare_opencv_data(self):
    """Read image from file. Set OpenCV mat to OpencvFrame."""
    # Create a video_frame
    cv_frame = cnis.OpencvFrame()
    img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
    cv_frame.img = img
    cv_frame.fmt = cnis.VideoPixelFmt.BGR24

    # Create package with one frame and set cv_frame to it
    input_pak = cnis.Package(1, self.tag)
    input_pak.data[0].set(cv_frame)
    input_pak.data[0].set_user_data({"image_width": img.shape[1], "image_height": img.shape[0]})
    return input_pak


  @staticmethod
  def print_result(output_pak):
    """Print object detection results"""
    for data in output_pak.data:
      objs = data.get_dict()["objs"]
      sample_utils.print_objs(objs)


  def execute(self):
    """Execyte synchronous demo"""
    # Create InferServer
    cnis.bind_device(self.dev_id)
    infer_server = cnis.InferServer(self.dev_id)

    # Create session. Sync API
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session_sync"
    session_desc.engine_num = 1
    session_desc.strategy = cnis.BatchStrategy.STATIC
    session_desc.model = infer_server.load_model(self.model_dir)
    self.opencv_preproc(session_desc)
    self.python_postproc(session_desc)
    session = infer_server.create_sync_session(session_desc)

    for _ in range(4):
      # Prepare input and output
      input_pak = self.prepare_opencv_data()
      output_pak = cnis.Package(1)

      # Request
      status = cnis.Status.SUCCESS
      ret = infer_server.request_sync(session, input_pak, status, output_pak, timeout=20000)
      if not ret:
        print("[EasyDK PythonAPISamples] [SyncDemo] RequestSync failed, ret: {}, status: {}".format(ret, status))
      if status == cnis.Status.SUCCESS:
        self.print_result(output_pak)

    # Destroy Session
    infer_server.destroy_session(session)


def main():
  """Parser arguments and run the synchronous inference demo. Set argument -dev to choose device id"""
  parser = argparse.ArgumentParser()
  parser.add_argument("-dev", dest="dev_id", type=int, required=False, default = 0, help="The device id")

  args = parser.parse_args()

  start = time.time()
  demo = SyncDemo(args.dev_id)
  demo.execute()
  dur = time.time() - start
  print("\n[EasyDK PythonAPISamples] [SyncDemo] ## Sync demo  total time : {:.4f} second(s)\n".format(dur))


if __name__ == '__main__':
  main()
