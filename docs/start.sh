#!/bin/bash
if [  -d "./sphinx_env"  ];then
    source sphinx_env/bin/activate
    make html
else
    virtualenv -p /usr/bin/python3 sphinx_env
    source sphinx_env/bin/activate
    pip install -i https://pypi.tuna.tsinghua.edu.cn/simple -r requirements.txt
    make html
fi
