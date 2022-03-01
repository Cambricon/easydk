"""Sync inference demo

This module demonstrates how to run synchronous inference by using cnis python api.

First of all, we need an InferServer object. And secondly create a synchronous session with inference (ssd network).
inference (ssd network) and postprocess (written in python code).
Each frame is sent to InferServer by the request_sync API, and the request_sync API will block until it is processed.
At last do not forget to destroy session.

To run this script, on MLU270:
    python cnis_sync_demo.py -p mlu270
, on MLU220:
    python cnis_sync_demo.py -p mlu220

"""

import os, sys, time
import argparse
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis

tag = "stream_0"
ssd_mlu270_model_dir = \
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"
ssd_mlu220_model_dir = \
    "http://video.cambricon.com/models/MLU220/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu220.cambricon"


def opencv_preproc(session_desc):
  """Set OpenCV preproc to session description"""
  session_desc.preproc = cnis.PreprocessorHost()
  session_desc.set_preproc_func(cnis.OpencvPreproc(dst_fmt=cnis.VideoPixelFmt.BGRA, keep_aspect_ratio=False).execute)


def python_postproc(session_desc):
  """Define SSD postprocess and set it to session description"""
  def clip(x):
    """Limit the number in range [0, 1].
    if x < 0, x = 0
       x > 1, x = 1
    otherwise x = x
    """
    return max(0, min(1, x))
  class CustomPostprocess(cnis.Postprocess):
    """To use custom postprocess, we define a class CustomPostprocess which inherits from cnis.Postprocess.
    The execute_func API will be called by InferServer to do postprocess.
    """
    def __init__(self, threshold):
      super().__init__()
      self.threshold = threshold

    def execute_func(self, result, model_output, model_info):
      data = model_output.buffers[0].data(model_info.output_shape(0), model_info.output_layout(0))
      data = data.reshape(model_info.output_shape(0)[3])
      box_num = int(data[0])
      objs = []
      for i in range(box_num):
        obj = cnis.DetectObject()
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

  session_desc.postproc = cnis.Postprocessor()
  session_desc.set_postproc_func(CustomPostprocess(0.6).execute)


def prepare_opencv_data():
  """Read image from file. Set OpenCV mat to OpencvFrame."""
  # Create a video_frame
  cv_frame = cnis.OpencvFrame()
  img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  cv_frame.img = img
  cv_frame.fmt = cnis.VideoPixelFmt.BGR24

  # Create package with one frame and set cv_frame to it
  input_pak = cnis.Package(1, tag)
  input_pak.data[0].set(cv_frame)
  return input_pak


def print_result(output_pak):
  """Print object detection results"""
  for data in output_pak.data:
    objs = data.get_dict()["objs"]
    if len(objs) == 0:
      print("@@@@@@@@@@@ No objects detected in frame ")
    print("objects number: ", len(objs))
    for obj in objs:
      print("obj label: {}  score: {:.4f}  bbox : {:.4f}, {:.4f}, {:.4f}, {:.4f}".format(
          obj.label, obj.score, obj.bbox.x, obj.bbox.y, obj.bbox.w, obj.bbox.h))


def sync_demo(platform):
  """Synchronous demo API
  1. Create InferServer and Session
  2. Sent frames to InferServer
  3. Print results.
  4. Destroy session
  """
  # Create InferServer
  infer_server = cnis.InferServer(dev_id=0)

  # Create session. Sync API
  session_desc = cnis.SessionDesc()
  session_desc.name = "test_session_sync"
  session_desc.engine_num = 1
  session_desc.strategy = cnis.BatchStrategy.STATIC
  if platform in ["mlu220", "220", "MLU220"]:
    session_desc.model = infer_server.load_model(ssd_mlu220_model_dir)
  else:
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
  opencv_preproc(session_desc)
  python_postproc(session_desc)
  session = infer_server.create_sync_session(session_desc)

  for _ in range(4):
    # Prepare input and output
    input_pak = prepare_opencv_data()
    output_pak = cnis.Package(1)

    # Request
    status = cnis.Status.SUCCESS
    ret = infer_server.request_sync(session, input_pak, status, output_pak, timeout=20000)
    if not ret:
      print("RequestSync failed, ret: {}, status: {}".format(ret, status))
    if status == cnis.Status.SUCCESS:
      print_result(output_pak)

  # Destroy Session
  infer_server.destroy_session(session)


def main():
  """Parser arguments and run the synchronous inference demo. Set argument -p to choose platform"""
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
