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

## OPEN: `[call_depth]` core-vm unit test segfault (4 tests)

Crashes with SIGSEGV in both JIT and interpreter. Affects the core-vm standalone
test suite, not the chain tests. Needs investigation.
