import os, sys, time
import argparse
import numpy as np
import cv2

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
tag = "stream_0"
yolov3_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/yolov3_b4c4_argb_mlu270.cambricon"
yolov3_mlu220_model_dir = "http://video.cambricon.com/models/MLU220/yolov3_b4c4_argb_mlu220.cambricon"


def cncv_preproc(session_desc):
  session_desc.preproc = VideoPreprocessorMLU()
  session_desc.set_preproc_params(VideoPixelFmt.ARGB, VideoPreprocessType.CNCV_PREPROC, keep_aspect_ratio=True)


def cpp_postproc(session_desc):
  session_desc.postproc = Postprocessor()
  session_desc.set_postproc_func(PostprocYolov3(0.5).execute)


def prepare_mlu_data():
  # Prepare MLU data
  img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  w = img.shape[1]
  h = img.shape[0]
  # Create a video_frame
  video_frame = VideoFrame()
  video_frame.plane_num = 2
  video_frame.format = VideoPixelFmt.NV12
  video_frame.width = w
  video_frame.height = h
  video_frame.stride = [video_frame.width, video_frame.width]

  # Convert image from BGR24 TO YUV NV12
  i420_img = cv2.cvtColor(img, cv2.COLOR_BGR2YUV_I420)
  i420_img = i420_img.reshape(int(w * h * 3 / 2))
  img_y = i420_img[:w*h]
  img_uv = i420_img[w*h:]
  img_uv.reshape((int(w * h / 4), 2), order="F").reshape(int(w * h /2))

  # Create mlu buffer
  mlu_buffer_y = Buffer(w * h, 0)
  mlu_buffer_uv = Buffer(int(w * h / 2), 0)
  # Copy to mlu buffer
  mlu_buffer_y.copy_from(img_y)
  mlu_buffer_uv.copy_from(img_uv)
  # Set buffer to video_frame
  video_frame.set_plane(0, mlu_buffer_y)
  video_frame.set_plane(1, mlu_buffer_uv)

  # Create package and set video_frame to it
  input_pak = Package(1, tag)
  input_pak.data[0].set(video_frame)
  # Set user data to input data, as PostprocYolov3 need to known the image width and height
  input_pak.data[0].set_user_data({"image_width": w, "image_height": h})
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


class MyObserver(Observer):
  def __init(self):
    super().__init__()
  def response_func(self, status, data, user_data):
      if status == Status.SUCCESS:
         print_result(data)
      else:
        print("MyObserver.response status: ", status)


def async_demo(platform):
  # Create InferServer
  infer_server = InferServer(dev_id=0)

  # Create session. Sync API
  session_desc = SessionDesc()
  session_desc.name = "test_session_async"
  session_desc.engine_num = 1
  session_desc.strategy = BatchStrategy.DYNAMIC

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

  for i in range(4):
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
