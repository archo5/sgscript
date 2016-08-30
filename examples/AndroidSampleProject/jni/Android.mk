LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := sgs_sample
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../src
LOCAL_SRC_FILES := interface.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_SHARED_LIBRARY)

include $(LOCAL_PATH)/../../../jni/Android.mk
