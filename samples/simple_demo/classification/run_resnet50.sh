#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p cache

PrintUsages(){
    echo "Usages: run.sh [mlu370/mlu590/ce3226]"
}

if [ $# -ne 1 ]; then
    PrintUsages
    exit 1
fi

MODEL_DIR=${CURRENT_DIR}/cache
if [[ ${1} == "ce3226" ]]; then
    MM_VER=v0.13.0
    MODEL_PATH=${MODEL_DIR}/resnet50_${MM_VER}_4b_rgb_uint8.magicmind
    REMOTE_MODEL_PATH=http://video.cambricon.com/models/magicmind/${MM_VER}/resnet50_${MM_VER}_4b_rgb_uint8.magicmind
elif [[ ${1} == "mlu370" ]]; then
    MM_VER=v0.13.0
    MODEL_PATH=${MODEL_DIR}/resnet50_${MM_VER}_4b_rgb_uint8.magicmind
    REMOTE_MODEL_PATH=http://video.cambricon.com/models/magicmind/${MM_VER}/resnet50_${MM_VER}_4b_rgb_uint8.magicmind
elif [[ ${1} == "mlu590" ]]; then
    MM_VER=v0.14.0
    MODEL_PATH=${MODEL_DIR}/resnet50_${MM_VER}_4b_rgb_uint8.magicmind
    REMOTE_MODEL_PATH=http://video.cambricon.com/models/magicmind/${MM_VER}/resnet50_${MM_VER}_4b_rgb_uint8.magicmind
else
    PrintUsages
    exit 1
fi
LABEL_PATH=${MODEL_DIR}/synset_words.txt
REMOTE_LABEL_PATH=http://video.cambricon.com/models/labels/synset_words.txt

# download model and label
if [[ ! -f ${MODEL_PATH} ]]; then
    wget -O ${MODEL_PATH} ${REMOTE_MODEL_PATH}
    if [ $? -ne 0 ]; then
        echo "Download ${REMOTE_MODEL_PATH} to ${MODEL_PATH} failed."
        exit 1
    fi
fi

if [[ ! -f ${LABEL_PATH} ]]; then
    wget -O ${LABEL_PATH} ${REMOTE_LABEL_PATH}
    if [ $? -ne 0 ]; then
        echo "Download ${REMOTE_LABEL_PATH} to ${LABEL_PATH} failed."
        exit 1
    fi
fi

DATA_PATH="${CURRENT_DIR}/../../data/videos/cars.mp4"

../bin/classification  \
     --model_path $MODEL_PATH \
     --label_path $LABEL_PATH \
     --data_path $DATA_PATH \
     --show=false \
     --save_video=true \
     --wait_time 0 \
     --repeat_time 0 \
     --decode_type mlu \
     --dev_id 0 \
     --colorlogtostderr \
     --alsologtostderr
popd
