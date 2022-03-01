"""Model test

This module tests Model related APIs

"""

import os, sys
sys.path.append(os.path.split(os.path.realpath(__file__))[0] + "/../lib")
import cnis

ssd_mlu270_model_dir = \
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon"

class TestModel(object):
  """TestModel class provides several APIs for testing Model"""
  @staticmethod
  def test_model():
    infer_server = cnis.InferServer(dev_id=0)
    model = infer_server.load_model(ssd_mlu270_model_dir)
    assert model.input_layout(0)
    assert model.input_shape(0)
    assert model.output_layout(0)
    assert model.output_shape(0)
    assert model.input_num() == 1
    assert model.output_num() == 1
    assert model.batch_size() == 4
    assert model.get_key() == "./vgg16_ssd_b4c4_bgra_mlu270.cambricon_subnet0"
