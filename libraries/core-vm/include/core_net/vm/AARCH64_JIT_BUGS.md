# AArch64 JIT — Known Bugs and Notes

## FIXED: Conditional branch and ADR range overflow (getpeerkeys bug)

**Root cause:** AArch64 conditional branches (CBZ/CBNZ/B.cond) have a 19-bit
signed offset (±1MB), and ADR has a 21-bit offset (±1MB). Large WASM modules
like eosio.system produce JIT code exceeding these ranges. In Release builds
(assertions disabled), the overflow silently produced wrong branch targets.

**Symptom:** `getpeerkeys_unit_test_vm-jit` failed with `require_auth` receiving
the wrong account name. The 49-deep if/else action dispatcher in eosio.system's
`apply()` function caused a conditional branch to wrap to a wrong target.

**Fix (two parts):**

1. Conditional branches: Every CBZ/CBNZ/B.cond that might reach a far target
   now reserves a NOP after it. `fix_branch` detects when the 19-bit range is
   exceeded and converts to: inverted-condition short branch (+8) followed by
   unconditional B (26-bit range, ±128MB) to the real target.

2. ADR → ADRP+ADD: Replaced single ADR instruction (±1MB) with ADRP+ADD pair
   (±4GB) for PC-relative jump table addressing in `call_indirect`.

**Design constraint:** Monolithic contracts with many actions produce deep dispatch
chains and large JIT code. See AnvoIO/core#42, AnvoIO/cdt#18, AnvoIO/contracts#1.

## OPEN: `wasm_config_part5` OC test — max_pages enforcement (issue #39)

`max_pages` test expects `wasm_exception` when memory growth exceeds configured
limit via intrinsic. On AArch64 OC, the exception isn't thrown — the memory
growth succeeds when it shouldn't. Passes on x86_64 OC. This is an OC-tier
issue (`memory_grow` enforcement), not JIT.

Test: `wasm_config_part5_tests/max_pages` at `wasm_config_tests.cpp:955`.

## OPEN: `call_depth dynamic` JIT segfault (1 core-vm test)

Only `Test call depth dynamic - core_net::vm::jit` fails. Static call_depth
tests pass for both JIT and interpreter. Dynamic interpreter also passes.

The crash is at address 0x0 (return to null) inside `invoke_with_signal_handler`.
The JIT code exhausts the native stack, and the SIGSEGV handler in `signals.hpp`
doesn't catch it because `si_addr` (0x0) is not in `code_memory_range` or
`memory_range`. The handler falls through to the default action (terminate).

Root cause: the signal handler needs to handle stack overflow on AArch64. On
x86_64, this likely works because the stack guard page produces a `si_addr`
within a recognizable range. On AArch64, the stack overflow produces `si_addr=0`
(attempting to execute at the null return address after running off the stack).

Fix options:
1. Treat any SIGSEGV with `signal_dest != nullptr` and `si_addr` outside both
   ranges as a recoverable error (call `siglongjmp`).
2. Use `sigaltstack` so the signal handler has its own stack to run on when
   the main stack is exhausted.
3. Make the JIT's `emit_check_call_depth` also check remaining native stack
   space (compare sp against a known limit).

Does NOT affect chain tests (0/809 chain JIT failures from this).
