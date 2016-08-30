
LOCAL_PATH := $(call my-dir)

# SGScript core library
include $(CLEAR_VARS)
LOCAL_MODULE := sgscript
LOCAL_SRC_FILES := \
	../src/sgs_util.c \
	../src/sgs_tok.c \
	../src/sgs_ctx.c \
	../src/sgs_fnt.c \
	../src/sgs_bcg.c \
	../src/sgs_xpc.c \
	../src/sgs_proc.c \
	../src/sgs_regex.c \
	../src/sgs_std.c \
	../src/sgs_stdL.c \
	../src/sgs_srlz.c
include $(BUILD_SHARED_LIBRARY)


# extended game math library
include $(CLEAR_VARS)
LOCAL_MODULE := sgsxgmath
LOCAL_CFLAGS := -DSGS_COMPILE_MODULE=1
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_SRC_FILES := ../ext/sgsxgmath.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_SHARED_LIBRARY)


# JSON library
include $(CLEAR_VARS)
LOCAL_MODULE := sgsjson
LOCAL_CFLAGS := -DSGS_COMPILE_MODULE=1
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_SRC_FILES := ../ext/sgsjson.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_SHARED_LIBRARY)


# parallel processing library
include $(CLEAR_VARS)
LOCAL_MODULE := sgspproc
LOCAL_CFLAGS := -DSGS_COMPILE_MODULE=1
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_SRC_FILES := ../ext/sgspproc.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_SHARED_LIBRARY)


# sockets library
include $(CLEAR_VARS)
LOCAL_MODULE := sgssockets
LOCAL_CFLAGS := -DSGS_COMPILE_MODULE=1
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_SRC_FILES := ../ext/sgssockets.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_SHARED_LIBRARY)


# metaprogramming helper library
include $(CLEAR_VARS)
LOCAL_MODULE := sgsmeta
LOCAL_CFLAGS := -DSGS_COMPILE_MODULE=1
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_SRC_FILES := ../ext/sgsmeta.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_SHARED_LIBRARY)


# tests
ifneq ($(TEST),)

include $(CLEAR_VARS)
LOCAL_MODULE := sgsapitest
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_SRC_FILES := ../ext/sgsapitest.c ../ext/sgs_prof.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := sgstest
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_SRC_FILES := ../ext/sgstest.c
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := sgscppbctest
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_CPPFLAGS += -std=c++03
# need to run the local cppbctest before this to generated obj/cppbc_test.cpp
LOCAL_SRC_FILES := ../ext/sgscppbctest.cpp ../obj/cppbc_test.cpp
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := sgscppbctest11
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_CPPFLAGS += -std=c++11
# need to run the local cppbctest before this to generated obj/cppbc_test.cpp
LOCAL_SRC_FILES := ../ext/sgscppbctest.cpp ../obj/cppbc_test.cpp
LOCAL_SHARED_LIBRARIES := sgscript
include $(BUILD_EXECUTABLE)

endif
