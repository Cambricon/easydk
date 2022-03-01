"""Async inference demo

This module demonstrates how to run asynchronous inference by using cnis python api.

First of all, we need an InferServer object. And secondly create an asynchronous Session with cncv preprocess,
inference (yolov3 network) and postprocess (written in c++ code).
Each frame is sent to InferServer by the request API, and after it is processed, the MyObserver::response_func API
will be called.
After all frames are sent to InferServer, call the wait_task_done API to wait all done.
The wait_task_done API will block until all frames are processed.
At last do not forget to destroy Session.

To run this script, on MLU270:
    python cnis_async_demo.py -p mlu270
, on MLU220:
    python cnis_async_demo.py -p mlu220

"""

import os, sys, time
import argparse
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis

tag = "stream_0"
yolov3_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/yolov3_b4c4_argb_mlu270.cambricon"
yolov3_mlu220_model_dir = "http://video.cambricon.com/models/MLU220/yolov3_b4c4_argb_mlu220.cambricon"


def cncv_preproc(session_desc):
  """Set CNCV preproc to session description"""
  session_desc.preproc = cnis.VideoPreprocessorMLU()
  session_desc.set_preproc_params(cnis.VideoPixelFmt.ARGB, cnis.VideoPreprocessType.CNCV_PREPROC,
                                  keep_aspect_ratio=True)


def cpp_postproc(session_desc):
  """Set Yolov3 postproc to session description"""
  session_desc.postproc = cnis.Postprocessor()
  session_desc.set_postproc_func(cnis.PostprocYolov3(0.5).execute)


def prepare_mlu_data():
  """Read image from file. Convert OpenCV mat to VideoFrame (convert color and copy data from cpu to mlu)"""
  # Prepare MLU data
  cv_image = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  img_width = cv_image.shape[1]
  img_height = cv_image.shape[0]
  # Create a video_frame
  video_frame = cnis.VideoFrame()
  video_frame.plane_num = 2
  video_frame.format = cnis.VideoPixelFmt.NV12
  video_frame.width = img_width
  video_frame.height = img_height
  video_frame.stride = [video_frame.width, video_frame.width]

  # Convert image from BGR24 TO YUV NV12
  i420_img = cv2.cvtColor(cv_image, cv2.COLOR_BGR2YUV_I420)
  i420_img = i420_img.reshape(int(img_width * img_height * 3 / 2))
  img_y = i420_img[:img_width*img_height]
  img_uv = i420_img[img_width*img_height:]
  img_uv.reshape((int(img_width * img_height / 4), 2), order="F").reshape(int(img_width * img_height /2))

  # Create mlu buffer
  mlu_buffer_y = cnis.Buffer(img_width * img_height, 0)
  mlu_buffer_uv = cnis.Buffer(int(img_width * img_height / 2), 0)
  # Copy to mlu buffer
  mlu_buffer_y.copy_from(img_y)
  mlu_buffer_uv.copy_from(img_uv)
  # Set buffer to video_frame
  video_frame.set_plane(0, mlu_buffer_y)
  video_frame.set_plane(1, mlu_buffer_uv)

  # Create package and set video_frame to it
  input_pak = cnis.Package(1, tag)
  input_pak.data[0].set(video_frame)
  # Set user data to input data, as PostprocYolov3 need to known the image width and height
  input_pak.data[0].set_user_data({"image_width": img_width, "image_height": img_height})
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


class MyObserver(cnis.Observer):
  """To receive results from InferServer, we define a class MyObserver which inherits from cnis.Observer.
  After a request is sent to InferServer and is processed by InferServer, the response_func API will be called with
  status, results and user data.
  """
  def __init__(self):
    super().__init__()
  def response_func(self, status, data, user_data):
      if status == cnis.Status.SUCCESS:
         print_result(data)
      else:
        print("MyObserver.response status: ", status)


def async_demo(platform):
  """Asynchronous demo API
  1. Create InferServer and Session
  2. Sent frames to InferServer
  3. Print results.
  4. Wait all task finished
  5. Destroy session
  """
  # Create InferServer
  infer_server = cnis.InferServer(dev_id=0)

  # Create session. Sync API
  session_desc = cnis.SessionDesc()
  session_desc.name = "test_session_async"
  session_desc.engine_num = 1
  session_desc.strategy = cnis.BatchStrategy.DYNAMIC

  # Load model
  if platform in ["mlu220", "220", "MLU220"]:
    session_desc.model = infer_server.load_model(yolov3_mlu220_model_dir)
  else:
    session_desc.model = infer_server.load_model(yolov3_mlu270_model_dir)

  # Set preproc and postproc
  cncv_preproc(session_desc)
  cpp_postproc(session_desc)

  # Create observer
  obs = MyObserver()

  # Create session
  session = infer_server.create_session(session_desc, obs)

  for _ in range(4):
    input_pak = prepare_mlu_data()
    user_data = {"user_data" : "this is user data"}

    # Request
    ret = infer_server.request(session, input_pak, user_data, timeout=20000)
    if not ret:
      print("RequestAsync failed, ret: {}".format(ret))

  # Wait all tasks done
  infer_server.wait_task_done(session, tag)
  # Destroy Session
  infer_server.destroy_session(session)


def main():
  """Parser arguments and run the asynchronous inference demo. Set argument -p to choose platform"""
  parser = argparse.ArgumentParser()
  parser.add_argument("-p", dest="platform", required=False, default = "mlu270",
                      help="The platform, choose from mlu270 or mlu220")

  args = parser.parse_args()

  start = time.time()
  async_demo(args.platform)
  dur = time.time() - start
  print("\n## Async demo total time : {:.4f} second(s)\n".format(dur))


if __name__ == '__main__':
  main()
