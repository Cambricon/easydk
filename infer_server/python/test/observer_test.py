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

"""Observer test

This module tests Observer related APIs

"""

import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import numpy as np

import cnis
import utils

class TestObserver(object):
  """TestObserver class provides several APIs for testing Observer"""
  @staticmethod
  def test_observer():
    infer_server = cnis.InferServer(dev_id=0)
    session_desc = cnis.SessionDesc()
    session_desc.model = infer_server.load_model(utils.model_dir)
    session_desc.show_perf = False

    class TestPostproc(cnis.Postprocess):
      """To use custom postprocess, we define a class TestPostproc which inherits from cnis.Postprocess.
      The execute_func API will be called by InferServer to do postprocess.
      """
      def __init__(self):
        super().__init__()
      def execute_func(self, result, model_output, model_info):
        result.set({"key1": "result1", "key2": "result2"})
        return True

    session_desc.postproc = cnis.Postprocessor()
    session_desc.set_postproc_func(TestPostproc().execute)

    class TestObserver(cnis.Observer):
      """To receive results from InferServer, we define a class TestObserver which inherits from cnis.Observer.
      After a request is sent to InferServer and is processed by InferServer, the response_func API will be called with
      status, results and user data.
      """
      def __init__(self):
        super().__init__()
        self.called = False
      def response_func(self, status, data, user_data):
        # Check user data
        assert "user_data" in user_data
        assert user_data["user_data"] == "cnis"
        assert len(data.data) == 1
        # Check data
        result = data.data[0].get_dict()
        assert "key1" in result
        assert "key2" in result
        assert result["key1"] == "result1"
        assert result["key2"] == "result2"
        self.called = True

    obs = TestObserver()
    session = infer_server.create_session(session_desc, obs)
    input_pak = cnis.Package(1, utils.tag)
    if cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU270 or \
       cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU220:
      input_pak.data[0].set(np.random.randint(0, 255, size=(300, 300, 4), dtype=np.uint8))
    else:
      input_pak.data[0].set(np.random.randint(0, 255, size=(416, 416, 3), dtype=np.uint8))
    infer_server.request(session, input_pak, {"user_data":"cnis"})
    infer_server.wait_task_done(session, utils.tag)
    assert obs.called
    infer_server.destroy_session(session)
