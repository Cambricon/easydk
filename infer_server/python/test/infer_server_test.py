import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *
import cv2
import numpy as np

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]

tag = "stream_0"
ssd_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

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

def prepare_input(model):
  img = cv2.imread(cur_file_dir + "/data/test.jpg")
  resized_img = cv2.resize(img, (get_model_input_wh(model)))
  bgra_img = cv2.cvtColor(resized_img, cv2.COLOR_BGR2BGRA)
  input_pak = Package(1, tag)
  input_pak.data[0].set(bgra_img)
  return input_pak

class TestInferServer:
  def test_create_session(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
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
    assert(session)

    # Test request
    input_pak = prepare_input(session_desc.model)
    assert(infer_server.request(session, input_pak, {"user_data":"cnis"}))
    infer_server.wait_task_done(session, tag)
    assert(obs.called)

    # Test discard task
    for i in range(10):
      input_pak = prepare_input(session_desc.model)
      assert(infer_server.request(session, input_pak, {"user_data":"cnis"}))
    infer_server.discard_task(session, tag)
    infer_server.wait_task_done(session, tag)

    assert(infer_server.destroy_session(session))

  def test_create_sync_session(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
    session_desc.show_perf = False
    session = infer_server.create_sync_session(session_desc)
    assert(session)

    # Test request sync
    input_pak = prepare_input(session_desc.model)
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)
    assert(output.perf)
    assert(len(output.data) == 1 and output.data[0].get_model_io())

    # Test get model and unload model
    assert(infer_server.get_model(session) == session_desc.model)
    assert(InferServer.unload_model(session_desc.model))
    assert(not InferServer.unload_model(session_desc.model))
    model = infer_server.load_model(ssd_mlu270_model_dir)
    InferServer.clear_model_cache()
    assert(not InferServer.unload_model(model))

    assert(infer_server.destroy_session(session))


class TestPackage:
  def test_package(self):
    data_num = 4
    input_pak = Package(data_num, tag)
    assert(len(input_pak.data) == data_num)
    assert(input_pak.tag == tag)

  def test_infer_data(self):
    infer_data = InferData()
    assert(not infer_data.has_value())
    dict_data = {"key1" : "val1", "key2" : "val2"}
    infer_data.set(dict_data)
    assert(infer_data.get_dict() == dict_data)
    video_frame = VideoFrame()
    video_frame.width = 1920
    video_frame.height = 1080
    infer_data.set(video_frame)
    assert(infer_data.get_video_frame().width == video_frame.width)
    assert(infer_data.get_video_frame().height == video_frame.height)
    cv_frame = OpencvFrame()
    cv_frame.fmt = VideoPixelFmt.BGR24
    infer_data.set(cv_frame)
    assert(infer_data.get_cv_frame().fmt == cv_frame.fmt)
    array = np.zeros(100)
    infer_data.set(array)
    assert(infer_data.get_array().any() == array.any())
    array = np.random.rand(300, 200)
    infer_data.set(array)
    assert(infer_data.get_array().any() == array.any())
    assert(infer_data.has_value())

    user_data = {"key1" : "val1", "key2" : "val2"}
    infer_data.set_user_data(user_data)
    assert(infer_data.get_user_data() == user_data)


class TestDataLayout:
  def test_data_layout(self):
    infer_server = InferServer(dev_id=0)
    model = infer_server.load_model(ssd_mlu270_model_dir)
    assert(model.input_layout(0).dtype == DataType.UINT8)
    assert(model.input_layout(0).order == DimOrder.NHWC)
    assert(model.output_layout(0).dtype == DataType.FLOAT16)
    assert(model.output_layout(0).order == DimOrder.NHWC)

    assert(get_type_size(DataType.UINT8) == 1)
    assert(get_type_size(DataType.FLOAT32) == 4)
    assert(get_type_size(DataType.FLOAT16) == 2)
    assert(get_type_size(DataType.INT16) == 2)
    assert(get_type_size(DataType.INT32) == 4)


class TestDevice:
   def test_device(self):
     assert(total_device_count())
     assert(check_device(0))
     assert(set_current_device(0))
