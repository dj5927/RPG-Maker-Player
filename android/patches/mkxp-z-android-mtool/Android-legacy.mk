L_PATH := $(call my-dir)

# Common dependencies required by the legacy Ruby 1.8.7 / 1.9.3 builds.
include $(L_PATH)/SDL2/Android.mk
include $(L_PATH)/SDL2_image/Android.mk
include $(L_PATH)/SDL2_ttf/Android.mk
include $(L_PATH)/SDL2_sound.mk
include $(L_PATH)/libogg.mk
include $(L_PATH)/libvorbis.mk
include $(L_PATH)/libtheora.mk
include $(L_PATH)/openal.mk
include $(L_PATH)/pixman.mk
include $(L_PATH)/physfs.mk
include $(L_PATH)/uchardet.mk
include $(L_PATH)/libiconv.mk
include $(L_PATH)/openssl.mk

# Build all selectable RGSS Ruby runtimes. The Ruby 3.1 shared library is
# prepared separately by the dependency Makefile before ndk-build runs.
include $(L_PATH)/ruby187.mk
include $(L_PATH)/mkxp-z-187.mk
include $(L_PATH)/ruby193.mk
include $(L_PATH)/mkxp-z-193.mk
include $(L_PATH)/ruby.mk
include $(L_PATH)/mkxp-z.mk
