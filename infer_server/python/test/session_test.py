"""Session test

This module tests Session related APIs

"""

import os, sys
import numpy as np

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis

ssd_mlu270_model_dir = \
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

class CustomPreprocess(cnis.Preprocess):
  """To use custom preprocess, we define a class CustomPreprocess which inherits from cnis.Preprocess.
  The execute_func API will be called by InferServer to do preprocess.
  """
  def __init__(self):
    super().__init__()

  def execute_func(self, model_input, input_data, model):
    # doing preprocessing.
    return True

class CustomPostprocess(cnis.Postprocess):
  """To use custom postprocess, we define a class CustomPostprocess which inherits from cnis.Postprocess.
  The execute_func API will be called by InferServer to do postprocess.
  """
  def __init__(self):
    super().__init__()

  def execute_func(self, result, model_output, model):
    # doing postprocessing.
    return True

class TestSession(object):
  """TestSession class provides several APIs for testing Session"""
  @staticmethod
  def test_session():
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    session_desc.model = infer_server.load_model(ssd_mlu270_model_dir)
    session_desc.strategy = cnis.BatchStrategy.DYNAMIC
    session_desc.preproc = cnis.PreprocessorHost()
    session_desc.set_preproc_func(CustomPreprocess().execute)
    session_desc.postproc = cnis.Postprocessor()
    session_desc.set_postproc_func(CustomPostprocess().execute)
    session_desc.host_input_layout = cnis.DataLayout(cnis.DataType.UINT8, cnis.DimOrder.NHWC)
    session_desc.host_output_layout = cnis.DataLayout(cnis.DataType.FLOAT32, cnis.DimOrder.NHWC)
    session_desc.batch_timeout = 1000
    session_desc.priority = 0
    session_desc.engine_num = 2
    session_desc.show_perf = False

    session = infer_server.create_sync_session(session_desc)
    assert session
    assert infer_server.destroy_session(session)

    class TestObserver(cnis.Observer):
      """To receive results from InferServer, we define a class TestObserver which inherits from cnis.Observer.
      After a request is sent to InferServer and is processed by InferServer, the response_func API will be called with
      status, results and user data.
      """
      def __init__(self):
        super().__init__()
      def response_func(self, status, data, user_data):
        # Empty, just for testing session creation
        pass
    obs = TestObserver()
    session = infer_server.create_session(session_desc, obs)
    assert session
    assert infer_server.destroy_session(session)
