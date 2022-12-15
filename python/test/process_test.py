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

"""Processor test

This module tests Preprocessor, Postprocessor and ModelIO related APIs

"""

import os, sys
import numpy as np

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis
import cnis_cpptest
import utils

def request_package(session_desc, infer_server, session):
  """Create input package and send it to InferServer."""
  input_pak = utils.prepare_model_input(session_desc.model)
  output = cnis.Package(1, utils.tag)
  status = cnis.Status.SUCCESS
  # Request
  assert infer_server.request_sync(session, input_pak, status, output)
  assert status == cnis.Status.SUCCESS

class CustomIPreproc(cnis.IPreproc):
  """To use custom preprocess, we define a class CustomIPreproc which inherits from cnis.IPreproc.
  The on_preproc API will be called by InferServer to do preprocess.
  """
  def __init__(self):
    super().__init__()
    self.called = False

  def on_tensor_params(self, params):
    return 0

  def on_preproc(self, src, dst, src_rects):
    # doing preprocessing.
    self.called = True
    return 0

class CustomIPostproc(cnis.IPostproc):
  """To use custom postprocess, we define a class CustomIPostproc which inherits from cnis.IPostproc.
  The on_postproc API will be called by InferServer to do postprocess.
  """
  def __init__(self):
    super().__init__()
    self.called = False

  def on_postproc(self, data_vec, model_output, model_info):
    # doing postprocessing.
    self.called = True
    return 0

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
    session_desc.model = infer_server.load_model(utils.get_model_dir())
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB

    # Create Preprocessor and set custom preproc handler
    session_desc.preproc = cnis.Preprocessor()
    custom_python_preproc = CustomIPreproc()
    cnis.set_preproc_handler(session_desc.model.get_key(), custom_python_preproc)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    # Create a package
    input_pak = utils.prepare_model_input(session_desc.model)
    output = cnis.Package(1, utils.tag)
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
    session_desc.model = infer_server.load_model(utils.get_model_dir())
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB

    # Create Preprocessor and set custom preproc handler
    session_desc.preproc = cnis.Preprocessor()
    custom_cpp_preproc = cnis_cpptest.PreprocTest()
    cnis.set_preproc_handler(session_desc.model.get_key(), custom_cpp_preproc)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    # Create a package
    input_pak = utils.prepare_model_input(session_desc.model)
    output = cnis.Package(1, utils.tag)
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
    """Test custom postprocess handler (written in python)"""
    # Create InferServer
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.name = "test_session"
    # Load model
    session_desc.model = infer_server.load_model(utils.get_model_dir())
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB

    # Create Postprocessor and set custom postproc handler
    session_desc.postproc = cnis.Postprocessor()
    custom_python_postproc = CustomIPostproc()
    cnis.set_postproc_handler(session_desc.model.get_key(), custom_python_postproc)
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
    session_desc.model = infer_server.load_model(utils.get_model_dir())
    session_desc.model_input_format = cnis.NetworkInputFormat.RGB

    # Create Postprocessor and set custom postproc handler
    session_desc.postproc = cnis.Postprocessor()
    custom_cpp_postproc = cnis_cpptest.PostprocTest()
    cnis.set_postproc_handler(session_desc.model.get_key(), custom_cpp_postproc)
    session_desc.show_perf = False

    # Create synchronous session
    session = infer_server.create_sync_session(session_desc)

    request_package(session_desc, infer_server, session)

    # Destroy session
    assert infer_server.destroy_session(session)
