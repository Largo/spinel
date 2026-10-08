/* Windows: what the compiler driver (src/main.c) says differently when it
   builds a program here -- gcc (clang on ARM64), the .exe, the shim's
   headers and libraries on every program's command line ("@LIB@" is the
   runtime's lib directory), and two things done once at startup
   (sp_win32_driver.c). */
#ifndef SP_WIN32_DRIVER_H
#define SP_WIN32_DRIVER_H
#define SPINEL_EXE_SUFFIX ".exe"
#if defined(__aarch64__)
/* ARM64: clang, as MSYS2's CLANGARM64 has no gcc, and what clang needs said
   that gcc does on its own -- winpthread on the link line. -falign-loops=1
   comes after the driver's -falign-loops=64: LLVM cannot size a function
   whose loop heads are padded when it writes ARM64 Windows unwind info
   ("Failed to evaluate function length in SEH unwind info", clang 22 and
   23), so loops keep their natural alignment there. */
#define SPINEL_DEFAULT_CC "clang"
#define SPINEL_PLATFORM_CFLAGS "-I@LIB@/win32 -D_FILE_OFFSET_BITS=64 -DWIN32_LEAN_AND_MEAN -falign-loops=1"
#define SPINEL_PLATFORM_LIBS   "-L@LIB@/win32 -static -lws2_32 -lbcrypt -lwinpthread -Wl,--stack,8388608"
#else
#define SPINEL_DEFAULT_CC "gcc"
#define SPINEL_PLATFORM_CFLAGS "-I@LIB@/win32 -D_FILE_OFFSET_BITS=64 -DWIN32_LEAN_AND_MEAN"
#define SPINEL_PLATFORM_LIBS   "-L@LIB@/win32 -static -lws2_32 -lbcrypt -Wl,--stack,8388608"
#endif
/* $TMPDIR as the C compiler can use it, and the C compiler on PATH when
   RubyInstaller's Devkit or MSYS2 has one that is not */
void sp_w32_driver_init(void);
/* a path named on the command line, with slashes: the driver splits on '/',
   and the C it writes names the file in #line, where a backslash escapes */
const char *sp_w32_driver_path(const char *p);
/* a builtins/ or packages/ file the compiler read, with what a Windows
   build answers differently appended: lib/win32/overlay/<the same path>,
   reopening what the file defines (RbConfig's mingw32, Gem.win_platform?,
   Pathname's C:/ root). The file's own lines keep their numbers. */
char *sp_w32_overlay(char *content, const char *path);
#define SPINEL_PLATFORM_INIT()  sp_w32_driver_init()
#define SPINEL_PLATFORM_PATH(p) sp_w32_driver_path(p)
#endif
