LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby
LOCAL_SRC_FILES := lib/$(TARGET_ARCH_ABI)/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)

# ---------- Original shared lib (in-process / legacy) ----------
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
    $(LOCAL_PATH)/src/xdl \
    $(LOCAL_PATH)/Il2Cpp

FILE_LIST := $(wildcard $(LOCAL_PATH)/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/src/xdl/*.c)
FILE_LIST += $(wildcard $(LOCAL_PATH)/src/ImGui/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/src/ImGui/backends/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/Il2Cpp/*.cpp)

# Exclude standalone-only sources from shared lib
FILE_LIST := $(filter-out $(LOCAL_PATH)/native_main.cpp,$(FILE_LIST))
FILE_LIST := $(filter-out $(LOCAL_PATH)/native_surface.cpp,$(FILE_LIST))

LOCAL_SRC_FILES := $(FILE_LIST:$(LOCAL_PATH)/%=%)

LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -ldl
LOCAL_CPPFLAGS := -std=c++17 -fvisibility=hidden -fno-exceptions -fpermissive -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
LOCAL_CFLAGS := -fvisibility=hidden

include $(BUILD_SHARED_LIBRARY)

# ---------- Standalone native overlay executable ----------
include $(CLEAR_VARS)
LOCAL_MODULE := v2mod

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/include/ImGui \
    $(LOCAL_PATH)/include/ImGui/backends \
    $(LOCAL_PATH)/include/ImGui/font \
    $(LOCAL_PATH)/include/Hook/Dobby \
    $(LOCAL_PATH)/include/xdl \
    $(LOCAL_PATH)/src/xdl \
    $(LOCAL_PATH)/Il2Cpp

STANDALONE_SRCS := \
    native_main.cpp \
    native_surface.cpp \
    menu.cpp \
    feature_state.cpp \
    external_engine.cpp \
    mem_reader.cpp \
    game_api.cpp \
    auto_retribution.cpp

STANDALONE_SRCS += $(wildcard $(LOCAL_PATH)/src/xdl/*.c)
STANDALONE_SRCS += $(wildcard $(LOCAL_PATH)/src/ImGui/*.cpp)
# Exclude imgui_impl_android for standalone (no JNI/ANativeActivity)
STANDALONE_SRCS := $(filter-out $(LOCAL_PATH)/src/ImGui/backends/imgui_impl_android.cpp,$(STANDALONE_SRCS))
STANDALONE_SRCS += $(LOCAL_PATH)/src/ImGui/backends/imgui_impl_opengl3.cpp

# Make src/xdl and imgui paths relative
STANDALONE_SRCS := $(STANDALONE_SRCS:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(STANDALONE_SRCS)

LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -ldl
LOCAL_CPPFLAGS := -std=c++17 -fvisibility=hidden -fno-exceptions -fpermissive -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
LOCAL_CFLAGS := -fvisibility=hidden
LOCAL_LDFLAGS := -fPIE -pie

include $(BUILD_EXECUTABLE)
