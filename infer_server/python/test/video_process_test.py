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

"""VideoProcessor test

This module tests VideoPreprocessorMLU related APIs

"""

import os, sys
import numpy as np
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis
import utils

def request_package(infer_server, session):
  """Request InferServer to process a package"""
  # Test request sync
  video_frame = utils.prepare_video_frame()
  # create package and set video_frame to it
  input_pak = cnis.Package(1, utils.tag)
  input_pak.data[0].set(video_frame)

  output = cnis.Package(1, utils.tag)
  status = cnis.Status.SUCCESS
  assert infer_server.request_sync(session, input_pak, status, output)
  assert status == cnis.Status.SUCCESS

class TestVideoPreprocessorMLU(object):
  """TestVideoPreprocessorMLU class provides several APIs for testing VideoPreprocessorMLU"""
  @staticmethod
  def test_video_preprocessor_mlu():
    # Create VideoPreprocessorMLU
    preproc = cnis.VideoPreprocessorMLU()
    assert preproc

    # Create InferServer object
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(utils.model_dir)

    # Create VideoPreprocessorMLU and set parameters. Use CNCV preproc.
    session_desc.preproc = preproc
    if cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU270 or \
       cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU220:
      session_desc.set_preproc_params(cnis.VideoPixelFmt.BGRA, cnis.VideoPreprocessType.CNCV_PREPROC,
                                      keep_aspect_ratio=False)
    else:
      session_desc.set_preproc_params(cnis.VideoPixelFmt.RGB24, cnis.VideoPreprocessType.CNCV_PREPROC,
                                      keep_aspect_ratio=True)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)
    # Request
    request_package(infer_server, session)
    # Destroy sessoion
    infer_server.destroy_session(session)

    # Use RCOP preproc.
    if cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU270 or \
       cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU220:
      session_desc.set_preproc_params(cnis.VideoPixelFmt.BGRA, cnis.VideoPreprocessType.RESIZE_CONVERT,
                                      keep_aspect_ratio=False)
      # Create synchronous session
      session = infer_server.create_sync_session(session_desc)
      # Request
      request_package(infer_server, session)
      # Destroy sessoion
      infer_server.destroy_session(session)
