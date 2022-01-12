import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *
import numpy as np

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
ssd_mlu270_model_dir = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

class CustomPreprocess(Preprocess):
  def __init__(self):
    super().__init__()

  def execute_func(self, model_input, input_data, model):
    # doing preprocessing.
    return True

class CustomPostprocess(Postprocess):
  def __init__(self):
    super().__init__()

  def execute_func(self, result, model_output, model):
    # doing postprocessing.
    return True

class TestSession:
  def test_session(self):
    infer_server = InferServer(dev_id=0)
    session_desc = SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
    session_desc.strategy = BatchStrategy.DYNAMIC
    session_desc.preproc = PreprocessorHost()
    session_desc.set_preproc_func(CustomPreprocess().execute)
    session_desc.postproc = Postprocessor()
    session_desc.set_postproc_func(CustomPostprocess().execute)
    session_desc.host_input_layout = DataLayout(DataType.UINT8, DimOrder.NHWC)
    session_desc.host_output_layout = DataLayout(DataType.FLOAT32, DimOrder.NHWC)
    session_desc.batch_timeout = 1000
    session_desc.priority = 0
    session_desc.engine_num = 2
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)
    assert(session)
    assert(infer_server.destroy_session(session))

    class TestObserver(Observer):
      def __init(self):
        super().__init__()
      def response_func(self, status, data, user_data):
        pass
    obs = TestObserver()
    session = infer_server.create_session(session_desc, obs)
    assert(session)
    assert(infer_server.destroy_session(session))
