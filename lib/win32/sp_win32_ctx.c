/* sp_win32_ctx.c -- the coroutine context switch for Windows x64 and ARM64, behind
   <ucontext.h> (ucontext.h here), which the runtime's coroutine fallback
   calls (lib/sp_fiber_ctx.h).

   sp_fiber.c's x86_64 switch is written for the System V ABI; Windows x64
   passes the arguments in rcx/rdx, keeps rdi, rsi and xmm6-xmm15 across a
   call as well, and leaves 32 bytes of home space above a callee's return
   address. And Windows keeps the running stack's bounds in the thread's TEB
   -- StackBase, StackLimit, DeallocationStack -- which the unwinder checks
   frames against and the memory manager consults to grow a stack on its
   guard page. So the switch saves and restores those three with the
   registers: each coroutine stack is, to Windows, the thread's stack while
   it runs, and grows the way a thread's does (sp_win32.c maps a MAP_STACK
   region like one: committed at the top, a guard page below).

   The frame sp_w32_swapcontext leaves on the stack it switches away from, from the
   saved stack pointer up:
     +0   DeallocationStack   gs:[0x1478]
     +8   StackLimit          gs:[0x10]
     +16  StackBase           gs:[0x08]
     +24  xmm6 .. xmm15       10 x 16 bytes
     +184 r15 r14 r13 r12 rsi rdi rbx rbp
     +248 return address

   On ARM64 the arguments come in x0/x1, a call keeps x19-x28, the frame
   pointer, the link register and d8-d15, and x18 holds the TEB (the same
   64-bit TEB as x64's, with the same offsets), which nothing here may
   change. The frame there, from the saved stack pointer up (sp stays
   16-aligned throughout):
     +0   x19 x20 .. x27 x28  10 x 8 bytes
     +80  x29 (fp) x30 (lr)   the switch returns through lr
     +96  d8 .. d15           8 x 8 bytes
     +160 StackBase           [x18, #0x08]
     +168 StackLimit          [x18, #0x10]
     +176 DeallocationStack   [x18, #0x1478]
     +184 (padding) */
#if defined(_WIN64) && (defined(__x86_64__) || defined(__aarch64__))
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ucontext.h>

#if defined(__x86_64__)
__asm__(
  ".text\n"
  ".globl sp_w32_swapcontext\n"
  ".def sp_w32_swapcontext; .scl 2; .type 32; .endef\n"
  "sp_w32_swapcontext:\n"
  "  pushq %rbp\n  pushq %rbx\n  pushq %rdi\n  pushq %rsi\n"
  "  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
  "  subq $160, %rsp\n"
  "  movups %xmm6,    0(%rsp)\n  movups %xmm7,   16(%rsp)\n"
  "  movups %xmm8,   32(%rsp)\n  movups %xmm9,   48(%rsp)\n"
  "  movups %xmm10,  64(%rsp)\n  movups %xmm11,  80(%rsp)\n"
  "  movups %xmm12,  96(%rsp)\n  movups %xmm13, 112(%rsp)\n"
  "  movups %xmm14, 128(%rsp)\n  movups %xmm15, 144(%rsp)\n"
  "  pushq %gs:0x08\n"           /* StackBase */
  "  pushq %gs:0x10\n"           /* StackLimit */
  "  pushq %gs:0x1478\n"         /* DeallocationStack */
  "  movq %rsp, (%rcx)\n"        /* from->sp = rsp */
  "  movq (%rdx), %rsp\n"        /* rsp = to->sp   */
  "  popq %gs:0x1478\n"
  "  popq %gs:0x10\n"
  "  popq %gs:0x08\n"
  "  movups    0(%rsp), %xmm6\n  movups   16(%rsp), %xmm7\n"
  "  movups   32(%rsp), %xmm8\n  movups   48(%rsp), %xmm9\n"
  "  movups   64(%rsp), %xmm10\n  movups   80(%rsp), %xmm11\n"
  "  movups   96(%rsp), %xmm12\n  movups  112(%rsp), %xmm13\n"
  "  movups  128(%rsp), %xmm14\n  movups  144(%rsp), %xmm15\n"
  "  addq $160, %rsp\n"
  "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n"
  "  popq %rsi\n  popq %rdi\n  popq %rbx\n  popq %rbp\n"
  "  xorl %eax, %eax\n"          /* swapcontext answers 0 */
  "  ret\n"
);
#else
__asm__(
  ".text\n"
  ".globl sp_w32_swapcontext\n"
  ".def sp_w32_swapcontext; .scl 2; .type 32; .endef\n"
  ".p2align 2\n"
  "sp_w32_swapcontext:\n"
  "  sub sp, sp, #192\n"
  "  stp x19, x20, [sp, #0]\n   stp x21, x22, [sp, #16]\n"
  "  stp x23, x24, [sp, #32]\n  stp x25, x26, [sp, #48]\n"
  "  stp x27, x28, [sp, #64]\n  stp x29, x30, [sp, #80]\n"
  "  stp d8,  d9,  [sp, #96]\n  stp d10, d11, [sp, #112]\n"
  "  stp d12, d13, [sp, #128]\n stp d14, d15, [sp, #144]\n"
  "  ldr x9,  [x18, #0x08]\n"    /* StackBase */
  "  ldr x10, [x18, #0x10]\n"    /* StackLimit */
  "  ldr x11, [x18, #0x1478]\n"  /* DeallocationStack */
  "  stp x9, x10, [sp, #160]\n"
  "  str x11, [sp, #176]\n"
  "  mov x9, sp\n"
  "  str x9, [x0]\n"             /* from->sp = sp */
  "  ldr x9, [x1]\n"
  "  mov sp, x9\n"               /* sp = to->sp */
  "  ldp x9, x10, [sp, #160]\n"
  "  ldr x11, [sp, #176]\n"
  "  str x9,  [x18, #0x08]\n"
  "  str x10, [x18, #0x10]\n"
  "  str x11, [x18, #0x1478]\n"
  "  ldp x19, x20, [sp, #0]\n   ldp x21, x22, [sp, #16]\n"
  "  ldp x23, x24, [sp, #32]\n  ldp x25, x26, [sp, #48]\n"
  "  ldp x27, x28, [sp, #64]\n  ldp x29, x30, [sp, #80]\n"
  "  ldp d8,  d9,  [sp, #96]\n  ldp d10, d11, [sp, #112]\n"
  "  ldp d12, d13, [sp, #128]\n ldp d14, d15, [sp, #144]\n"
  "  add sp, sp, #192\n"
  "  mov w0, #0\n"               /* swapcontext answers 0 */
  "  ret\n"
);
#endif

/* A context makecontext can prime: the stack it runs on is the caller's to
   set in uc_stack afterwards, as POSIX has it. */
int sp_w32_getcontext(ucontext_t *uc) {
  if (!uc) { errno = EINVAL; return -1; }
  memset(uc, 0, sizeof *uc);
  return 0;
}

/* Prime a context so the first switch into it starts fn() on the stack
   uc_stack names. The TEB values it will run under: the top as StackBase,
   the reservation's start as DeallocationStack, and as StackLimit the
   bottom of the committed run at the top (the guard page sits just below
   it; the memory manager moves both as the stack grows). fn takes no
   arguments here and must not return. */
void sp_w32_makecontext(ucontext_t *uc, void (*fn)(void), int argc, ...) {
  (void)argc;
  char *base = (char *)uc->uc_stack.ss_sp;
  uintptr_t top = ((uintptr_t)base + uc->uc_stack.ss_size) & ~(uintptr_t)15;
  void *dealloc = base, *limit = (void *)top;
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery((void *)(top - 1), &mbi, sizeof mbi)) {
    dealloc = mbi.AllocationBase;
    /* the committed run the top is in starts where the walk up from the
       reservation's start first meets committed memory that is not the
       guard page; VirtualQuery at the top itself names only the top page.
       ARM64 Windows checks a resumed thread's sp against StackLimit, so a
       limit too high there ended a program whose frames had gone below it
       at the first exception it continued from (an -O0 build's) */
    for (char *p = (char *)dealloc; p < (char *)top; ) {
      if (!VirtualQuery(p, &mbi, sizeof mbi)) break;
      char *end = (char *)mbi.BaseAddress + mbi.RegionSize;
      if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && end >= (char *)top) {
        limit = mbi.BaseAddress;
        break;
      }
      p = end;
    }
  }
#if defined(__x86_64__)
  /* fn's first instruction sees rsp % 16 == 8, as after a call, with its
     32 bytes of home space above the return slot */
  uintptr_t ret_slot = top - 48;
  void **s = (void **)(ret_slot - 248);
  memset(s, 0, 256);
  s[0] = dealloc;
  s[1] = limit;
  s[2] = (void *)top;
  s[31] = (void *)fn;   /* +248 */
#else
  /* the switch returns through the saved lr into fn, with sp at the top
     (16-aligned) and fp 0, the end of the frame chain */
  void **s = (void **)(top - 192);
  memset(s, 0, 192);
  s[11] = (void *)fn;     /* +88:  x30 */
  s[20] = (void *)top;    /* +160: StackBase */
  s[21] = limit;          /* +168: StackLimit */
  s[22] = dealloc;        /* +176: DeallocationStack */
#endif
  uc->sp = s;
}
#endif
