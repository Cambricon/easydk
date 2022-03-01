"""VideoHelper test

This module tests VideoFrame and VideoInferServer related APIs

"""

import os, sys
import numpy as np
import cv2

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis
import utils

tag = "stream_0"
ssd_mlu270_model_dir = \
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

class TestVideoFrame(object):
  """TestVideoFrame class provides several APIs for testing VideoFrame"""
  @staticmethod
  def test_video_frame():
    # Create a VideoFrame object with a yuv420sp nv12 1920x1080 image.
    video_frame = cnis.VideoFrame()
    video_frame.plane_num = 2
    video_frame.format = cnis.VideoPixelFmt.NV12
    video_frame.width = 1920
    video_frame.height = 1080
    video_frame.stride = [video_frame.width, video_frame.width]
    y_size = int(video_frame.stride[0] * video_frame.height)
    uv_size = int(video_frame.stride[1] * video_frame.height // 2)
    assert video_frame.get_plane_size(0) == y_size
    assert video_frame.get_plane_size(1) == uv_size
    assert video_frame.get_total_size() == y_size + uv_size
    # Create Buffers to store y and uv data
    mlu_buffer_y = cnis.Buffer(y_size, 0)
    mlu_buffer_uv = cnis.Buffer(uv_size, 0)
    dtype = np.dtype(np.uint8)
    # Use random numbers as y and uv plane data
    y_data = np.random.randint(0, 255, size=y_size, dtype=dtype)
    uv_data = np.random.randint(0, 255, size=uv_size, dtype=dtype)
    mlu_buffer_y.copy_from(y_data)
    mlu_buffer_uv.copy_from(uv_data)
    # Set buffers to VideoFrame object
    video_frame.set_plane(0, mlu_buffer_y)
    video_frame.set_plane(1, mlu_buffer_uv)
    plane_0 = video_frame.get_plane(0)
    plane_1 = video_frame.get_plane(1)
    # Modify the y and uv data
    dst_y_data = np.random.randint(0, 255, size=y_size, dtype=dtype)
    dst_uv_data = np.random.randint(0, 255, size=uv_size, dtype=dtype)
    plane_0.copy_to(dst=dst_y_data)
    plane_1.copy_to(dst=dst_uv_data)
    assert dst_y_data.any() == y_data.any()
    assert dst_uv_data.any() == uv_data.any()
    # set roi
    video_frame.roi = cnis.BoundingBox(x=0, y=0, w=0.5, h=0.5)

  @staticmethod
  def test_get_plane_num():
    # Check plane num of serveral pixel format is correct
    assert cnis.get_plane_num(cnis.VideoPixelFmt.I420) == 3
    assert cnis.get_plane_num(cnis.VideoPixelFmt.NV12) == 2
    assert cnis.get_plane_num(cnis.VideoPixelFmt.NV21) == 2
    assert cnis.get_plane_num(cnis.VideoPixelFmt.RGB24) == 1
    assert cnis.get_plane_num(cnis.VideoPixelFmt.BGR24) == 1
    assert cnis.get_plane_num(cnis.VideoPixelFmt.RGBA) == 1
    assert cnis.get_plane_num(cnis.VideoPixelFmt.BGRA) == 1
    assert cnis.get_plane_num(cnis.VideoPixelFmt.ARGB) == 1
    assert cnis.get_plane_num(cnis.VideoPixelFmt.ABGR) == 1

class TestVideoInferServer(object):
  """TestVideoInferServer class provides several APIs for testing VideoInferServer"""
  @staticmethod
  def test_video_infer_server_request_sync():
    # Create VideoInferServer object
    infer_server = cnis.VideoInferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    # Create VideoPreprocessorMLU and set parameters. Use CNCV preproc.
    session_desc.preproc = cnis.VideoPreprocessorMLU()
    session_desc.set_preproc_params(cnis.VideoPixelFmt.BGRA, cnis.VideoPreprocessType.CNCV_PREPROC,
                                    keep_aspect_ratio=False)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    # Prepare input video_frame
    video_frame = utils.prepare_video_frame()
    output = cnis.Package(1, tag)
    status = cnis.Status.SUCCESS
    # Request VideoFrame
    assert infer_server.request_sync(session, video_frame, tag, status, output)
    assert status == cnis.Status.SUCCESS

    # Prepare input video_frame
    video_frame = utils.prepare_video_frame()
    input_pak = cnis.Package(1, tag)
    input_pak.data[0].set(video_frame)
    # Request input package
    assert infer_server.request_sync(session, input_pak, status, output)
    assert status == cnis.Status.SUCCESS

    # Prepare input with bounding boxes
    video_frame = utils.prepare_video_frame()
    bbox = [cnis.BoundingBox(x=0, y=0, w=0.5, h=0.5)] * 4
    output = cnis.Package(1, tag)
    status = cnis.Status.SUCCESS
    # Request VideoFrame and bounding boxes of it
    assert infer_server.request_sync(session, video_frame, bbox, tag, status, output)
    assert status == cnis.Status.SUCCESS

    # Destroy session
    infer_server.destroy_session(session)

  @staticmethod
  def test_video_infer_server_request_async():
    # Create VideoInferServer object
    infer_server = cnis.VideoInferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    # Create VideoPreprocessorMLU and set parameters. Use CNCV preproc.
    session_desc.preproc =  cnis.VideoPreprocessorMLU()
    session_desc.set_preproc_params(cnis.VideoPixelFmt.BGRA, cnis.VideoPreprocessType.CNCV_PREPROC,
                                    keep_aspect_ratio=False)
    session_desc.show_perf = False

    # Define a TestObserver class to receive results
    class TestObserver(cnis.Observer):
      """To receive results from InferServer, we define a class MyObserver which inherits from cnis.Observer.
      After a request is sent to InferServer and is processed by InferServer, the response_func API will be called with
      status, results and user data.
      """
      def __init__(self):
        super().__init__()
        self.called = False
      def response_func(self, status, data, user_data):
        assert status == cnis.Status.SUCCESS
        assert "user_data" in user_data
        assert user_data["user_data"] == "cnis"
        self.called = True

    # Create observer
    obs = TestObserver()
    # Create asynchronous session
    session = infer_server.create_session(session_desc, obs)

    # Prepare input video_frame
    video_frame = utils.prepare_video_frame()
    # Create user data
    user_data = {"user_data":"cnis"}
    # Request VideoFrame
    assert infer_server.request(session, video_frame, tag, user_data)

    # Prepare input package
    video_frame = utils.prepare_video_frame()
    input_pak = cnis.Package(1, tag)
    input_pak.data[0].set(video_frame)
    # Request package
    assert infer_server.request(session, input_pak, user_data)

    # Prepare input with bounding box
    video_frame = utils.prepare_video_frame()
    bbox = [cnis.BoundingBox(x=0, y=0, w=0.5, h=0.5)] * 4
    # Request VideoFrame and bounding boxes of it
    assert infer_server.request(session, video_frame, bbox, tag, user_data)

    infer_server.wait_task_done(session, tag)
    assert obs.called

    # Destroy session
    infer_server.destroy_session(session)
