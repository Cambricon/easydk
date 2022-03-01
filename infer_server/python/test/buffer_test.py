"""Buffer test

This module tests Buffer related APIs

"""

import os, sys
import numpy as np

sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis

class TestBuffer(object):
  """TestBuffer class provides several APIs for testing Buffer"""
  @staticmethod
  def test_buffer():
    """Test cpu buffer and mlu buffer"""
    buf_size = 4
    # cpu memory
    cpu_buf = cnis.Buffer(size=buf_size)
    assert cpu_buf.memory_size() == buf_size
    assert cpu_buf.dev_id() < 0
    assert cpu_buf.type() == cnis.MemoryType.CPU
    assert not cpu_buf.on_mlu()
    assert not cpu_buf.own_memory()
    # malloc when get data
    cpu_buf.data()
    assert cpu_buf.own_memory()
    # mlu memory
    mlu_buf = cnis.Buffer(size=buf_size, dev_id=0)
    assert mlu_buf.memory_size() == buf_size
    assert mlu_buf.dev_id() == 0
    assert mlu_buf.type() == cnis.MemoryType.MLU
    assert mlu_buf.on_mlu()
    assert not mlu_buf.own_memory()
    # malloc when get data
    mlu_buf.data()
    assert mlu_buf.own_memory()


  @staticmethod
  def test_cpu_buffer_copy_from():
    """Test copy an array or a buffer to a cpu buffer"""
    buf_size = 4
    buf = cnis.Buffer(size=buf_size)
    # copy from array
    dtype = np.dtype(np.uint8)
    src_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    buf.copy_from(src_data)
    dst_data = buf.data(dtype=dtype)
    assert dst_data.any() == src_data.any()

    # modify data
    dst_data[0] = 0
    assert buf.data(dtype=dtype).any() == dst_data.any()

    # get reshaped data
    dst_data = buf.data(shape=[2, buf_size // 2], dtype=dtype)
    assert dst_data.shape == (2, buf_size // 2)
    assert buf.data(dtype=dtype).any() == dst_data.any()

    # copy from buffer
    dst_buf = cnis.Buffer(buf_size)
    dst_buf.copy_from(src=buf)
    assert buf.data(dtype=dtype).any() == dst_buf.data(dtype=dtype).any()

  @staticmethod
  def test_cpu_buffer_copy_to():
    """Test copy cpu buffer to an array or a buffer"""
    buf_size = 4
    buf = cnis.Buffer(size=buf_size)
    dtype = np.dtype(np.uint8)
    src_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    buf.copy_from(src_data)

    # copy to array
    dst_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    buf.copy_to(dst=dst_data)
    assert src_data.any() == dst_data.any()

    # copy to buffer
    dst_buf = cnis.Buffer(buf_size)
    buf.copy_to(dst=dst_buf)
    assert buf.data().any() == dst_buf.data().any()

  @staticmethod
  def test_mlu_buffer_copy():
    """Test copy an array or a buffer to a mlu buffer
       Test copy a mlu buffer to a buffer
    """
    buf_size = 4
    buf = cnis.Buffer(size=buf_size, dev_id=0)
    # copy from array
    dtype = np.dtype(np.uint8)
    src_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    buf.copy_from(src_data)
    dst_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    buf.copy_to(dst=dst_data)
    assert dst_data.any() == src_data.any()

    # copy from buffer
    dst_buf = cnis.Buffer(buf_size)
    dst_buf.copy_from(src=buf)
    dst_buf_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    dst_buf.copy_to(dst=dst_buf_data)
    assert src_data.any() == dst_buf_data.any()

    # copy to buffer
    buf.copy_to(dst=dst_buf)
    dst_buf.copy_to(dst=dst_buf_data)
    assert dst_data.any() == dst_buf_data.any()
