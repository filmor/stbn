#!/bin/bash

emcc --bind \
  -o libmdb.html \
  ./libmdb/src/*.c ./libmdb/bindings.cpp \
  -DHAVE_ICONV \
  -DICONV_CONST= \
  -I./libmdb/include \
  -gsource-map \
  -g3 \
  -s ALLOW_MEMORY_GROWTH=1 \
  --source-map-base /
