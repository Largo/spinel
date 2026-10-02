# win32.mk -- the native Windows build: MinGW-w64 gcc against the UCRT, as
# MSYS2 and RubyInstaller's Devkit ship it. common.mk includes this file when
# $(OS) is Windows_NT; on every other host the PLATFORM_ variables it sets are
# empty and none of it is read.
#
# The runtime is written against POSIX. lib/win32 holds the POSIX surface
# MinGW leaves out or answers differently (see lib/win32/sp_win32.h): every
# compile sees it ahead of MinGW's own headers, and its objects go into the
# runtime archives and the compiler. Programs link statically, so one needs
# no MinGW DLL beside it.

# the rules below are read before the Makefile's own: `make` alone is still
# `make all`
.DEFAULT_GOAL := all

# there is no `cc`
ifeq ($(origin CC),default)
  CC = gcc
endif

# the shim's headers first; 64-bit st_size and offsets; no winsock.h through
# <windows.h> (the shim declares its own sockets)
PLATFORM_FLAGS = -Ilib/win32 -D_FILE_OFFSET_BITS=64 -DWIN32_LEAN_AND_MEAN
PLATFORM_HDRS  = $(wildcard lib/win32/*.h lib/win32/*/*.h)
# the winsock and CNG import libraries the shim calls
PLATFORM_LIBS  = -lws2_32 -lbcrypt

PLATFORM_SRC    = lib/win32/sp_win32.c lib/win32/sp_win32_net.c lib/win32/sp_win32_ctx.c lib/win32/sp_win32_crypt.c lib/win32/sp_win32_driver.c
PLATFORM_OBJ    = $(patsubst lib/win32/%.c,build/win32/%.o,$(PLATFORM_SRC))
PLATFORM_MT_OBJ = $(patsubst lib/win32/%.c,build/mt/win32/%.o,$(PLATFORM_SRC))

build/win32/%.o: lib/win32/%.c $(PLATFORM_HDRS)
	@mkdir -p $(@D)
	$(CC) -c $(COPT) $(SEC_FLAGS) $< -o $@
build/mt/win32/%.o: lib/win32/%.c $(PLATFORM_HDRS)
	@mkdir -p $(@D)
	$(CC) -c $(COPT) $(SEC_FLAGS) $(MT_DEF) $< -o $@

# Static, with the shim's import libraries, and an 8 MB main-thread stack as
# Linux gives one (Windows' default is 1 MB); the compiler itself recurses
# deeper and gets 64 MB. -Llib/win32 is for the stub archives below.
LDFLAGS += -static $(PLATFORM_LIBS) -Llib/win32 -Wl,--stack,8388608
SPINEL_LDFLAGS = -Wl,--stack,67108864

# libc's own link names (-lc for an ffi_lib "c", -lcrypt for String#crypt):
# the C runtime and the shim are what answers them here, and every link has
# both, so each is an empty archive. The driver passes -L<lib>/win32 too.
PLATFORM_STUB_LIBS = $(addprefix lib/win32/lib,$(addsuffix .a,c dl rt util crypt))
lib/win32/lib%.a:
	@rm -f $@
	ar rcs $@
lib/libspinel_rt.a lib/libspinel_rt_mt.a: | $(PLATFORM_STUB_LIBS)

# every bundled package object includes the runtime headers, and so the shim's
$(foreach p,$(wildcard packages/*/sp_*.c),$(p:.c=.o) $(p:.c=_mt.o)): $(PLATFORM_HDRS)

# spinel-timeout over CreateProcess, in place of the fork/SIGALRM one
SPINEL_TIMEOUT_SRC = scripts/spinel-timeout-win32.c
SPINEL_TIMEOUT_LDFLAGS = -municode

# The corpus's shell command lines are written for a POSIX sh, and the shim
# runs `/bin/sh -c CMD` under cmd.exe as CRuby on Windows does; the harness
# runs under MSYS2, so its programs get that sh instead (SPINEL_SHELL).
export SPINEL_SHELL := $(shell cygpath -m /usr/bin/sh 2>/dev/null)

# a test that asserts what only a POSIX system answers says
# `# spinel: posix -- <why>`, and is not run here
TEST_SKIP_MARKER = posix
