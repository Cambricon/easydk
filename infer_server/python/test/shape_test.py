import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
from cnis import *
import numpy as np

class TestShape:
  def test_shape(self):
    empty_shape = Shape()
    assert(empty_shape)
    assert(empty_shape.size() == 0)
    assert(empty_shape.empty())

    shape_list = [16, 1, 1, 100]
    shape = Shape(shape_list)
    for i in range (len(shape_list)):
      assert(shape[i] == shape_list[i])
    assert(shape.vectorize() == shape_list)
    assert(shape.size() == 4)
    assert(not shape.empty())
    assert(shape.batch_size() == shape_list[0])
    assert(shape.data_count() == (np.prod(shape_list) // shape_list[0]))
    assert(shape.batch_data_count() == np.prod(shape_list))
    # modify
    shape[0] = 100
    assert(shape.batch_size() == 100)
    shape_list[0] = 100
    assert(shape.vectorize() == shape_list)
    assert(shape != empty_shape)
    shape2 = Shape(shape_list)
    assert(shape == shape2)
