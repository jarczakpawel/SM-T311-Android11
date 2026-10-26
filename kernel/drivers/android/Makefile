# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)			# needed for trace events

# SM-T311-V71-BINDER-32BIT-ABI
# Android 11 userspace on this ARM32 tablet uses Binder protocol 7.
ifneq ($(CONFIG_64BIT),y)
ccflags-y += -DBINDER_IPC_32BIT=1
endif

obj-$(CONFIG_ANDROID_BINDERFS)		+= binderfs.o
obj-$(CONFIG_ANDROID_BINDER_IPC)	+= binder.o binder_alloc.o binder_netlink.o
obj-$(CONFIG_ANDROID_BINDER_ALLOC_KUNIT_TEST)	+= tests/
obj-$(CONFIG_ANDROID_BINDER_IPC_RUST)	+= binder/
