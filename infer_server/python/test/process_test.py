"""Processor test

This module tests Preprocessor, Postprocessor and ModelIO related APIs

"""

import os, sys
import numpy as np

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis
import cnis_cpptest

tag = "stream_0"
ssd_mlu270_model_dir = \
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

def request_package(session_desc, infer_server, session):
  """Create input package and send it to InferServer."""
  # Create a package with random data
  input_pak = cnis.Package(1, tag)
  input_shape = session_desc.model.input_shape(0)
  data_size = [input_shape[1], input_shape[2], input_shape[3]]
  if session_desc.model.input_layout(0).order == cnis.DimOrder.NCHW:
    data_size = [input_shape[2], input_shape[3], input_shape[1]]
  input_data = np.random.randint(0, 255, size=data_size, dtype=np.dtype(np.uint8))
  input_pak.data[0].set(input_data)
  output = cnis.Package(1, tag)
  status = cnis.Status.SUCCESS
  # Request
  assert infer_server.request_sync(session, input_pak, status, output)
  assert status == cnis.Status.SUCCESS

class CustomPreprocess(cnis.Preprocess):
  """To use custom preprocess, we define a class CustomPreprocess which inherits from cnis.Preprocess.
  The execute_func API will be called by InferServer to do preprocess.
  """
  def __init__(self):
    super().__init__()
    self.called = False

  def execute_func(self, model_input, input_data, model):
    # doing preprocessing.
    self.called = True
    return True

class CustomPostprocess(cnis.Postprocess):
  """To use custom postprocess, we define a class CustomPostprocess which inherits from cnis.Postprocess.
  The execute_func API will be called by InferServer to do postprocess.
  """
  def __init__(self):
    super().__init__()
    self.called = False

  def execute_func(self, result, model_output, model):
    # doing postprocessing.
    self.called = True
    return True

class TestPreprocess(object):
  """TestPreprocess class provides several APIs for testing Preprocessor"""
  @staticmethod
  def test_python_preprocess():
    """Test custom preprocess function (written in python)"""
    # Create InferServer
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    # Create PreprocessorHost and set custom preprocess function to description
    session_desc.preproc = cnis.PreprocessorHost()
    custom_python_preproc = CustomPreprocess()
    session_desc.set_preproc_func(custom_python_preproc.execute)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    # Create a package
    input_pak = cnis.Package(1, tag)
    output = cnis.Package(1, tag)
    status = cnis.Status.SUCCESS
    # Request
    assert infer_server.request_sync(session, input_pak, status, output)
    assert status == cnis.Status.SUCCESS
    assert custom_python_preproc.called

    # Destroy session
    assert infer_server.destroy_session(session)

  @staticmethod
  def test_cpp_preprocess():
    """Test custom preprocess function (written in c++)"""
    # Create InferServer
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    # Create PreprocessorHost and set custom preprocess function to description
    session_desc.preproc = cnis.PreprocessorHost()
    custom_cpp_preproc = cnis_cpptest.PreprocTest()
    session_desc.set_preproc_func(custom_cpp_preproc.execute)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    # Create a package
    input_pak = cnis.Package(1, tag)
    output = cnis.Package(1, tag)
    status = cnis.Status.SUCCESS
    # Request
    assert infer_server.request_sync(session, input_pak, status, output)
    assert status == cnis.Status.SUCCESS

    # Destroy session
    assert infer_server.destroy_session(session)


class TestPostprocess(object):
  """TestPostprocess class provides several APIs for testing Postprocessor"""
  @staticmethod
  def test_python_postprocess():
    """Test custom postprocess function (written in python)"""
    # Create InferServer
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    # Create Postprocessor and set custom postprocess function to description
    session_desc.postproc = cnis.Postprocessor()
    custom_python_postproc = CustomPostprocess()
    session_desc.set_postproc_func(custom_python_postproc.execute)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    request_package(session_desc, infer_server, session)

    # Check if custom postprocess function is called
    assert custom_python_postproc.called

    # Destroy session
    assert infer_server.destroy_session(session)

  @staticmethod
  def test_cpp_postprocess():
    """Test custom postprocess function (written in c++)"""
    # Create InferServer
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    # Create Postprocessor and set custom postprocess function to description
    session_desc.postproc = cnis.Postprocessor()
    custom_cpp_postproc = cnis_cpptest.PostprocTest()
    session_desc.set_postproc_func(custom_cpp_postproc.execute)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    request_package(session_desc, infer_server, session)

    # Destroy session
    assert infer_server.destroy_session(session)


class TestModelIO(object):
  """TestModelIO class provides several APIs for testing ModelIO"""
  @staticmethod
  def test_model_io():
    # Create ModelIO
    model_io = cnis.ModelIO()
    assert len(model_io.buffers) == 0
    assert len(model_io.shapes) == 0
    data_size = 100 * 100
    dtype = np.dtype(np.uint8)
    buffers = [cnis.Buffer(size=data_size)] * 4
    src_data = np.random.randint(0, 255, size=data_size, dtype=dtype)
    for buffer in buffers:
      buffer.copy_from(src_data)
    # Set buffers and shapes to ModelIO
    model_io.buffers = buffers
    shapes = [cnis.Shape([1, 1, 100, 100])] * 4
    model_io.shapes = shapes
    assert len(model_io.buffers) == 4
    assert len(model_io.shapes) == 4
    for buffer in model_io.buffers:
      dst_data = np.random.randint(0, 255, size=data_size, dtype=dtype)
      buffer.copy_to(dst_data)
      assert dst_data.any() == src_data.any()
    for i, shape in enumerate(model_io.shapes):
      assert shape == shapes[i]
