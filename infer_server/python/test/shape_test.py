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
