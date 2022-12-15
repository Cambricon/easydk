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

"""Shape test

This module tests Shape related APIs

"""

import os, sys
import numpy as np

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis

class TestShape(object):
  """TestShape class provides several APIs for testing Shape"""
  @staticmethod
  def test_shape():
    # Create an empty shape
    empty_shape = cnis.Shape()
    assert empty_shape
    assert empty_shape.size() == 0
    assert empty_shape.empty()

    # Create a Shape with [16, 1, 1, 100]
    shape_list = [16, 1, 1, 100]
    shape = cnis.Shape(shape_list)
    for i in range (len(shape_list)):
      assert shape[i] == shape_list[i]
    assert shape.vectorize() == shape_list
    assert shape.size() == 4
    assert not shape.empty()
    assert shape.batch_size() == shape_list[0]
    assert shape.data_count() == (np.prod(shape_list) // shape_list[0])
    assert shape.batch_data_count() == np.prod(shape_list)
    # modify the shape
    shape[0] = 100
    assert shape.batch_size() == 100
    shape_list[0] = 100
    assert shape.vectorize() == shape_list
    # Check != operator
    assert shape != empty_shape
    shape2 = cnis.Shape(shape_list)
    # Check == operator
    assert shape == shape2
