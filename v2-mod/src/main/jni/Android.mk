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
    $(LOCAL_PATH)/include/ImGui \
    $(LOCAL_PATH)/include/ImGui/backends \
    $(LOCAL_PATH)/include/ImGui/font \
    $(LOCAL_PATH)/include/Hook/Dobby \
    $(LOCAL_PATH)/include/xdl \
    $(LOCAL_PATH)/Il2Cpp

FILE_LIST := $(wildcard $(LOCAL_PATH)/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/src/ImGui/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/src/ImGui/backends/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/Il2Cpp/*.cpp)

LOCAL_SRC_FILES := $(FILE_LIST:$(LOCAL_PATH)/%=%)

LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3
LOCAL_CPPFLAGS := -std=c++17 -fvisibility=hidden -fno-exceptions -fpermissive -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
LOCAL_CFLAGS := -fvisibility=hidden

include $(BUILD_SHARED_LIBRARY)
