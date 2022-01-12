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


def get_model_input_wh(model):
  width = 0
  height = 0
  order = model.input_layout(0).order
  if order == DimOrder.NHWC:
    width = model.input_shape(0)[2]
    height = model.input_shape(0)[1]
  elif order == DimOrder.NCHW:
    width = model.input_shape(0)[1]
    height =model.input_shape(0)[0]
  else:
    print("unsupported dim order")
  return width, height


def prepare_input_and_preproc(width, height):
  img = cv2.imread(cur_file_dir + "/../test/data/test.jpg")
  resized_img = cv2.resize(img, (width, height))
  bgra_img = cv2.cvtColor(resized_img, cv2.COLOR_BGR2BGRA)

  input_pak = Package(1, tag)
  input_pak.data[0].set(bgra_img)
  return input_pak


def ssd_postproc_and_print_result(output_pak, model, threshold):
  def clip(x):
    return max(0, min(1, x))
  for data in output_pak.data:
    model_io = data.get_model_io()
    buffer = model_io.buffers[0]
    shape = model_io.shapes[0]
    layout = model.output_layout(0)
    data = buffer.data(shape, layout)
    # Postproc ssd
    data = data.reshape(model.output_shape(0)[3])
    box_num = int(data[0])
    objs = []
    for i in range(box_num):
      obj = DetectObject()
      if data[64 + i * 7 + 1] == 0:
        continue
      obj.label = int(data[64 + i * 7 + 1] - 1)
      obj.score = data[64 + i * 7 + 2]
      if threshold > 0 and obj.score < threshold:
        continue
      obj.bbox.x = clip(data[64 + i * 7 + 3])
      obj.bbox.y = clip(data[64 + i * 7 + 4])
      obj.bbox.w = clip(data[64 + i * 7 + 5]) - obj.bbox.x
      obj.bbox.h = clip(data[64 + i * 7 + 6]) - obj.bbox.y
      objs.append(obj)

    # Print results
    if len(objs) == 0:
      print("@@@@@@@@@@@ No objects detected in frame ")
    print("objects number: ", len(objs))
    for obj in objs:
      print("obj label: {}  score: {}  bbox : {}, {}, {}, {}".format(obj.label, obj.score, obj.bbox.x, obj.bbox.y, obj.bbox.w, obj.bbox.h))


def sync_default_preproc_postproc_demo(platform):
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
  session = infer_server.create_sync_session(session_desc)

  width, height = get_model_input_wh(session_desc.model)

  for i in range(4):
    # Prepare input and output
    input_pak = prepare_input_and_preproc(width, height)
    output_pak = Package(1)

    # Request
    status = Status.SUCCESS
    ret = infer_server.request_sync(session, input_pak, status, output_pak, timeout=20000)
    if not ret:
      print("RequestSync failed, ret: {}, status: {}".format(ret, status))

    # Postproc and print results
    if status == Status.SUCCESS:
      ssd_postproc_and_print_result(output_pak, session_desc.model, 0.6)

  # Destroy Session
  infer_server.destroy_session(session)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("-p", dest="platform", required=False, default = "mlu270",
                      help="The platform, choose from mlu270 or mlu220")

  args = parser.parse_args()

  # Infer server will do inference only, users should do preproc and postproc before and after request
  start = time.time()
  sync_default_preproc_postproc_demo(args.platform)
  dur = time.time() - start
  print("\n## Sync demo (default preproc/postproc) total time : {:.4f} second(s)\n".format(dur))


if __name__ == '__main__':
  main()
