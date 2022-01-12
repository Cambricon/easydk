import os, sys, time
import argparse
import numpy as np
import cv2

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
tag = "stream_0"
ssd_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"
ssd_mlu220_model_dir = "http://video.cambricon.com/models/MLU220/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu220.cambricon"


def opencv_preproc(session_desc):
  session_desc.preproc = PreprocessorHost()
  session_desc.set_preproc_func(OpencvPreproc(dst_fmt=VideoPixelFmt.BGRA, keep_aspect_ratio=False).execute)


def python_postproc(session_desc):
  def clip(x):
    return max(0, min(1, x))
  class CustomPostprocess(Postprocess):
    def __init__(self, threshold):
      super().__init__()
      self.threshold = threshold

    def execute_func(self, result, model_output, model_info):
      data = model_output.buffers[0].data(model_info.output_shape(0), model_info.output_layout(0))
      data = data.reshape(model_info.output_shape(0)[3])
      box_num = int(data[0])
      objs = []
      for i in range(box_num):
        obj = DetectObject()
        if data[64 + i * 7 + 1] == 0:
          continue
        obj.label = int(data[64 + i * 7 + 1] - 1)
        obj.score = data[64 + i * 7 + 2]
        if self.threshold > 0 and obj.score < self.threshold:
          continue
        obj.bbox.x = clip(data[64 + i * 7 + 3])
        obj.bbox.y = clip(data[64 + i * 7 + 4])
        obj.bbox.w = clip(data[64 + i * 7 + 5]) - obj.bbox.x
        obj.bbox.h = clip(data[64 + i * 7 + 6]) - obj.bbox.y
        objs.append(obj)
      result.set({"objs":objs})
      return True

  session_desc.postproc = Postprocessor()
  session_desc.set_postproc_func(CustomPostprocess(0.6).execute)


def prepare_opencv_data():
  # Create a video_frame
  cv_frame = OpencvFrame()
  img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  cv_frame.img = img
  cv_frame.fmt = VideoPixelFmt.BGR24

  # Create package with one frame and set cv_frame to it
  input_pak = Package(1, tag)
  input_pak.data[0].set(cv_frame)
  return input_pak


def print_result(output_pak):
  for data in output_pak.data:
    objs = data.get_dict()["objs"]
    if len(objs) == 0:
      print("@@@@@@@@@@@ No objects detected in frame ")
    print("objects number: ", len(objs))
    for obj in objs:
      print("obj label: {}  score: {:.4f}  bbox : {:.4f}, {:.4f}, {:.4f}, {:.4f}".format(
          obj.label, obj.score, obj.bbox.x, obj.bbox.y, obj.bbox.w, obj.bbox.h))


def sync_demo(platform):
  # Create InferServer
  infer_server = InferServer(dev_id=0)

  # Create session. Sync API
  session_desc = SessionDesc()
  session_desc.name = "test_session_sync"
  session_desc.engine_num = 1
  session_desc.strategy = BatchStrategy.STATIC
  if platform in ["mlu220", "220", "MLU220"]:
    session_desc.model = infer_server.load_model(ssd_mlu220_model_dir)
  else:
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
  opencv_preproc(session_desc)
  python_postproc(session_desc)
  session = infer_server.create_sync_session(session_desc)

  for i in range(4):
    # Prepare input and output
    input_pak = prepare_opencv_data()
    output_pak = Package(1)

    # Request
    status = Status.SUCCESS
    ret = infer_server.request_sync(session, input_pak, status, output_pak, timeout=20000)
    if not ret:
      print("RequestSync failed, ret: {}, status: {}".format(ret, status))
    if status == Status.SUCCESS:
      print_result(output_pak)

  # Destroy Session
  infer_server.destroy_session(session)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("-p", dest="platform", required=False, default = "mlu270",
                      help="The platform, choose from mlu270 or mlu220")

  args = parser.parse_args()

  start = time.time()
  sync_demo(args.platform)
  dur = time.time() - start
  print("\n## Sync demo  total time : {:.4f} second(s)\n".format(dur))


if __name__ == '__main__':
  main()
