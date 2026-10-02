# Native Windows

```sh
# in RubyInstaller's Devkit shell (`ridk enable`), or an MSYS2 UCRT64 shell
pacman -S --needed make diffutils curl tar git mingw-w64-ucrt-x86_64-gcc
make deps && make
bin/spinel app.rb          # -> app.exe
./app.exe
```

Spinel builds and runs on Windows as a native program: the compiler is
`spinel.exe`, the C it emits is compiled by MinGW-w64's gcc against the UCRT
-- the toolchain RubyInstaller's Devkit ships -- and the programs it builds
are ordinary Windows executables that need no DLL beyond the system's own
(the MinGW runtime is linked in statically). `spinel.exe` runs from
PowerShell or cmd.exe as well as from MSYS2: with no `gcc` on `PATH` it
uses the Devkit beside the `ruby` on `PATH` (RubyInstaller puts it there
only under `ridk enable`), then the installs RubyInstaller records in the
registry, then `C:\msys64`. WSL remains a way to run the Linux build.

`spin` runs its build steps as POSIX shell command lines, so on Windows it
needs a POSIX `sh`: it takes the first `sh.exe` on `PATH`, or the Devkit's
beside the `ruby` on `PATH`, when `SPINEL_SHELL` does not name one, and says
so when there is none.

## How the port is built

The runtime is written against POSIX, and it stays that way: everything
Windows needs is in `lib/win32/` and `win32.mk`, the arrangement `lib/wasi/`
is for wasm32-wasi.

- `lib/win32/` holds a header for each POSIX header MinGW leaves out or
  answers differently (`unistd.h`, `sys/mman.h`, `poll.h`, `spawn.h`,
  `ucontext.h`, `sys/socket.h`, `signal.h`, `dirent.h`, `sys/stat.h`,
  `time.h`, `stdio.h`, ...), each `#include_next`-ing MinGW's own where it
  has one, and the code behind them over Win32: `sp_win32.c` (files,
  processes, signals, `mmap`, ...), `sp_win32_net.c` (sockets as file
  descriptors, `poll`), `sp_win32_ctx.c` (the Windows x64 context switch),
  `sp_win32_crypt.c` (DES `crypt(3)`) and `sp_win32_driver.c` (the compiler
  driver's startup). The shim headers include neither `<windows.h>` nor
  `<winsock2.h>`; only those `.c` files do, so neither the runtime nor a
  generated program sees their macros.
- `win32.mk`, which `common.mk` includes when `$(OS)` is `Windows_NT`, sets
  the toolchain and the `PLATFORM_` variables the Makefile's rules take
  (`-Ilib/win32`, the shim's objects and libraries, the static link, the
  test marker below) -- empty on every other host -- and empty
  `lib/win32/lib{c,dl,rt,util,crypt}.a`, so a program's `-lc` or `-lcrypt`
  finds the C runtime and the shim that answer them here.
- The driver includes `lib/win32/sp_win32_driver.h` on Windows, which sets
  what it otherwise defaults to POSIX's: the compiler (`gcc`), the `.exe`,
  the flags every program gets, and a startup hook.

What `lib/` itself carries for Windows is small and named:

- **The coroutine switch** (`lib/sp_fiber_ctx.h`): Windows takes the
  `ucontext` fallback, which `lib/win32/ucontext.h` provides -- its x64 ABI
  saves more registers, and a switch there also moves the stack bounds the
  thread's TEB holds.
- **Starting a process** (`sp_process.c`, `sp_system.c`, `sp_cold.c`):
  there is no `fork`, so `Process.spawn`, `Kernel#system` and backticks have
  a `posix_spawn` path, which the shim implements over `CreateProcessW`. The
  same path builds on any POSIX host with `-DSP_USE_POSIX_SPAWN` (the corpus
  passes there), so it can be exercised without a Windows machine.
- **Path roots** (`sp_path_root_len` in `sp_alloc.h`): `File.expand_path`,
  `Dir.glob`, `File.absolute_path?`, `File.dirname` and the tmpdir package
  take `C:/` as a root as well as `/`; `Pathname.root_str` does the same.
- **One-liners**: the CSPRNG reads `getentropy` (as on WASI), which the shim
  answers from `BCryptGenRandom`; `File.birthtime` reads the creation time;
  `%a` goes through MinGW-w64's C99 printf; `RUBY_PLATFORM` is
  `x64-mingw-ucrt`, CRuby's spelling, and RbConfig and `Gem.win_platform?`
  answer for it.

## What is different on this target

- **Paths** are the ones CRuby on Windows answers: `Dir.pwd` and
  `File.expand_path` give `C:/Users/me/...` with forward slashes, and a
  backslash is accepted as a separator. Names are UTF-8 throughout (the shim
  calls the wide Win32 API). `/dev/null` means `NUL`, and `/tmp` is the
  system temporary directory (`%TEMP%`), as under MSYS2 and Cygwin.
- **Shell commands** -- `system("...")`, backticks, a one-string
  `Process.spawn` -- run under `%ComSpec%` (cmd.exe), as CRuby's do on
  Windows. Setting `SPINEL_SHELL` to a POSIX `sh` runs them under that
  instead; the test harness does, under MSYS2. A program named by its POSIX
  path (`/bin/echo`, `/usr/bin/env`) is looked up on `PATH` by its name.
- **Signals** are delivered as POSIX delivers them: a process-directed one
  (the console's Ctrl-C, Ctrl-Break or close, which are `INT`, `BREAK` and
  `HUP`; a `Process.kill` from another thread) interrupts the main thread
  and runs its handler there -- through a special user APC on Windows 11
  and Server 2022, and by suspending the thread and redirecting it where
  those are missing -- and a `sleep` it interrupts returns early with
  `EINTR` underneath, as on POSIX. A signal is blocked while its own
  handler runs. `Process.kill` on a child ends it (`TerminateProcess`), and
  `$?` reads the signal it was sent; `INT`/`BREAK` reach a child started in
  its own process group as a console Ctrl-Break. A fault in the program --
  an overflowed stack included -- is delivered to its handler as SIGSEGV,
  on the alternate signal stack, so a deep recursion is a
  `SystemStackError` here too.
- **No `fork`**: `Process.fork` and a prefork server answer `Errno::ENOSYS`.
  `exec` runs the program and exits with its status when it ends (Windows
  cannot replace a process image), as the MSVC runtime's `_exec` does.
- **Permissions and owners**: a file's mode is what Windows can say --
  read-only or not, and executable for `.exe`/`.com`/`.bat`/`.cmd` -- so
  `File.chmod(0600, f)` followed by `File.stat(f).mode` answers `0100644`.
  `chown` succeeds and changes nothing; `uid`/`gid` are 0; `Etc.getpwnam`
  knows the current user only.
- **Sockets** are file descriptors as on POSIX (`fdopen`, `IO.select`,
  non-blocking mode); `UNIXSocket` needs Windows 10 1803 or later.
  `SO_REUSEADDR` is accepted and has POSIX's meaning (winsock's own would
  let a second server take a port in use, so it is not passed through).
- **Symlinks** need Developer Mode or an elevated process, as for any
  Windows program.

## Tests

`make test-corpus` runs the corpus as on Linux, from an MSYS2 shell. A test
that asserts what only a POSIX system answers -- a path under `/proc` or
`/dev/fd`, permission bits, a POSIX shell's own output, `/etc/passwd` --
says so with a line

```ruby
# spinel: posix -- <why>
```

and is not run on Windows (the Makefile filters it, as `# spinel: int64`
is filtered on a 32-bit target). CI runs the corpus on `windows-latest`
for every push to master.
