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

"""Model test

This module tests Model related APIs

"""

import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis
import utils

class TestModel(object):
  """TestModel class provides several APIs for testing Model"""
  @staticmethod
  def test_model():
    infer_server = cnis.InferServer(dev_id=0)
    model = infer_server.load_model(utils.model_dir)
    if cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU270 or \
       cnis.get_device_core_version(dev_id=0) == cnis.CoreVersion.MLU220:
      assert model.input_layout(0)
      assert model.input_shape(0)
      assert model.output_layout(0)
      assert model.output_shape(0)
      assert model.input_num() == 1
      assert model.output_num() == 1
      assert model.batch_size() == 4
      assert model.get_key() == "./vgg16_ssd_b4c4_bgra_mlu270.cambricon_subnet0"
    else:
      assert model.input_layout(0)
      assert model.input_shape(0)
      assert model.output_layout(0)
      assert model.output_shape(0)
      assert model.output_layout(1)
      assert model.output_shape(1)
      assert model.input_num() == 1
      assert model.output_num() == 2
      assert model.batch_size() == 4
      assert model.get_key() == "./yolov3_nhwc_tfu_0.8.2_uint8_int8_fp16.model"
