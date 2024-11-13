LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# LOCAL PATH COPY
AROMA_INSTALLER_LOCALPATH := $(LOCAL_PATH)

# VERSIONING
AROMA_NAME := AROMA Installer
AROMA_VERSION := 3.00B1
AROMA_BUILD := $(shell date +%y%m%d%H)
AROMA_CN := Melati

# MINUTF8 SOURCE FILES
LOCAL_SRC_FILES += \
    libs/minutf8/minutf8.c

# AROMA CONTROLS SOURCE FILES
LOCAL_SRC_FILES += \
    src/controls/aroma_controls.c \
    src/controls/aroma_control_button.c \
    src/controls/aroma_control_check.c \
    src/controls/aroma_control_checkbox.c \
    src/controls/aroma_control_menubox.c \
    src/controls/aroma_control_checkopt.c \
    src/controls/aroma_control_optbox.c \
    src/controls/aroma_control_textbox.c \
    src/controls/aroma_control_threads.c \
    src/controls/aroma_control_imgbutton.c

# AROMA LIBRARIES SOURCE FILES
LOCAL_SRC_FILES += \
    src/libs/aroma_array.c \
    src/libs/aroma_freetype.c \
    src/libs/aroma_graph.c \
    src/libs/aroma_input.c \
    src/libs/aroma_languages.c \
    src/libs/aroma_libs.c \
    src/libs/aroma_memory.c \
    src/libs/aroma_png.c \
    src/libs/aroma_zip.c

# AROMA FRAMEBUFFER SOURCE FILES
LOCAL_SRC_FILES += \
    src/libs/fb/aroma_fb.c \
    src/libs/fb/aroma_fbdev.c \
    src/libs/fb/aroma_drm.c \
    src/libs/fb/aroma_engine.c \
    src/libs/fb/aroma_overlay.c

# AROMA INSTALLER SOURCE FILES
LOCAL_SRC_FILES += \
    src/main/aroma_ui.c \
    src/main/aroma_installer.c \
    src/main/aroma.c

# MODULE SETTINGS
LOCAL_MODULE := aroma_installer
LOCAL_MODULE_PATH := $(PRODUCT_OUT)
LOCAL_MODULE_TAGS := eng
LOCAL_FORCE_STATIC_EXECUTABLE := true

# INCLUDES
LOCAL_C_INCLUDES := \
    $(AROMA_INSTALLER_LOCALPATH)/libs/minutf8 \
    $(AROMA_INSTALLER_LOCALPATH)/src/libs/fb \
    $(AROMA_INSTALLER_LOCALPATH)/src \
    external/freetype/include \
    external/selinux/libselinux/include \
    external/png \
    external/libdrm \
    external/libdrm/include/drm \
    bootable/recovery

# The header files required for qcom overlay graphics!
ifeq ($(TARGET_CUSTOM_KERNEL_HEADERS),)
    LOCAL_C_INCLUDES += bootable/recovery/minuitwrp/include
else
    LOCAL_C_INCLUDES += $(TARGET_CUSTOM_KERNEL_HEADERS)
endif

# COMPILER FLAGS
LOCAL_CFLAGS := -O2
LOCAL_CFLAGS += -DFT2_BUILD_LIBRARY=1 -DDARWIN_NO_CARBON
LOCAL_CFLAGS += -fdata-sections -ffunction-sections
LOCAL_CFLAGS += -Wl,--gc-sections -fPIC -DPIC
LOCAL_CFLAGS += -D_AROMA_NODEBUG
#LOCAL_CFLAGS += -D_AROMA_VERBOSE_INFO

# SET VERSION
LOCAL_CFLAGS += -DAROMA_NAME="\"$(AROMA_NAME)\""
LOCAL_CFLAGS += -DAROMA_VERSION="\"$(AROMA_VERSION)\""
LOCAL_CFLAGS += -DAROMA_BUILD="\"$(AROMA_BUILD)\""
LOCAL_CFLAGS += -DAROMA_BUILD_CN="\"$(AROMA_CN)\""

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

# INCLUDED LIBRARIES
LOCAL_STATIC_LIBRARIES := libpng libminzip libft2_aroma_static libedify_aroma libm libc libz libdrm libcutils

# Remove Old Build
$(shell rm -rf $(PRODUCT_OUT)/obj/EXECUTABLES/$(LOCAL_MODULE)_intermediates)
$(shell rm -rf $(PRODUCT_OUT)/aroma.zip)

include $(BUILD_EXECUTABLE)

# freetype
include $(AROMA_INSTALLER_LOCALPATH)/libs/freetype/Android.mk

# edify
include $(AROMA_INSTALLER_LOCALPATH)/libs/edify/Android.mk

include $(CLEAR_VARS)

AROMA_ZIP_TARGET := $(PRODUCT_OUT)/aroma.zip
$(AROMA_ZIP_TARGET):
	@echo "----- Making aroma zip installer ------"
	$(hide) rm -rf $(PRODUCT_OUT)/assets
	$(hide) rm -rf $(PRODUCT_OUT)/aroma.zip
	$(hide) cp -R $(AROMA_INSTALLER_LOCALPATH)/assets/ $(PRODUCT_OUT)/assets/
	$(hide) cp $(PRODUCT_OUT)/aroma_installer $(PRODUCT_OUT)/assets/META-INF/com/google/android/update-binary
	$(hide) pushd $(PRODUCT_OUT)/assets/ && zip -r9 ../aroma.zip . && popd
	@echo "Made flashable aroma.zip: $@"

.PHONY: aroma_installer_zip
aroma_installer_zip: $(AROMA_ZIP_TARGET)
