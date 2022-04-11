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

"""OpencvFrame test

This module tests OpencvFrame related APIs

"""

import os, sys
import numpy as np
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis
import utils

def prepare_input():
  """Read image from file. Set OpenCV mat to OpencvFrame."""
  cv_frame = cnis.OpencvFrame()
  img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  cv_frame.img = img
  cv_frame.fmt = cnis.VideoPixelFmt.BGR24
  # Create package with one frame and set cv_frame to it
  input_pak = cnis.Package(1, utils.tag)
  input_pak.data[0].set(cv_frame)
  return input_pak

class TestOpencvFrame(object):
  """TestOpencvFrame class provides several APIs for testing OpencvFrame"""
  @staticmethod
  def test_opencv_frame():
    # Create InferServer
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(utils.model_dir)

    # Set OpencvPreproc
    session_desc.preproc = cnis.PreprocessorHost()
    session_desc.set_preproc_func(cnis.OpencvPreproc(dst_fmt=cnis.VideoPixelFmt.BGRA, keep_aspect_ratio=False).execute)
    session_desc.show_perf = False

    # create synchronous session
    session = infer_server.create_sync_session(session_desc)

    # Request opencv frame
    input_pak = prepare_input()
    output = cnis.Package(1, utils.tag)
    status = cnis.Status.SUCCESS
    assert infer_server.request_sync(session, input_pak, status, output)
    assert status == cnis.Status.SUCCESS

    # Request opencv frame with roi
    input_pak = prepare_input()
    cv_frame = input_pak.data[0].get_cv_frame()
    cv_frame.roi = cnis.BoundingBox(x=0, y=0, w=0.5, h=0.5)

    output = cnis.Package(1, utils.tag)
    status = cnis.Status.SUCCESS
    assert infer_server.request_sync(session, input_pak, status, output)
    assert status == cnis.Status.SUCCESS

    # Destroy session
    assert infer_server.destroy_session(session)
