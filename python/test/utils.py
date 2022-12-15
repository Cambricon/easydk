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

"""Utils

This module provides utility functions

"""
import os, sys
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis

tag = "stream_0"

def get_model_dir():
  if cnis.get_platfrom_name() == "MLU370":
    return "http://video.cambricon.com/models/magicmind/v0.13.0/yolov3_v0.13.0_4b_rgb_uint8.magicmind"
  else:
    return "http://video.cambricon.com/models/magicmind/v0.13.0/yolov3_v0.14.0_4b_rgb_uint8.magicmind"

def get_model_key():
  if cnis.get_platfrom_name() == "MLU370":
    return "./yolov3_v0.13.0_4b_rgb_uint8.magicmind"
  else:
    return "./yolov3_v0.14.0_4b_rgb_uint8.magicmind"

def prepare_input():
  """Read image from file. Convert OpenCV mat to BufSurface"""
  img = cv2.imread(cur_file_dir + "/../data/test.jpg")
  img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
  preproc_input = cnis.PreprocInput()
  preproc_input.surf = cnis.convert_to_buf_surf(img, cnis.CnedkBufSurfaceColorFormat.RGB)
  # Create input package and set PreprocInput to input package
  input_pak = cnis.Package(1, tag)
  input_pak.data[0].set(preproc_input)
  return input_pak

def get_model_input_wh(model):
  """Get the input width and height of the model"""
  width = 0
  height = 0
  order = model.input_layout(0).order
  # Get height and width according to the dim order and the shape.
  if order == cnis.DimOrder.NHWC:
    width = model.input_shape(0)[2]
    height = model.input_shape(0)[1]
  elif order == cnis.DimOrder.NCHW:
    width = model.input_shape(0)[1]
    height = model.input_shape(0)[0]
  else:
    print("unsupported dim order")
  return width, height

def prepare_model_input(model):
  """Read image from file. Convert OpenCV mat to BufSurface"""
  img = cv2.imread(cur_file_dir + "/../data/test.jpg")
  # resize to model input shape
  resized_img = cv2.resize(img, (get_model_input_wh(model)))
  # convert color to model input pixel format
  result_img = cv2.cvtColor(resized_img, cv2.COLOR_BGR2RGB)
  preproc_input = cnis.PreprocInput()
  preproc_input.surf = cnis.convert_to_buf_surf(result_img, cnis.CnedkBufSurfaceColorFormat.RGB)
  # Create input package and set PreprocInput to input package
  input_pak = cnis.Package(1, tag)
  input_pak.data[0].set(preproc_input)
  return input_pak
