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

tag = "stream_0"
ssd_mlu270_model_dir = \
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

def request_package(infer_server, session):
  """Request InferServer to process a package"""
  # Test request sync
  video_frame = utils.prepare_video_frame()
  # create package and set video_frame to it
  input_pak = cnis.Package(1, tag)
  input_pak.data[0].set(video_frame)

  output = cnis.Package(1, tag)
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
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    # Create VideoPreprocessorMLU and set parameters. Use CNCV preproc.
    session_desc.preproc = preproc
    session_desc.set_preproc_params(cnis.VideoPixelFmt.BGRA, cnis.VideoPreprocessType.CNCV_PREPROC,
                                    keep_aspect_ratio=False)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)
    # Request
    request_package(infer_server, session)
    # Destroy sessoion
    infer_server.destroy_session(session)

    # Use RCOP preproc.
    session_desc.set_preproc_params(cnis.VideoPixelFmt.BGRA, cnis.VideoPreprocessType.RESIZE_CONVERT,
                                    keep_aspect_ratio=False)

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)
    # Request
    request_package(infer_server, session)
    # Destroy sessoion
    infer_server.destroy_session(session)
