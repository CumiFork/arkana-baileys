LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby
LOCAL_SRC_FILES := lib/$(TARGET_ARCH_ABI)/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := MEOW

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/include/Hook/Dobby \
    $(LOCAL_PATH)/include/xdl \
    $(LOCAL_PATH)/Il2Cpp

LOCAL_SRC_FILES := \
    main.cpp \
    auto_retribution.cpp \
    Il2Cpp/il2cpp_dump.cpp

LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog -landroid
LOCAL_CPPFLAGS := -std=c++17 -fvisibility=hidden -fno-rtti -fno-exceptions -Wall -Wextra
LOCAL_CFLAGS := -fvisibility=hidden

include $(BUILD_SHARED_LIBRARY)
