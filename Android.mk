LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := GTA6HUD
LOCAL_SRC_FILES := main.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS    := -O2 -mfloat-abi=softfp -DNDEBUG -Wno-error
LOCAL_LDLIBS    := -llog
LOCAL_ARM_MODE  := arm

include $(BUILD_SHARED_LIBRARY)
