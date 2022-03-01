"""Utils

This module provides utility functions

"""
import os, sys
import cv2

cur_file_dir = os.path.split(os.path.realpath(__file__))[0]
sys.path.append(cur_file_dir + "/../lib")
import cnis

def prepare_video_frame():
  """Read image from file. Convert OpenCV mat to VideoFrame (convert color and copy data from cpu to mlu)"""
  # Prepare MLU data
  img = cv2.imread(cur_file_dir + "/data/test.jpg")
  w = img.shape[1]
  h = img.shape[0]
  # create a video_frame
  video_frame = cnis.VideoFrame()
  video_frame.plane_num = 2
  video_frame.format = cnis.VideoPixelFmt.NV12
  video_frame.width = w
  video_frame.height = h
  video_frame.stride = [video_frame.width, video_frame.width]

  # convert image from BGR24 TO YUV NV12
  i420_img = cv2.cvtColor(img, cv2.COLOR_BGR2YUV_I420)
  i420_img = i420_img.reshape(int(w * h * 3 / 2))
  img_y = i420_img[:w*h]
  img_uv = i420_img[w*h:]
  img_uv.reshape((int(w * h / 4), 2), order="F").reshape(int(w * h /2))

  # Create mlu buffer
  mlu_buffer_y = cnis.Buffer(w * h, 0)
  mlu_buffer_uv = cnis.Buffer(int(w * h / 2), 0)
  # copy to mlu buffer
  mlu_buffer_y.copy_from(img_y)
  mlu_buffer_uv.copy_from(img_uv)
  # set buffer to video_frame
  video_frame.set_plane(0, mlu_buffer_y)
  video_frame.set_plane(1, mlu_buffer_uv)
  return video_frame
