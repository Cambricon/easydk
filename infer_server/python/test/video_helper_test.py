import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *
import numpy as np
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
tag = "stream_0"
ssd_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

class TestVideoFrame:
  def test_video_frame(self):
    video_frame = VideoFrame()
    video_frame.plane_num = 2
    video_frame.format = VideoPixelFmt.NV12
    video_frame.width = 1920
    video_frame.height = 1080
    video_frame.stride = [video_frame.width, video_frame.width]
    y_size = int(video_frame.stride[0] * video_frame.height)
    uv_size = int(video_frame.stride[1] * video_frame.height // 2)
    assert(video_frame.get_plane_size(0) == y_size)
    assert(video_frame.get_plane_size(1) == uv_size)
    assert(video_frame.get_total_size() == y_size + uv_size)
    mlu_buffer_y = Buffer(y_size, 0)
    mlu_buffer_uv = Buffer(uv_size, 0)
    dtype = np.dtype(np.uint8)
    y_data = np.random.randint(0, 255, size=y_size, dtype=dtype)
    uv_data = np.random.randint(0, 255, size=uv_size, dtype=dtype)
    mlu_buffer_y.copy_from(y_data)
    mlu_buffer_uv.copy_from(uv_data)
    video_frame.set_plane(0, mlu_buffer_y)
    video_frame.set_plane(1, mlu_buffer_uv)
    plane_0 = video_frame.get_plane(0)
    plane_1 = video_frame.get_plane(1)
    dst_y_data = np.random.randint(0, 255, size=y_size, dtype=dtype)
    dst_uv_data = np.random.randint(0, 255, size=uv_size, dtype=dtype)
    plane_0.copy_to(dst=dst_y_data)
    plane_1.copy_to(dst=dst_uv_data)
    assert(dst_y_data.any() == y_data.any())
    assert(dst_uv_data.any() == uv_data.any())
    # set roi
    video_frame.roi = BoundingBox(x=0, y=0, w=0.5, h=0.5)

  def test_get_plane_num(self):
    assert(get_plane_num(VideoPixelFmt.I420) == 3)
    assert(get_plane_num(VideoPixelFmt.NV12) == 2)
    assert(get_plane_num(VideoPixelFmt.NV21) == 2)
    assert(get_plane_num(VideoPixelFmt.RGB24) == 1)
    assert(get_plane_num(VideoPixelFmt.BGR24) == 1)
    assert(get_plane_num(VideoPixelFmt.RGBA) == 1)
    assert(get_plane_num(VideoPixelFmt.BGRA) == 1)
    assert(get_plane_num(VideoPixelFmt.ARGB) == 1)
    assert(get_plane_num(VideoPixelFmt.ABGR) == 1)

def prepare_input():
  # Prepare MLU data
  img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  w = img.shape[1]
  h = img.shape[0]
  # create a video_frame
  video_frame = VideoFrame()
  video_frame.plane_num = 2
  video_frame.format = VideoPixelFmt.NV12
  video_frame.width = w
  video_frame.height = h
  video_frame.stride = [video_frame.width, video_frame.width]

  # convert image from BGR24 TO YUV NV12
  i420_img = cv2.cvtColor(img, cv2.COLOR_BGR2YUV_I420)
  i420_img = i420_img.reshape(int(w * h * 3 / 2))
  img_y = i420_img[:w*h]
  img_uv = i420_img[w*h:]
  img_uv.reshape((int(w * h / 4), 2), order="F").reshape(int(w * h /2))

  # Create mlu buffer
  mlu_buffer_y = Buffer(w * h, 0)
  mlu_buffer_uv = Buffer(int(w * h / 2), 0)
  # copy to mlu buffer
  mlu_buffer_y.copy_from(img_y)
  mlu_buffer_uv.copy_from(img_uv)
  # set buffer to video_frame
  video_frame.set_plane(0, mlu_buffer_y)
  video_frame.set_plane(1, mlu_buffer_uv)
  return video_frame

class TestVideoInferServer:
  def test_video_infer_server_request_sync(self):
    infer_server = VideoInferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.preproc =  VideoPreprocessorMLU()
    session_desc.set_preproc_params(VideoPixelFmt.BGRA, VideoPreprocessType.CNCV_PREPROC, keep_aspect_ratio=False)
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)

    # input video_frame
    video_frame = prepare_input()
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, video_frame, tag, status, output))
    assert(status == Status.SUCCESS)

    # input package
    video_frame = prepare_input()
    input_pak = Package(1, tag)
    input_pak.data[0].set(video_frame)
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)

    # input with bounding box
    video_frame = prepare_input()
    bbox = [BoundingBox(x=0, y=0, w=0.5, h=0.5)] * 4
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, video_frame, bbox, tag, status, output))
    assert(status == Status.SUCCESS)
    infer_server.destroy_session(session)

  def test_video_infer_server_request_async(self):
    infer_server = VideoInferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.preproc =  VideoPreprocessorMLU()
    session_desc.set_preproc_params(VideoPixelFmt.BGRA, VideoPreprocessType.CNCV_PREPROC, keep_aspect_ratio=False)
    session_desc.show_perf = False

    class TestObserver(Observer):
      def __init(self):
        super().__init__()
        self.called = False
      def response_func(self, status, data, user_data):
        assert(status == Status.SUCCESS)
        assert("user_data" in user_data)
        assert(user_data["user_data"] == "cnis")
        self.called = True

    obs = TestObserver()
    session = infer_server.create_session(session_desc, obs)

    # input video_frame
    video_frame = prepare_input()
    user_data = {"user_data":"cnis"}
    assert(infer_server.request(session, video_frame, tag, user_data))

    # input package
    video_frame = prepare_input()
    input_pak = Package(1, tag)
    input_pak.data[0].set(video_frame)
    assert(infer_server.request(session, input_pak, user_data))

    # input with bounding box
    video_frame = prepare_input()
    bbox = [BoundingBox(x=0, y=0, w=0.5, h=0.5)] * 4
    assert(infer_server.request(session, video_frame, bbox, tag, user_data))

    infer_server.wait_task_done(session, tag)
    assert(obs.called)
    infer_server.destroy_session(session)
