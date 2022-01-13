import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *
import numpy as np
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
tag = "stream_0"
ssd_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

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

  # create package and set video_frame to it
  input_pak = Package(1, tag)
  input_pak.data[0].set(video_frame)
  return input_pak

def request(infer_server, session):
    # Test request sync
    input_pak = prepare_input()
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)

class TestVideoPreprocessorMLU:
  def test_video_preprocessor_mlu(self):
    preproc = VideoPreprocessorMLU()
    assert(preproc)

    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.preproc = preproc
    # CNCV
    session_desc.set_preproc_params(VideoPixelFmt.BGRA, VideoPreprocessType.CNCV_PREPROC, keep_aspect_ratio=False)
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)
    request(infer_server, session)
    infer_server.destroy_session(session)

    # RCOP
    session_desc.set_preproc_params(VideoPixelFmt.BGRA, VideoPreprocessType.RESIZE_CONVERT, keep_aspect_ratio=False)

    session = infer_server.create_sync_session(session_desc)
    request(infer_server, session)
    infer_server.destroy_session(session)
