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

"""Buffer test

This module tests Buffer related APIs

"""

import os, sys
import cv2
import numpy as np
import random

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")

import cnis
import utils

class TestPackage(object):
  """TestPackage class provides several APIs for testing Package"""
  @staticmethod
  def test_package():
    data_num = 4
    # Create a Package with 4 data
    input_pak = cnis.Package(data_num, utils.tag)
    assert len(input_pak.data) == data_num
    assert input_pak.tag == utils.tag

  @staticmethod
  def test_infer_data():
    # Create an InferData object
    infer_data = cnis.InferData()
    assert not infer_data.has_value()
    # Set a dictionary to the InferData object
    dict_data = {"key1" : "val1", "key2" : "val2"}
    infer_data.set(dict_data)
    assert infer_data.get_dict() == dict_data
    # Set a VideoFrame to the InferData object
    preproc_input = cnis.PreprocInput()
    img = cv2.imread(cur_file_dir + "/../data/test.jpg")
    preproc_input.surf = cnis.convert_to_buf_surf(img, cnis.CnedkBufSurfaceColorFormat.BGR)
    preproc_input.has_bbox = True
    preproc_input.bbox = cnis.CNInferBoundingBox(random.random(), random.random(), random.random(), random.random())
    infer_data.set(preproc_input)
    assert infer_data.get_preproc_input().surf.get_width() == img.shape[1]
    assert infer_data.get_preproc_input().surf.get_height() == img.shape[0]
    assert infer_data.get_preproc_input().has_bbox == preproc_input.has_bbox
    assert infer_data.get_preproc_input().bbox.x == preproc_input.bbox.x
    assert infer_data.get_preproc_input().bbox.y == preproc_input.bbox.y
    assert infer_data.get_preproc_input().bbox.w == preproc_input.bbox.w
    assert infer_data.get_preproc_input().bbox.h == preproc_input.bbox.h
    # Set an array to the InferData object
    array = np.zeros(100)
    infer_data.set(array)
    assert infer_data.get_array().any() == array.any()
    # Set another array to the InferData object
    array = np.random.rand(300, 200)
    infer_data.set(array)
    assert infer_data.get_array().any() == array.any()
    assert infer_data.has_value()

    # Set user data to the InferData object
    user_data = {"key1" : "val1", "key2" : "val2"}
    infer_data.set_user_data(user_data)
    assert infer_data.get_user_data() == user_data

class TestData(object):
  """TestBuffer class provides several APIs for testing Buffer"""
  @staticmethod
  def test_buffer_surface():
    """Test cpu buffer and mlu buffer"""
    # cpu memory
    params = cnis.CnedkBufSurfaceCreateParams()
    params.mem_type = cnis.CnedkBufSurfaceMemType.SYSTEM
    params.width = 1920
    params.height = 1080
    params.color_format = cnis.CnedkBufSurfaceColorFormat.BGR
    params.batch_size = 4
    params.force_align_1 = True

    cpu_buffer = cnis.cnedk_buf_surface_create(params)
    cpu_buffer_wrapper = cnis.CnedkBufSurfaceWrapper(cpu_buffer)
    assert cpu_buffer_wrapper.get_width() == params.width
    assert cpu_buffer_wrapper.get_height() == params.height
    assert cpu_buffer_wrapper.get_color_format() == params.color_format

    # mlu memory
    params.mem_type = cnis.CnedkBufSurfaceMemType.DEVICE
    params.device_id = 0
    mlu_buffer = cnis.cnedk_buf_surface_create(params)
    mlu_buffer_wrapper = cnis.CnedkBufSurfaceWrapper(mlu_buffer)
    assert mlu_buffer_wrapper.get_width() == params.width
    assert mlu_buffer_wrapper.get_height() == params.height
    assert mlu_buffer_wrapper.get_color_format() == params.color_format
    assert mlu_buffer_wrapper.get_device_id() == params.device_id

    # get host data
    data = mlu_buffer_wrapper.get_host_data(plane_idx = 0, batch_idx = 0)
    buf_size = params.width * params.height * 3
    dtype = np.dtype(np.uint8)
    data[:] = np.random.randint(0, 255, size=buf_size, dtype=dtype).reshape(params.height, params.width, 3)
    mlu_buffer_wrapper.sync_host_to_device(plane_idx = 0, batch_idx = 0)
    result = mlu_buffer_wrapper.get_host_data(plane_idx = 0, batch_idx = 0)
    assert result.any() == data.any()


  @staticmethod
  def test_copy():
    """Test copy an array or a buffer to a BufSurface"""
    # copy from array
    params = cnis.CnedkBufSurfaceCreateParams()
    params.mem_type = cnis.CnedkBufSurfaceMemType.DEVICE
    params.width = 1920
    params.height = 1080
    params.color_format = cnis.CnedkBufSurfaceColorFormat.BGR
    params.batch_size = 1
    params.device_id = 0
    params.force_align_1 = True
    mlu_buffer = cnis.cnedk_buf_surface_create(params)

    dtype = np.dtype(np.uint8)
    buf_size = params.width * params.height * 3
    src_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    mlu_buffer.get_surface_list(0).copy_from(src_data, buf_size)
    dst_data = np.random.randint(0, 255, size=buf_size, dtype=dtype)
    mlu_buffer.get_surface_list(0).copy_to(dst_data, buf_size)
    assert dst_data.any() == src_data.any()
    cnis.cnedk_buf_surface_destroy(mlu_buffer)


class TestModelIO(object):
  """TestModelIO class provides several APIs for testing ModelIO"""
  @staticmethod
  def test_model_io():
    # Create ModelIO
    model_io = cnis.ModelIO()
    assert len(model_io.surfs) == 0
    assert len(model_io.shapes) == 0

    params = cnis.CnedkBufSurfaceCreateParams()
    params.mem_type = cnis.CnedkBufSurfaceMemType.DEVICE
    params.size = 100
    params.color_format = cnis.CnedkBufSurfaceColorFormat.TENSOR
    params.batch_size = 1
    params.device_id = 0
    params.force_align_1 = True
    buffer_num = 4
    mlu_buffers = []
    for i in range (4):
      buffer = cnis.cnedk_buf_surface_create(params)
      buffer_wrapper = cnis.CnedkBufSurfaceWrapper(buffer)
      mlu_buffers.append(buffer_wrapper)

    data_size = params.size
    dtype = np.dtype(np.uint8)
    
    src_data = np.random.randint(0, 255, size=data_size, dtype=dtype)
    for i in range (buffer_num):
      mlu_buffers[i].get_buf_surface().get_surface_list(0).copy_from(src_data, data_size)

    # Set surfs and shapes to ModelIO
    model_io.surfs = mlu_buffers
    shapes = [cnis.Shape([1, 1, 1, params.size])] * 4
    model_io.shapes = shapes
    assert len(model_io.surfs) == 4
    assert len(model_io.shapes) == 4
    for buffer in model_io.surfs:
      dst_data = np.random.randint(0, 255, size=data_size, dtype=dtype)
      buffer.get_buf_surface().get_surface_list(0).copy_to(dst_data, data_size)
      assert dst_data.any() == src_data.any()
    for i, shape in enumerate(model_io.shapes):
      assert shape == shapes[i]
