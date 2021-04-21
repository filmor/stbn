#!/bin/bash

emcc --bind \
  -o libmdb.html \
  ./libmdb/src/*.c ./libmdb/bindings.cpp \
  -I./libmdb/include \
  -gsource-map \
  -g3
