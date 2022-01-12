import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *
from cnis_cpptest import *
import numpy as np

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]

tag = "stream_0"
ssd_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

class CustomPreprocess(Preprocess):
  def __init__(self):
    super().__init__()
    self.called = False

  def execute_func(self, model_input, input_data, model):
    # doing preprocessing.
    self.called = True
    return True

class CustomPostprocess(Postprocess):
  def __init__(self):
    super().__init__()
    self.called = False

  def execute_func(self, result, model_output, model):
    # doing postprocessing.
    self.called = True
    return True

class TestPreprocess:
  def test_python_preprocess(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.preproc = PreprocessorHost()
    custom_python_preproc = CustomPreprocess()
    session_desc.set_preproc_func(custom_python_preproc.execute)
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)

    input_pak = Package(1, tag)
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)
    assert(custom_python_preproc.called)

    assert(infer_server.destroy_session(session))

  def test_cpp_preprocess(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.preproc = PreprocessorHost()
    custom_cpp_preproc = PreprocTest()
    session_desc.set_preproc_func(custom_cpp_preproc.execute)
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)

    input_pak = Package(1, tag)
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)

    assert(infer_server.destroy_session(session))


class TestPostprocess:
  def test_python_postprocess(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.postproc = Postprocessor()
    custom_python_postproc = CustomPostprocess()
    session_desc.set_postproc_func(custom_python_postproc.execute)
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)

    input_pak = Package(1, tag)
    input_shape = session_desc.model.input_shape(0)
    data_size = [input_shape[1], input_shape[2], input_shape[3]]
    if session_desc.model.input_layout(0).order == DimOrder.NCHW:
      data_size = [input_shape[2], input_shape[3], input_shape[1]]
    input_data = np.random.randint(0, 255, size=data_size, dtype=np.dtype(np.uint8))
    input_pak.data[0].set(input_data)
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)
    assert(custom_python_postproc.called)

    assert(infer_server.destroy_session(session))

  def test_cpp_postprocess(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)

    session_desc.postproc = Postprocessor()
    custom_cpp_postproc = PostprocTest()
    session_desc.set_postproc_func(custom_cpp_postproc.execute)
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)

    input_pak = Package(1, tag)
    input_shape = session_desc.model.input_shape(0)
    data_size = [input_shape[1], input_shape[2], input_shape[3]]
    if session_desc.model.input_layout(0).order == DimOrder.NCHW:
      data_size = [input_shape[2], input_shape[3], input_shape[1]]
    input_data = np.random.randint(0, 255, size=data_size, dtype=np.dtype(np.uint8))
    input_pak.data[0].set(input_data)
    output = Package(1, tag)
    status = Status.SUCCESS
    assert(infer_server.request_sync(session, input_pak, status, output))
    assert(status == Status.SUCCESS)

    assert(infer_server.destroy_session(session))


class TestModelIO:
  def test_model_io(self):
    model_io = ModelIO()
    assert(len(model_io.buffers) == 0)
    assert(len(model_io.shapes) == 0)
    data_size = 100 * 100
    dtype = np.dtype(np.uint8)
    buffers = [Buffer(size=data_size)] * 4
    src_data = np.random.randint(0, 255, size=data_size, dtype=dtype)
    for buffer in buffers:
      buffer.copy_from(src_data)
    model_io.buffers = buffers
    shapes = [Shape([1, 1, 100, 100])] * 4
    model_io.shapes = shapes
    assert(len(model_io.buffers) == 4)
    assert(len(model_io.shapes) == 4)
    for buffer in model_io.buffers:
      dst_data = np.random.randint(0, 255, size=data_size, dtype=dtype)
      buffer.copy_to(dst_data)
      assert(dst_data.any() == src_data.any())
    for i, shape in enumerate(model_io.shapes):
      assert(shape == shapes[i])
