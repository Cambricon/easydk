"""InferServer test

This module tests InferServer, Package, DataLayout and Device related APIs

"""
import os, sys
import cv2
import numpy as np

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis

tag = "stream_0"
ssd_mlu270_model_dir = \
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

def get_model_input_wh(model):
  """Get the input width and height of the model"""
  width = 0
  height = 0
  order = model.input_layout(0).order
  # Get height and width according to the dim order and the shape.
  if order == cnis.DimOrder.NHWC:
    width = model.input_shape(0)[2]
    height = model.input_shape(0)[1]
  elif order == cnis.DimOrder.NCHW:
    width = model.input_shape(0)[1]
    height = model.input_shape(0)[0]
  else:
    print("unsupported dim order")
  return width, height

def prepare_input(model):
  """Read image from file. Set OpenCV mat to input package."""
  img = cv2.imread(cur_file_dir + "/data/test.jpg")
  # resize to model input shape
  resized_img = cv2.resize(img, (get_model_input_wh(model)))
  # convert color to model input pixel format
  bgra_img = cv2.cvtColor(resized_img, cv2.COLOR_BGR2BGRA)
  # Create input package and set OpenCV mat to input package
  input_pak = cnis.Package(1, tag)
  input_pak.data[0].set(bgra_img)
  return input_pak

class TestInferServer(object):
  """TestInferServer class provides several APIs for testing InferServer"""
  @staticmethod
  def test_create_session():
    # First of all we need to create an InferServer object
    infer_server = cnis.InferServer(dev_id=0)
    # Secondly, to create an asynchronous session, a session description is needed.
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model and set it to session description
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
    session_desc.show_perf = False
    # Define a TestObserver class for receiving results. Create a TestObserver object and set it to description.
    class TestObserver(cnis.Observer):
      """To receive results from InferServer, we define a class TestObserver which inherits from cnis.Observer.
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
    obs = TestObserver()
    # create an asynchronous session
    session = infer_server.create_session(session_desc, obs)
    assert session

    # Test request
    input_pak = prepare_input(session_desc.model)
    assert infer_server.request(session, input_pak, {"user_data":"cnis"})
    infer_server.wait_task_done(session, tag)
    assert obs.called

    # Test discard task
    for _ in range(10):
      input_pak = prepare_input(session_desc.model)
      assert infer_server.request(session, input_pak, {"user_data":"cnis"})
    infer_server.discard_task(session, tag)
    infer_server.wait_task_done(session, tag)

    # destroy session
    assert infer_server.destroy_session(session)

  @staticmethod
  def test_create_sync_session():
    # First of all we need to create an InferServer object
    infer_server = cnis.InferServer(dev_id=0)
    # Secondly, to create a synchronous session, a session description is needed.
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model and set it to session description
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
    session_desc.show_perf = False
    # create a synchronous session
    session = infer_server.create_sync_session(session_desc)
    assert session

    # Test request sync
    input_pak = prepare_input(session_desc.model)
    output = cnis.Package(1, tag)
    status = cnis.Status.SUCCESS
    assert infer_server.request_sync(session, input_pak, status, output)
    assert status == cnis.Status.SUCCESS
    assert output.perf
    assert len(output.data) == 1 and output.data[0].get_model_io()

    # Test get model and unload model
    assert infer_server.get_model(session) == session_desc.model
    assert cnis.InferServer.unload_model(session_desc.model)
    assert not cnis.InferServer.unload_model(session_desc.model)
    model = infer_server.load_model(ssd_mlu270_model_dir)
    cnis.InferServer.clear_model_cache()
    assert not cnis.InferServer.unload_model(model)

    # destroy session
    assert infer_server.destroy_session(session)


class TestPackage(object):
  """TestPackage class provides several APIs for testing Package"""
  @staticmethod
  def test_package():
    data_num = 4
    # Create a Package with 4 data
    input_pak = cnis.Package(data_num, tag)
    assert len(input_pak.data) == data_num
    assert input_pak.tag == tag

  @staticmethod
  def test_infer_data():
    # Create an InferData object
    infer_data = cnis.InferData()
    assert not infer_data.has_value()
    # Set a dictionary to the InferData object
    dict_data = {"key1" : "val1", "key2" : "val2"}
    infer_data.set(dict_data)
    assert infer_data.get_dict() == dict_data
    # Set a VideoFrame to the InferData object
    video_frame = cnis.VideoFrame()
    video_frame.width = 1920
    video_frame.height = 1080
    infer_data.set(video_frame)
    assert infer_data.get_video_frame().width == video_frame.width
    assert infer_data.get_video_frame().height == video_frame.height
    # Set a OpencvFrame to the InferData object
    cv_frame = cnis.OpencvFrame()
    cv_frame.fmt = cnis.VideoPixelFmt.BGR24
    infer_data.set(cv_frame)
    assert infer_data.get_cv_frame().fmt == cv_frame.fmt
    # Set an array to the InferData object
    array = np.zeros(100)
    infer_data.set(array)
    assert infer_data.get_array().any() == array.any()
    # Set another array to the InferData object
    array = np.random.rand(300, 200)
    infer_data.set(array)
    assert infer_data.get_array().any() == array.any()
    assert infer_data.has_value()

    # Set user data to the InferData object
    user_data = {"key1" : "val1", "key2" : "val2"}
    infer_data.set_user_data(user_data)
    assert infer_data.get_user_data() == user_data


class TestDataLayout(object):
  """TestDataLayout class provides several APIs for testing DataLayout"""
  @staticmethod
  def test_data_layout():
    infer_server =cnis.InferServer(dev_id=0)
    # Load model
    model = infer_server.load_model(ssd_mlu270_model_dir)
    # Check model input and output data type and dim order
    assert model.input_layout(0).dtype == cnis.DataType.UINT8
    assert model.input_layout(0).order == cnis.DimOrder.NHWC
    assert model.output_layout(0).dtype == cnis.DataType.FLOAT16
    assert model.output_layout(0).order == cnis.DimOrder.NHWC

    # Check data type size is correct
    assert cnis.get_type_size(cnis.DataType.UINT8) == 1
    assert cnis.get_type_size(cnis.DataType.FLOAT32) == 4
    assert cnis.get_type_size(cnis.DataType.FLOAT16) == 2
    assert cnis.get_type_size(cnis.DataType.INT16) == 2
    assert cnis.get_type_size(cnis.DataType.INT32) == 4


class TestDevice(object):
  """TestDevice class provides several APIs for testing Device"""
  @staticmethod
  def test_device():
    # Assume there is at least one device
    assert cnis.total_device_count()
    assert cnis.check_device(0)
    assert cnis.set_current_device(0)
