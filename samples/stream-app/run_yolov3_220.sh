#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p cache

if [ ! -f "cache/yolov3_b4c4_argb_mlu220.cambricon" ]; then
  wget -O cache/yolov3_b4c4_argb_mlu220.cambricon http://video.cambricon.com/models/MLU220/yolov3_b4c4_argb_mlu220.cambricon
fi
if [ ! -f "cache/label_map_coco.txt" ]; then
  wget -O cache/label_map_coco.txt http://video.cambricon.com/models/MLU220/Primary_Detector/YOLOv3/label_map_coco.txt
fi
model_file="${CURRENT_DIR}/cache/yolov3_b4c4_argb_mlu220.cambricon"
label_path="${CURRENT_DIR}/cache/label_map_coco.txt"
data_path="${CURRENT_DIR}/../data/videos/cars.mp4"
track_model_path="cpu"

../bin/stream-app  \
     --model_path $model_file \
     --track_model_path $track_model_path \
     --label_path $label_path \
     --data_path $data_path \
     --net_type "YOLOv3" \
     --show=false \
     --save_video=true \
     --wait_time 0 \
     --repeat_time 0 \
     --decode_type mlu \
     --dev_id 0
popd
