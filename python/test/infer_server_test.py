# ==============================================================================
# Copyright (C) [2022] by Cambricon, Inc. All rights reserved
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# ==============================================================================

"""InferServer test

This module tests InferServer, Package, DataLayout and Device related APIs

"""
import os, sys
import cv2
import numpy as np

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis
import utils

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
    session_desc.model = infer_server.load_model(utils.get_model_dir())
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB
    session_desc.show_perf = False
    # Define a TestObserver class for receiving results. Create a TestObserver object and set it to description.
    class TestObserver(cnis.Observer):
      """To receive results from InferServer, we define a class TestObserver which inherits from cnis.Observer.
      After a request is sent to InferServer and is processed by InferServer, the response API will be called with
      status, results and user data.
      """
      def __init__(self):
        super().__init__()
        self.called = False
      def response(self, status, data, user_data):
        assert status == cnis.Status.SUCCESS
        assert "user_data" in user_data
        assert user_data["user_data"] == "cnis"
        self.called = True
    obs = TestObserver()
    # create an asynchronous session
    session = infer_server.create_session(session_desc, obs)
    assert session

    # Test request
    input_pak = utils.prepare_model_input(session_desc.model)
    assert infer_server.request(session, input_pak, {"user_data":"cnis"})
    infer_server.wait_task_done(session, utils.tag)
    assert obs.called

    # Test discard task
    for _ in range(10):
      input_pak = utils.prepare_model_input(session_desc.model)
      assert infer_server.request(session, input_pak, {"user_data":"cnis"})
    infer_server.discard_task(session, utils.tag)
    infer_server.wait_task_done(session, utils.tag)

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
    session_desc.model = infer_server.load_model(utils.get_model_dir())
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB

    session_desc.show_perf = False

    # create a synchronous session
    session = infer_server.create_sync_session(session_desc)
    assert session

    # Test request sync
    input_pak = utils.prepare_model_input(session_desc.model)
    output = cnis.Package(1, utils.tag)
    status = cnis.Status.SUCCESS
    assert infer_server.request_sync(session, input_pak, status, output)
    assert status == cnis.Status.SUCCESS
    assert output.perf
    assert len(output.data) == 1 and output.data[0].get_model_io()

    # Test get model and unload model
    assert infer_server.get_model(session) == session_desc.model
    assert cnis.InferServer.unload_model(session_desc.model)
    assert not cnis.InferServer.unload_model(session_desc.model)
    model = infer_server.load_model(utils.get_model_dir())
    cnis.InferServer.clear_model_cache()
    assert not cnis.InferServer.unload_model(model)

    # destroy session
    assert infer_server.destroy_session(session)

class TestDataLayout(object):
  """TestDataLayout class provides several APIs for testing DataLayout"""
  @staticmethod
  def test_data_layout():
    infer_server =cnis.InferServer(dev_id=0)
    # Load model
    model = infer_server.load_model(utils.get_model_dir())
    # Check model input and output data type and dim order
    assert model.input_layout(0).dtype == cnis.DataType.UINT8
    assert model.input_layout(0).order == cnis.DimOrder.NHWC
    assert model.output_layout(0).dtype == cnis.DataType.FLOAT32
    assert model.output_layout(0).order == cnis.DimOrder.NONE
    assert model.output_layout(1).dtype == cnis.DataType.INT32
    assert model.output_layout(1).order == cnis.DimOrder.NONE

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
