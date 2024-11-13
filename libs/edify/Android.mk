# Copyright 2009 The Android Open Source Project

LOCAL_PATH := $(call my-dir)

edify_src_files += \
    expr.c \
    lex.yy.c \
    parser.c

# "-x c" forces the lex/yacc files to be compiled as c the build system
# otherwise forces them to be c++. Need to also add an explicit -std because the
# build system will soon default C++ to -std=c++11.
edify_cflags := -x c -std=gnu89

#
# Build the device-side library
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(edify_src_files)

LOCAL_CFLAGS := $(edify_cflags)
LOCAL_CFLAGS += -Wno-unused-parameter -Wno-unneeded-internal-declaration -Wno-unused-function
LOCAL_MODULE := libedify_aroma

include $(BUILD_STATIC_LIBRARY)
