/* sp_win32_driver.c -- the compiler driver's startup on Windows (see
   sp_win32_driver.h). Linked into spinel.exe only: nothing in a program
   calls these. */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "sp_win32_driver.h"

static char *sp_w32_drv_slashes(const char *p) {
  char *d = _strdup(p);
  if (d) for (char *c = d; *c; c++) if (*c == '\\') *c = '/';
  return d;
}

const char *sp_w32_driver_path(const char *p) {
  if (!p || !strchr(p, '\\')) return p;
  char *d = sp_w32_drv_slashes(p);
  return d ? d : p;
}

static int sp_w32_drv_file(const wchar_t *p) {
  DWORD a = GetFileAttributesW(p);
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* <root>\msys64\ucrt64\bin when it holds a gcc: RubyInstaller's Devkit
   layout (and MSYS2's own, with <root> its parent). The UCRT64 one: the
   runtime archive is built against the UCRT, and a MINGW64 gcc links
   msvcrt. */
static int sp_w32_drv_devkit(const wchar_t *root, wchar_t *bin, size_t cap) {
  _snwprintf(bin, cap, L"%ls\\msys64\\ucrt64\\bin", root);
  bin[cap - 1] = 0;
  wchar_t gcc[MAX_PATH * 2];
  _snwprintf(gcc, MAX_PATH * 2, L"%ls\\gcc.exe", bin);
  gcc[MAX_PATH * 2 - 1] = 0;
  return sp_w32_drv_file(gcc);
}

/* the gcc to use when PATH has none: the Devkit beside the ruby on PATH,
   then the installs RubyInstaller records in the registry, then MSYS2's
   default C:\msys64 */
static int sp_w32_drv_find_gcc(wchar_t *bin, size_t cap) {
  wchar_t ruby[MAX_PATH * 2];
  DWORD n = SearchPathW(NULL, L"ruby.exe", NULL, MAX_PATH * 2, ruby, NULL);
  if (n > 0 && n < MAX_PATH * 2) {
    wchar_t *s = wcsrchr(ruby, L'\\');          /* ...\bin\ruby.exe */
    if (s) { *s = 0; s = wcsrchr(ruby, L'\\'); }
    if (s) { *s = 0; if (sp_w32_drv_devkit(ruby, bin, cap)) return 1; }
  }
  static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
  for (int r = 0; r < 2; r++) {
    HKEY k;
    if (RegOpenKeyExW(roots[r], L"Software\\RubyInstaller\\MRI", 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
    wchar_t ver[64];
    for (DWORD i = 0; ; i++) {
      DWORD vl = 64;
      if (RegEnumKeyExW(k, i, ver, &vl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
      wchar_t loc[MAX_PATH * 2]; DWORD ll = sizeof loc;
      if (RegGetValueW(k, ver, L"InstallLocation", RRF_RT_REG_SZ, NULL, loc, &ll) == ERROR_SUCCESS) {
        size_t m = wcslen(loc);
        while (m && (loc[m - 1] == L'\\' || loc[m - 1] == L'/')) loc[--m] = 0;
        if (sp_w32_drv_devkit(loc, bin, cap)) { RegCloseKey(k); return 1; }
      }
    }
    RegCloseKey(k);
  }
  return sp_w32_drv_devkit(L"C:", bin, cap);
}

void sp_w32_driver_init(void) {
  /* the temporary directory the driver writes the C into: a POSIX-spelled
     $TMPDIR (an MSYS2 shell's /tmp) means nothing to gcc, so %TEMP% */
  const char *t = getenv("TMPDIR");
  if (!t || !*t || t[0] == '/') {
    const char *w = getenv("TEMP");
    if (!w || !*w) w = getenv("TMP");
    if (w && *w) {
      char *d = sp_w32_drv_slashes(w);
      if (d) { _putenv_s("TMPDIR", d); free(d); }
    }
  }
  /* gcc: RubyInstaller puts its Devkit on PATH only under `ridk enable` */
  wchar_t found[MAX_PATH * 2];
  if (SearchPathW(NULL, L"gcc.exe", NULL, MAX_PATH * 2, found, NULL) > 0) return;
  wchar_t bin[MAX_PATH * 2];
  if (!sp_w32_drv_find_gcc(bin, MAX_PATH * 2)) return;
  const wchar_t *old = _wgetenv(L"PATH");
  size_t len = wcslen(bin) + (old ? wcslen(old) : 0) + 2;
  wchar_t *np = (wchar_t *)malloc(sizeof(wchar_t) * len);
  if (!np) return;
  _snwprintf(np, len, L"%ls;%ls", bin, old ? old : L"");
  np[len - 1] = 0;
  _wputenv_s(L"PATH", np);   /* the CRT's copy and the process's, which children inherit */
  free(np);
}
#endif
