import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *
import numpy as np
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]

tag = "stream_0"
ssd_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

def prepare_input():
  cv_frame = OpencvFrame()
  img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  cv_frame.img = img
  cv_frame.fmt = VideoPixelFmt.BGR24
  # Create package with one frame and set cv_frame to it
  input_pak = Package(1, tag)
  input_pak.data[0].set(cv_frame)
  return input_pak

class TestOpencvFrame:
  def test_opencv_frame(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.preproc = PreprocessorHost()
    session_desc.set_preproc_func(OpencvPreproc(dst_fmt=VideoPixelFmt.BGRA, keep_aspect_ratio=False).execute)
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)

    # Request opencv frame
    input_pak = prepare_input()
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)

    # Request opencv frame with roi
    input_pak = prepare_input()
    cv_frame = input_pak.data[0].get_cv_frame()
    cv_frame.roi = BoundingBox(x=0, y=0, w=0.5, h=0.5)

    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)

    assert(infer_server.destroy_session(session))
