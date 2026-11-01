LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := GTAVGraphics
LOCAL_SRC_FILES := main.cpp mod/logger.cpp mod/config.cpp
LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_CPPFLAGS  := -O2 -std=c++17 -fexceptions -fno-rtti -DNDEBUG -mfloat-abi=softfp -mthumb
LOCAL_LDLIBS    := -llog -ldl -lGLESv2 -lEGL

# Folder "mod" berisi header + logger.cpp + config.cpp dari repo AML:
#   https://github.com/RusJJ/AndroidModLoader  (folder "mod")
# Salin seluruh folder "mod" ke sebelah file ini (jni/mod).

include $(BUILD_SHARED_LIBRARY)
