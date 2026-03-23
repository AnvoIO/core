#pragma once

// ============================================================================
// AArch64 (ARM64) JIT backend for core-vm
// ============================================================================
//
// This file implements the machine_code_writer for AArch64, generating native
// ARM64 instructions from WASM bytecode. It mirrors the x86_64.hpp interface
// exactly — same public methods, same calling conventions with the host runtime.
//
// ---- Design Decisions & Tradeoffs ----
//
// 1. OPERAND STACK: 16-byte slots on sp
//
//    AArch64 (AAPCS64) requires sp to be 16-byte aligned for ALL sp-relative
//    memory accesses. Unlike x86_64 which uses 8-byte pushq/popq for the
//    operand stack, we use 16-byte stp/ldp pairs:
//
//      Push:  stp reg, xzr, [sp, #-16]!    (8 bytes value + 8 bytes padding)
//      Pop:   ldp reg, xzr, [sp], #16
//
//    This doubles per-slot overhead vs x86 but keeps the design simple:
//    one stack pointer (sp) serves params, locals, operand stack, and call
//    frames — matching the x86 pattern where rsp does everything.
//
//    Alternative considered: using a dedicated register (x28) as a separate
//    operand stack pointer with 8-byte slots. Rejected because it splits
//    the stack into two domains, complicating every C++ call site (host
//    functions, softfloat, globals) which must bridge between the two.
//    The 16-byte approach is simpler and follows the ISA's natural alignment.
//
//    Consequence: the stack_allocator in allocator.hpp always allocates a
//    separate execution stack on AArch64 (threshold=0 vs 4MB on x86) and
//    uses stack_slot_size=16 when computing the required size. Without this,
//    small WASM modules overflow the C++ thread stack.
//
// 2. REGISTER CONVENTION: x0/x1 as working registers, x21/x22 as backups
//
//    x86_64 uses rdi=context and rsi=linear_memory, relying on the fact that
//    x86 JIT code never writes to rdi/rsi. On AArch64, function returns put
//    the result in x0 (clobbering the context pointer). Rather than avoiding
//    x0/x1 writes (impossible with AAPCS64), we:
//
//      - Pin context and linear_memory to callee-saved x21/x22 in the trampoline
//      - Set x0=x21, x1=x22 before entering JIT code
//      - After every WASM-to-WASM call, restore: mov x0,x21 / mov x1,x22
//      - Around C++ calls (host, softfloat, globals), save/restore x0/x1 on stack
//
//    x21/x22 survive all function calls (callee-saved) so they're always valid.
//
// 3. INSTRUCTION CACHE COHERENCY
//
//    AArch64 has non-coherent instruction and data caches. After writing JIT
//    code via memcpy (in allocator.hpp), we must call __builtin___clear_cache
//    to flush the D-cache and invalidate the I-cache. Without this, the CPU
//    may execute stale instructions. This is a no-op on x86 (coherent caches).
//
// 4. ALTERNATE STACK ALLOCATION
//
//    The stack_allocator always provides a separate mmap'd execution stack on
//    AArch64 (vs only when >4MB on x86). This is necessary because:
//      - 16-byte slots double the operand stack footprint
//      - AArch64 has no red zone (vs 128 bytes on x86)
//      - Re-entrant host calls consume C++ stack frames between JIT levels
//    The alt stack top is reserved with 32-byte alignment (vs 24 on x86)
//    to satisfy AAPCS64's 16-byte sp alignment requirement.
//
// ---- Register Convention ----
//
//   x0  = context pointer  (AAPCS64 arg0, restored from x21 after calls)
//   x1  = linear memory base  (AAPCS64 arg1, restored from x22 after calls)
//   x19 = call depth counter  (callee-saved)
//   x20 = alt stack pointer (callee-saved, set by trampoline)
//   x21 = context pointer backup (callee-saved, set by trampoline)
//   x22 = linear memory backup (callee-saved, set by trampoline)
//   x29 = frame pointer (fp)
//   x30 = link register (lr)
//   x9-x15 = scratch registers (caller-saved)
//   sp  = stack pointer (16-byte aligned, carries everything)
//
// ---- Operand Stack Model ----
//
//   Each WASM value occupies 16 bytes on the native stack:
//     8 bytes value + 8 bytes padding (xzr).
//   Push:  stp reg, xzr, [sp, #-16]!
//   Pop:   ldp reg, xzr, [sp], #16
//   This keeps sp 16-byte aligned at all times.
//
// ---- Stack Frame Layout (after prologue) ----
//
//   [higher addresses]
//     param0        <--- x29 + 16*(nparams)
//     param1
//     ...
//     paramN
//     saved x29     <--- x29
//     saved x30     <--- x29 + 8
//     local0        <--- x29 - 16
//     local1        <--- x29 - 32
//     ...
//     localN
//     <operand stack grows downward>
//   [lower addresses]

#include <core_net/vm/allocator.hpp>
#include <core_net/vm/exceptions.hpp>
#include <core_net/vm/signals.hpp>
#include <core_net/vm/softfloat.hpp>
#include <core_net/vm/types.hpp>
#include <core_net/vm/utils.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>


namespace core_net { namespace vm {

   // ---- A64 instruction encoding constants ----
   //
   // All A64 instructions are fixed-width 32-bit words.
   // Branch offsets are in units of 4 bytes (instructions).

   template<typename Context>
   class machine_code_writer {
    public:
      machine_code_writer(growable_allocator& alloc, std::size_t source_bytes, module& mod) :
         _mod(mod), _allocator(alloc), _code_segment_base(_allocator.start_code()) {

         // Emit 4 error handlers.
         // Each handler is 5 A64 instructions = 20 bytes.
         const std::size_t error_handler_size = 4 * 20;
         _code_start = _allocator.alloc<unsigned char>(error_handler_size);
         _code_end = _code_start + error_handler_size;
         code = _code_start;

         fpe_handler = emit_error_handler(&on_fp_error);
         call_indirect_handler = emit_error_handler(&on_call_indirect_error);
         type_error_handler = emit_error_handler(&on_type_error);
         stack_overflow_handler = emit_error_handler(&on_stack_overflow);

         assert(code == _code_end);

         // Emit host function stubs.
         // Each host function stub:
         //   Without backtrace: 14 instructions = 56 bytes (max 15 = 60 if funcnum > 0xFFFF)
         //   With backtrace:    16 instructions = 64 bytes (max 17 = 68 if funcnum > 0xFFFF)
         // Allocate 64 bytes base + 8 for backtrace to cover worst case.
         const uint32_t num_imported = mod.get_imported_functions_size();
         const std::size_t host_fn_size = (64 + 8 * Context::async_backtrace()) * num_imported;
         _code_start = _allocator.alloc<unsigned char>(host_fn_size);
         _code_end = _code_start + host_fn_size;
         // code already set (continues from previous allocation)
         for(uint32_t i = 0; i < num_imported; ++i) {
            start_function(code, i);
            emit_host_call(i);
         }
         assert(code <= _code_end);
         // Advance code pointer past any unused padding in the host stub allocation
         // so that the jump table starts at the correct position.
         code = _code_end;

         // Emit jump table for call_indirect.
         jmp_table = code;
         if (_mod.tables.size() > 0) {
            // Each table entry: 7 A64 instructions = 28 bytes
            //   cmp w9, #expected_type (1 instr)
            //   b.ne type_error (1 instr)
            //   b target_function (1 instr)
            //   -- or for out-of-range: --
            //   b call_indirect_error (1 instr)
            //   nop padding (6 instrs)
            _table_element_size = 28;
            const std::size_t table_size = _table_element_size * _mod.tables[0].table.size();
            _code_start = _allocator.alloc<unsigned char>(table_size);
            _code_end = _code_start + table_size;
            for(uint32_t i = 0; i < _mod.tables[0].table.size(); ++i) {
               uint32_t fn_idx = _mod.tables[0].table[i];
               if (fn_idx < _mod.fast_functions.size()) {
                  // Load expected type into w10 for comparison
                  uint32_t expected_type = _mod.fast_functions[fn_idx];
                  // movz w10, #(expected_type & 0xFFFF)
                  emit_a64(0x52800000 | ((expected_type & 0xFFFF) << 5) | 10);
                  if (expected_type > 0xFFFF) {
                     // movk w10, #(expected_type >> 16), lsl #16
                     emit_a64(0x72A00000 | (((expected_type >> 16) & 0xFFFF) << 5) | 10);
                  } else {
                     // nop (padding to keep fixed size)
                     emit_a64(0xD503201F);
                  }
                  // cmp w9, w10  (w9 = actual type from call_indirect)
                  emit_a64(0x6B0A013F);
                  // b.eq target_function  (will be fixed up)
                  {
                     void* branch = code;
                     emit_a64(0x54000000); // b.eq placeholder (cond=EQ=0x0)
                     register_call(branch, fn_idx);
                  }
                  // b type_error_handler
                  {
                     void* te_branch = code;
                     emit_a64(0x14000000); // B placeholder
                     fix_branch(te_branch, type_error_handler);
                  }
                  // nop padding to fill 28 bytes (7 instrs total, used 5 so far)
                  emit_a64(0xD503201F);
                  emit_a64(0xD503201F);
               } else {
                  // Out-of-range function: branch to call_indirect_handler
                  {
                     void* ci_branch = code;
                     emit_a64(0x14000000); // B placeholder
                     fix_branch(ci_branch, call_indirect_handler);
                  }
                  // Padding to fill 28 bytes (7 instrs total, used 1)
                  emit_a64(0xD503201F);
                  emit_a64(0xD503201F);
                  emit_a64(0xD503201F);
                  emit_a64(0xD503201F);
                  emit_a64(0xD503201F);
                  emit_a64(0xD503201F);
               }
            }
            assert(code == _code_end);
         }
      }

      ~machine_code_writer() { _allocator.end_code<true>(_code_segment_base); }

      // ---- Prologue / Epilogue ----
      //
      // A64 prologue (max 13 instructions = 52 bytes for the loop case):
      //   stp x29, x30, [sp, #-16]!      // save frame pointer + link register
      //   mov x29, sp                     // set up frame pointer
      //   [zero-init locals loop or unrolled stp xzr, xzr]
      //
      // A64 epilogue (max 6 instructions = 24 bytes):
      //   ldp x9, xzr, [sp], #16         // pop return value if needed
      //   add sp, sp, #(local_count*16)   // clean up locals
      //   ldp x29, x30, [sp], #16         // restore frame
      //   mov x0, x9                      // move return value to x0 (if needed)
      //   ret                             // return

      static constexpr std::size_t max_prologue_size = 52;
      static constexpr std::size_t max_epilogue_size = 24;

      void emit_prologue(const func_type& /*ft*/, const guarded_vector<local_entry>& locals, uint32_t funcnum) {
         _ft = &_mod.types[_mod.functions[funcnum]];

         // A64 instructions are fixed 4 bytes each but we need more instructions
         // per WASM op than x86 (no variable-length encoding, no complex addressing modes).
         const std::size_t instruction_size_ratio_upper_bound =
            use_softfloat ? (Context::async_backtrace() ? 80 : 64) : 80;

         std::size_t code_size = max_prologue_size
                               + _mod.code[funcnum].size * instruction_size_ratio_upper_bound
                               + max_epilogue_size;
         _code_start = _allocator.alloc<unsigned char>(code_size);
         _code_end = _code_start + code_size;
         code = _code_start;

         start_function(code, funcnum + _mod.get_imported_functions_size());

         // stp x29, x30, [sp, #-16]!  -- save frame pointer and link register
         emit_a64(0xA9BF7BFD);

         // mov x29, sp  -- set up frame pointer
         emit_a64(0x910003FD);

         // Count locals
         uint32_t count = 0;
         for(uint32_t i = 0; i < locals.size(); ++i) {
            assert(uint64_t(count) + locals[i].count <= 0xFFFFFFFFu);
            count += locals[i].count;
         }
         _local_count = count;

         if (_local_count > 0) {
            if (_local_count > 8) {
               // Use a loop for many locals
               // mov w9, #_local_count
               emit_a64(0x52800000 | ((_local_count & 0xFFFF) << 5) | 9);
               if (_local_count > 0xFFFF) {
                  // movk w9, #(_local_count >> 16), lsl #16
                  emit_a64(0x72A00000 | (((_local_count >> 16) & 0xFFFF) << 5) | 9);
               }
               // loop:
               void* loop = code;
               // stp xzr, xzr, [sp, #-16]!  -- push 16 bytes of zeros
               emit_a64(0xA9BF7FFF);
               // subs w9, w9, #1
               emit_a64(0x71000529);
               // b.ne loop
               {
                  void* br = code;
                  emit_a64(0x54000000); // b.eq placeholder (will be patched to b.ne loop)
                  // Patch: b.ne = condition 0x1, offset to loop
                  intptr_t offset = (intptr_t)((char*)loop - (char*)br);
                  int32_t imm19 = (int32_t)(offset >> 2);
                  uint32_t instr = 0x54000001 | ((imm19 & 0x7FFFF) << 5);
                  memcpy(br, &instr, 4);
               }
            } else {
               // Unrolled: stp xzr, xzr, [sp, #-16]! for each local
               for (uint32_t i = 0; i < _local_count; ++i) {
                  // stp xzr, xzr, [sp, #-16]!
                  emit_a64(0xA9BF7FFF);
               }
            }
         }

         assert((char*)code <= (char*)_code_start + max_prologue_size);
      }

      void emit_epilogue(const func_type& ft, const guarded_vector<local_entry>& locals, uint32_t /*funcnum*/) {
#ifndef NDEBUG
         void* epilogue_start = code;
#endif
         if(ft.return_count != 0) {
            // ldp x9, xzr, [sp], #16  -- pop return value into x9
            // Encoding: LDP post-index, Xt1=x9, Xt2=xzr, Rn=sp, imm7=1 (16/8=2, but imm7 is offset/8)
            // LDP Xt1, Xt2, [Xn], #imm  = 1 01 0100011 imm7 Rt2 Rn Rt1
            // imm7 for #16 = 16/8 = 2 = 0b0000010
            emit_a64(0xA8C17FE9);
         }
         if (_local_count & 0xF0000000u) unimplemented();
         emit_multipop(_local_count);
         // ldp x29, x30, [sp], #16  -- restore frame pointer and link register
         emit_a64(0xA8C17BFD);
         if(ft.return_count != 0) {
            // mov x0, x9  -- return value in x0
            emit_a64(0xAA0903E0);
         }
         // ret
         emit_a64(0xD65F03C0);

         assert((char*)code <= (char*)epilogue_start + max_epilogue_size);
      }

      // ---- Control Flow ----

      void emit_unreachable() {
         // Emit an error handler inline (5 instructions = 20 bytes)
         emit_error_handler(&on_unreachable);
      }

      void emit_nop() {}

      void* emit_end() { return code; }

      void* emit_return(uint32_t depth_change) {
         return emit_br(depth_change);
      }

      void emit_block() {}

      void* emit_loop() { return code; }

      void* emit_if() {
         // Pop condition into x9
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // cbz w9, DEST  (branch if zero -- "else" or "end")
         void* result = code;
         emit_a64(0x34000009); // cbz w9, placeholder (will be fixed up)
         return result;
      }

      void* emit_else(void* if_loc) {
         void* result = emit_br(0);
         fix_branch(if_loc, code);
         return result;
      }

      void* emit_br(uint32_t depth_change) {
         // add sp, sp, #(depth_change * 16)
         emit_multipop(depth_change);
         // b DEST  (unconditional branch, placeholder)
         return emit_branch_target();
      }

      void* emit_br_if(uint32_t depth_change) {
         // ldp x9, xzr, [sp], #16  -- pop condition
         emit_a64(0xA8C17FE9);

         if(depth_change == 0u || depth_change == 0x80000001u) {
            // cbnz w9, DEST
            void* result = code;
            emit_a64(0x35000009); // cbnz w9, placeholder
            return result;
         } else {
            // cbz w9, SKIP
            void* skip = code;
            emit_a64(0x34000009); // cbz w9, placeholder
            // add sp, sp, #(depth_change * 16)
            emit_multipop(depth_change);
            // b DEST
            void* result = emit_branch_target();
            // SKIP:
            fix_branch(skip, code);
            return result;
         }
      }

      // ---- br_table (binary search, matching x86_64 interface) ----

      struct br_table_generator {
         void* emit_case(uint32_t depth_change) {
            while(true) {
               assert(!stack.empty() && "The parser is supposed to handle the number of elements in br_table.");
               auto [min, max, label] = stack.back();
               stack.pop_back();
               if (label) {
                  fix_branch(label, _this->code);
               }
               if (max - min > 1) {
                  uint32_t mid = min + (max - min)/2;
                  // cmp w9, #mid  -- compare index to midpoint
                  if (mid < 4096) {
                     // cmp w9, #mid
                     _this->emit_a64(0x7100001F | (mid << 10));
                  } else {
                     // mov w10, #mid
                     _this->emit_a64(0x52800000 | ((mid & 0xFFFF) << 5) | 10);
                     if (mid > 0xFFFF) {
                        _this->emit_a64(0x72A00000 | (((mid >> 16) & 0xFFFF) << 5) | 10);
                     }
                     // cmp w9, w10
                     _this->emit_a64(0x6B0A013F);
                  }
                  // b.hs MID  (unsigned higher or same)
                  void* mid_label = _this->code;
                  _this->emit_a64(0x54000002); // b.hs placeholder
                  stack.push_back({mid, max, mid_label});
                  stack.push_back({min, mid, nullptr});
               } else {
                  assert(min == static_cast<uint32_t>(_i));
                  _i++;
                  if (depth_change == 0u || depth_change == 0x80000001u) {
                     if(label) {
                        return label;
                     } else {
                        // b TARGET
                        return _this->emit_branch_target();
                     }
                  } else {
                     _this->emit_multipop(depth_change);
                     // b TARGET
                     return _this->emit_branch_target();
                  }
               }
            }
         }

         void* emit_default(uint32_t depth_change) {
            void* result = emit_case(depth_change);
            assert(stack.empty() && "unexpected default.");
            return result;
         }

         machine_code_writer* _this;
         int _i = 0;
         struct stack_item {
            uint32_t min;
            uint32_t max;
            void* branch_target = nullptr;
         };
         std::vector<stack_item> stack;
      };

      br_table_generator emit_br_table(uint32_t table_size) {
         // ldp x9, xzr, [sp], #16  -- pop index into w9
         emit_a64(0xA8C17FE9);
         return { this, 0, { {0, table_size+1, nullptr} } };
      }

      // ---- Function calls ----

      void register_call(void* ptr, uint32_t funcnum) {
         auto& vec = _function_relocations;
         if(funcnum >= vec.size()) vec.resize(funcnum + 1);
         if(void** addr = std::get_if<void*>(&vec[funcnum])) {
            fix_branch(ptr, *addr);
         } else {
            std::get<std::vector<void*>>(vec[funcnum]).push_back(ptr);
         }
      }

      void start_function(void* func_start, uint32_t funcnum) {
         auto& vec = _function_relocations;
         if(funcnum >= vec.size()) vec.resize(funcnum + 1);
         for(void* branch : std::get<std::vector<void*>>(vec[funcnum])) {
            fix_branch(branch, func_start);
         }
         vec[funcnum] = func_start;
      }

      void emit_call(const func_type& ft, uint32_t funcnum) {
         emit_check_call_depth();
         // bl TARGET  (branch with link, placeholder)
         void* branch = code;
         emit_a64(0x94000000); // BL #0 placeholder
         // Callee clobbers x0 with return value. Save it in x9, then
         // restore x0/x1 from callee-saved x21/x22 (set by trampoline).
         // mov x9, x0
         emit_a64(0xAA0003E9);
         // mov x0, x21  -- restore context
         emit_a64(0xAA1503E0);
         // mov x1, x22  -- restore linear_memory
         emit_a64(0xAA1603E1);
         emit_multipop(ft.param_types.size());
         register_call(branch, funcnum);
         if(ft.return_count != 0) {
            // stp x9, xzr, [sp, #-16]!  -- push return value (saved in x9)
            emit_a64(0xA9BF7FE9);
         }
         emit_check_call_depth_end();
      }

      void emit_call_indirect(const func_type& ft, uint32_t functypeidx) {
         emit_check_call_depth();
         auto& table = _mod.tables[0].table;
         functypeidx = _mod.type_aliases[functypeidx];
         // Pop the table index from the operand stack into x9
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // Bounds check: cmp x9, #table.size()
         // We need to handle table.size() which may be > 4095
         {
            uint32_t tsize = table.size();
            if (tsize < 4096) {
               // cmp x9, #tsize
               emit_a64(0xF100001F | (tsize << 10) | (9 << 5));
            } else {
               // mov x10, #tsize
               emit_a64(0xD2800000 | ((tsize & 0xFFFF) << 5) | 10);
               if (tsize > 0xFFFF) {
                  emit_a64(0xF2A00000 | (((tsize >> 16) & 0xFFFF) << 5) | 10);
               }
               // cmp x9, x10
               emit_a64(0xEB0A013F);
            }
         }
         // b.hs call_indirect_handler  (unsigned >=, i.e. out of range)
         {
            void* br = code;
            emit_a64(0x54000002); // b.hs placeholder
            fix_branch(br, call_indirect_handler);
         }
         // Load address of jump table into x10 (PC-relative, survives code copy)
         {
            void* adr_loc = emit_adr(10);
            fix_adr(adr_loc, jmp_table);
         }
         // Compute offset into jump table: x9 = x9 * _table_element_size
         // mov w11, #_table_element_size
         assert(_table_element_size <= 0xFFFF);
         emit_a64(0x52800000 | ((_table_element_size & 0xFFFF) << 5) | 11);
         // mul x9, x9, x11  (MADD x9, x9, x11, xzr)
         // MUL Xd, Xn, Xm = 0x9B007C00 | (Rm<<16) | (Rn<<5) | Rd
         emit_a64(0x9B007C00 | (11 << 16) | (9 << 5) | 9);
         // add x9, x10, x9  -- x9 = jmp_table + offset
         emit_a64(0x8B000000 | (9 << 16) | (10 << 5) | 9);
         // Load the expected type index into w9 for the jump table comparison
         // mov w9, #functypeidx  -- actual type to compare against
         emit_a64(0x52800000 | ((functypeidx & 0xFFFF) << 5) | 12);
         if (functypeidx > 0xFFFF) {
            emit_a64(0x72A00000 | (((functypeidx >> 16) & 0xFFFF) << 5) | 12);
         }
         // The jump table entries expect the type in w9 and then branch.
         // We need to put the type in w9 but x9 currently holds the table entry addr.
         // Rearrange: put table entry in x10, type in w9
         // mov x10, x9
         emit_a64(0xAA0903EA);
         // mov w9, w12
         emit_a64(0x2A0C03E9);
         // blr x10  -- call table entry (which will compare w9 and branch to function)
         // BLR Xn = 0xD63F0000 | (Xn<<5)
         emit_a64(0xD63F0000 | (10 << 5));
         // Callee clobbers x0 with return value. Save it in x9, then
         // restore x0/x1 from callee-saved x21/x22 (set by trampoline).
         // mov x9, x0
         emit_a64(0xAA0003E9);
         // mov x0, x21  -- restore context
         emit_a64(0xAA1503E0);
         // mov x1, x22  -- restore linear_memory
         emit_a64(0xAA1603E1);
         // The jump table entry will either branch to the target function or error.
         // After the call returns, clean up params and push result.
         emit_multipop(ft.param_types.size());
         if(ft.return_count != 0) {
            // stp x9, xzr, [sp, #-16]!  -- push return value (saved in x9)
            emit_a64(0xA9BF7FE9);
         }
         emit_check_call_depth_end();
      }

      // ---- Local / Global access ----

      void emit_drop() {
         // ldp x9, xzr, [sp], #16  -- pop and discard
         emit_a64(0xA8C17FE9);
      }

      void emit_select() {
         // Pop condition (x11), then val2 (x10), peek val1 on stack
         // ldp x11, xzr, [sp], #16  -- condition
         emit_a64(0xA8C17FEB);
         // ldp x10, xzr, [sp], #16  -- val2 (false value)
         emit_a64(0xA8C17FEA);
         // val1 is at [sp] (true value)
         // ldr x9, [sp]  -- peek val1
         emit_a64(0xF94003E9);
         // cmp w11, #0
         emit_a64(0x7100017F);
         // csel x9, x9, x10, ne  -- if condition != 0, keep val1, else val2
         // CSEL Xd, Xn, Xm, cond = 0x9A800000 | (Rm<<16) | (cond<<12) | (Rn<<5) | Rd
         // NE = 0x1
         emit_a64(0x9A8A1129);
         // str x9, [sp]  -- overwrite top of stack with result
         emit_a64(0xF90003E9);
      }

      void emit_get_local(uint32_t local_idx) {
         // Load from the frame and push onto operand stack
         if (local_idx < _ft->param_types.size()) {
            // Parameters are at positive offsets from x29
            // Offset = 16 * (nparams - local_idx + 1) but params are above the frame
            // param at [x29 + 16*(nparams - local_idx)]
            // Actually, params are pushed by caller before the call.
            // After stp x29, x30, [sp, #-16]!, x29 = sp.
            // params are above: [x29 + 16] = last param pushed (param[nparams-1])
            // Actually from the comment in the header:
            //   param0 <--- x29 + 16*(nparams)   [first param, pushed first, highest addr]
            //   paramN <--- x29 + 16*(1)          [last param, pushed last]
            //   saved x29/x30 <--- x29
            //
            // So local_idx 0 (first param) is at x29 + 16*nparams
            // local_idx k is at x29 + 16*(nparams - k)
            int32_t offset = 16 * (static_cast<int32_t>(_ft->param_types.size()) - static_cast<int32_t>(local_idx));
            // ldr x9, [x29, #offset]
            emit_ldr_fp_offset(9, offset);
         } else {
            // Locals are at negative offsets from x29
            // local at [x29 - 16*(local_idx - nparams + 1)]
            int32_t offset = -16 * (static_cast<int32_t>(local_idx) - static_cast<int32_t>(_ft->param_types.size()) + 1);
            // ldr x9, [x29, #offset]
            emit_ldr_fp_offset(9, offset);
         }
         // stp x9, xzr, [sp, #-16]!  -- push
         emit_a64(0xA9BF7FE9);
      }

      void emit_set_local(uint32_t local_idx) {
         // Pop from operand stack and store to frame
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         if (local_idx < _ft->param_types.size()) {
            int32_t offset = 16 * (static_cast<int32_t>(_ft->param_types.size()) - static_cast<int32_t>(local_idx));
            emit_str_fp_offset(9, offset);
         } else {
            int32_t offset = -16 * (static_cast<int32_t>(local_idx) - static_cast<int32_t>(_ft->param_types.size()) + 1);
            emit_str_fp_offset(9, offset);
         }
      }

      void emit_tee_local(uint32_t local_idx) {
         // Peek top of stack (don't pop) and store to frame
         // ldr x9, [sp]
         emit_a64(0xF94003E9);
         if (local_idx < _ft->param_types.size()) {
            int32_t offset = 16 * (static_cast<int32_t>(_ft->param_types.size()) - static_cast<int32_t>(local_idx));
            emit_str_fp_offset(9, offset);
         } else {
            int32_t offset = -16 * (static_cast<int32_t>(local_idx) - static_cast<int32_t>(_ft->param_types.size()) + 1);
            emit_str_fp_offset(9, offset);
         }
      }

      void emit_get_global(uint32_t globalidx) {
         auto& gl = _mod.globals[globalidx];
         emit_setup_backtrace();
         // Save x0, x1 on stack
         // stp x0, x1, [sp, #-16]!
         emit_a64(0xA9BF07E0);
         // mov w1, #globalidx  (second arg)
         emit_a64(0x52800000 | ((globalidx & 0xFFFF) << 5) | 1);
         if (globalidx > 0xFFFF) {
            emit_a64(0x72A00000 | (((globalidx >> 16) & 0xFFFF) << 5) | 1);
         }
         // Load address of get_global_xxx into x9
         {
            uint64_t addr;
            switch(gl.type.content_type) {
               case types::i32: addr = reinterpret_cast<uint64_t>(&get_global_i32); break;
               case types::i64: addr = reinterpret_cast<uint64_t>(&get_global_i64); break;
               case types::f32: addr = reinterpret_cast<uint64_t>(&get_global_f32); break;
               case types::f64: addr = reinterpret_cast<uint64_t>(&get_global_f64); break;
               default: unimplemented(); addr = 0; break;
            }
            emit_mov_imm64(9, addr);
         }
         // blr x9
         emit_a64(0xD63F0120);
         // Save result in x9
         // mov x9, x0
         emit_a64(0xAA0003E9);
         // Restore x0, x1
         // ldp x0, x1, [sp], #16
         emit_a64(0xA8C107E0);
         emit_restore_backtrace();
         // Push result
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_set_global(uint32_t globalidx) {
         auto& gl = _mod.globals[globalidx];
         // Pop value into x2 (third argument)
         // ldp x2, xzr, [sp], #16
         emit_a64(0xA8C17FE2);
         emit_setup_backtrace();
         // Save x0, x1 on stack
         // stp x0, x1, [sp, #-16]!
         emit_a64(0xA9BF07E0);
         // mov w1, #globalidx  (second arg)
         emit_a64(0x52800000 | ((globalidx & 0xFFFF) << 5) | 1);
         if (globalidx > 0xFFFF) {
            emit_a64(0x72A00000 | (((globalidx >> 16) & 0xFFFF) << 5) | 1);
         }
         // Load address of set_global_xxx into x9
         {
            uint64_t addr;
            switch(gl.type.content_type) {
               case types::i32: addr = reinterpret_cast<uint64_t>(&set_global_i32); break;
               case types::i64: addr = reinterpret_cast<uint64_t>(&set_global_i64); break;
               case types::f32: addr = reinterpret_cast<uint64_t>(&set_global_f32); break;
               case types::f64: addr = reinterpret_cast<uint64_t>(&set_global_f64); break;
               default: unimplemented(); addr = 0; break;
            }
            emit_mov_imm64(9, addr);
         }
         // blr x9
         emit_a64(0xD63F0120);
         // Restore x0, x1
         // ldp x0, x1, [sp], #16
         emit_a64(0xA8C107E0);
         emit_restore_backtrace();
      }

      // ---- Memory ----

      void emit_i32_load(uint32_t /*alignment*/, uint32_t offset) {
         // LDR Wt, [Xn, Xm] = 0xB8606800 -- loads 32 bits, zero-extended to 64
         emit_load_impl(offset, 0xB8606800);
      }

      void emit_i64_load(uint32_t /*alignment*/, uint32_t offset) {
         // LDR Xt, [Xn, Xm] = 0xF8606800
         emit_load_impl(offset, 0xF8606800);
      }

      void emit_f32_load(uint32_t /*alignment*/, uint32_t offset) {
         // Same as i32_load (bit pattern), LDR Wt, [Xn, Xm]
         emit_load_impl(offset, 0xB8606800);
      }

      void emit_f64_load(uint32_t /*alignment*/, uint32_t offset) {
         // Same as i64_load
         emit_load_impl(offset, 0xF8606800);
      }

      void emit_i32_load8_s(uint32_t /*alignment*/, uint32_t offset) {
         // LDRSB Wt, [Xn, Xm] = 0x38E06800 -- sign-extend byte to 32 bits (upper 32 zeroed)
         // Note: LDRSB Xt (0x38A06800) would sign-extend to 64 bits, leaving dirty upper bits
         // that break i64.extend_u_i32 (which is a no-op on x86_64 but must zero-extend here).
         emit_load_impl(offset, 0x38E06800);
      }

      void emit_i32_load16_s(uint32_t /*alignment*/, uint32_t offset) {
         // LDRSH Wt, [Xn, Xm] = 0x78E06800 -- sign-extend halfword to 32 bits (upper 32 zeroed)
         // Note: LDRSH Xt (0x78A06800) would sign-extend to 64 bits, same issue as above.
         emit_load_impl(offset, 0x78E06800);
      }

      void emit_i32_load8_u(uint32_t /*alignment*/, uint32_t offset) {
         // LDRB Wt, [Xn, Xm] = 0x38606800
         emit_load_impl(offset, 0x38606800);
      }

      void emit_i32_load16_u(uint32_t /*alignment*/, uint32_t offset) {
         // LDRH Wt, [Xn, Xm] = 0x78606800
         emit_load_impl(offset, 0x78606800);
      }

      void emit_i64_load8_s(uint32_t /*alignment*/, uint32_t offset) {
         // LDRSB Xt, [Xn, Xm] = 0x38A06800
         emit_load_impl(offset, 0x38A06800);
      }

      void emit_i64_load16_s(uint32_t /*alignment*/, uint32_t offset) {
         // LDRSH Xt, [Xn, Xm] = 0x78A06800
         emit_load_impl(offset, 0x78A06800);
      }

      void emit_i64_load32_s(uint32_t /*alignment*/, uint32_t offset) {
         // LDRSW Xt, [Xn, Xm] = 0xB8A06800
         emit_load_impl(offset, 0xB8A06800);
      }

      void emit_i64_load8_u(uint32_t /*alignment*/, uint32_t offset) {
         // LDRB Wt, [Xn, Xm] = 0x38606800
         emit_load_impl(offset, 0x38606800);
      }

      void emit_i64_load16_u(uint32_t /*alignment*/, uint32_t offset) {
         // LDRH Wt, [Xn, Xm] = 0x78606800
         emit_load_impl(offset, 0x78606800);
      }

      void emit_i64_load32_u(uint32_t /*alignment*/, uint32_t offset) {
         // LDR Wt, [Xn, Xm] = 0xB8606800  (32-bit load, zero-extended)
         emit_load_impl(offset, 0xB8606800);
      }

      void emit_i32_store(uint32_t /*alignment*/, uint32_t offset) {
         // STR Wt, [Xn, Xm] = 0xB8206800
         emit_store_impl(offset, 0xB8206800);
      }

      void emit_i64_store(uint32_t /*alignment*/, uint32_t offset) {
         // STR Xt, [Xn, Xm] = 0xF8206800
         emit_store_impl(offset, 0xF8206800);
      }

      void emit_f32_store(uint32_t /*alignment*/, uint32_t offset) {
         // STR Wt, [Xn, Xm] = 0xB8206800
         emit_store_impl(offset, 0xB8206800);
      }

      void emit_f64_store(uint32_t /*alignment*/, uint32_t offset) {
         // STR Xt, [Xn, Xm] = 0xF8206800
         emit_store_impl(offset, 0xF8206800);
      }

      void emit_i32_store8(uint32_t /*alignment*/, uint32_t offset) {
         // STRB Wt, [Xn, Xm] = 0x38206800
         emit_store_impl(offset, 0x38206800);
      }

      void emit_i32_store16(uint32_t /*alignment*/, uint32_t offset) {
         // STRH Wt, [Xn, Xm] = 0x78206800
         emit_store_impl(offset, 0x78206800);
      }

      void emit_i64_store8(uint32_t /*alignment*/, uint32_t offset) {
         // STRB Wt, [Xn, Xm] = 0x38206800
         emit_store_impl(offset, 0x38206800);
      }

      void emit_i64_store16(uint32_t /*alignment*/, uint32_t offset) {
         // STRH Wt, [Xn, Xm] = 0x78206800
         emit_store_impl(offset, 0x78206800);
      }

      void emit_i64_store32(uint32_t /*alignment*/, uint32_t offset) {
         // STR Wt, [Xn, Xm] = 0xB8206800
         emit_store_impl(offset, 0xB8206800);
      }

      // ---- Memory size ----

      void emit_current_memory() {
         emit_setup_backtrace();
         // stp x0, x1, [sp, #-16]!
         emit_a64(0xA9BF07E0);
         // Load address of current_memory
         emit_mov_imm64(9, reinterpret_cast<uint64_t>(&current_memory));
         // blr x9
         emit_a64(0xD63F0120);
         // mov x9, x0  -- save result
         emit_a64(0xAA0003E9);
         // ldp x0, x1, [sp], #16
         emit_a64(0xA8C107E0);
         emit_restore_backtrace();
         // stp x9, xzr, [sp, #-16]!  -- push result
         emit_a64(0xA9BF7FE9);
      }

      void emit_grow_memory() {
         // Pop pages argument
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         emit_setup_backtrace();
         // stp x0, x1, [sp, #-16]!
         emit_a64(0xA9BF07E0);
         // mov w1, w9  -- pages as second arg
         emit_a64(0x2A0903E1);
         // Load address of grow_memory
         emit_mov_imm64(9, reinterpret_cast<uint64_t>(&grow_memory));
         // blr x9
         emit_a64(0xD63F0120);
         // mov x9, x0  -- save result
         emit_a64(0xAA0003E9);
         // ldp x0, x1, [sp], #16
         emit_a64(0xA8C107E0);
         emit_restore_backtrace();
         // stp x9, xzr, [sp, #-16]!  -- push result
         emit_a64(0xA9BF7FE9);
      }

      // ---- Constants ----

      void emit_i32_const(uint32_t value) {
         // movz w9, #(value & 0xFFFF)
         emit_a64(0x52800000 | ((value & 0xFFFF) << 5) | 9);
         if (value > 0xFFFF) {
            // movk w9, #(value >> 16), lsl #16
            emit_a64(0x72A00000 | (((value >> 16) & 0xFFFF) << 5) | 9);
         }
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_const(uint64_t value) {
         emit_mov_imm64(9, value);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_f32_const(float value) {
         uint32_t bits;
         memcpy(&bits, &value, sizeof(bits));
         emit_i32_const(bits);
      }

      void emit_f64_const(double value) {
         uint64_t bits;
         memcpy(&bits, &value, sizeof(bits));
         emit_i64_const(bits);
      }

      // ---- i32 comparison ----

      void emit_i32_eqz() {
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // cmp w9, #0
         emit_a64(0x7100013F);
         // cset x9, eq  (CSINC x9, xzr, xzr, ne)
         // EQ: cond_inv = NE = 0x1
         emit_a64(0x9A9F17E9);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i32_eq() {
         // Condition: EQ, inverted = NE (0x1)
         emit_i32_relop(0x1);
      }

      void emit_i32_ne() {
         // Condition: NE, inverted = EQ (0x0)
         emit_i32_relop(0x0);
      }

      void emit_i32_lt_s() {
         // Condition: LT (signed), inverted = GE (0xA)
         emit_i32_relop(0xA);
      }

      void emit_i32_lt_u() {
         // Condition: LO (unsigned LT), inverted = HS (0x2)
         emit_i32_relop(0x2);
      }

      void emit_i32_gt_s() {
         // Condition: GT (signed), inverted = LE (0xD)
         emit_i32_relop(0xD);
      }

      void emit_i32_gt_u() {
         // Condition: HI (unsigned GT), inverted = LS (0x9)
         emit_i32_relop(0x9);
      }

      void emit_i32_le_s() {
         // Condition: LE (signed), inverted = GT (0xC)
         emit_i32_relop(0xC);
      }

      void emit_i32_le_u() {
         // Condition: LS (unsigned LE), inverted = HI (0x8)
         emit_i32_relop(0x8);
      }

      void emit_i32_ge_s() {
         // Condition: GE (signed), inverted = LT (0xB)
         emit_i32_relop(0xB);
      }

      void emit_i32_ge_u() {
         // Condition: HS (unsigned GE), inverted = LO (0x3)
         emit_i32_relop(0x3);
      }

      // ---- i64 comparison ----

      void emit_i64_eqz() {
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // cmp x9, #0
         emit_a64(0xF100013F);
         // cset x9, eq
         emit_a64(0x9A9F17E9);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_eq() {
         emit_i64_relop(0x1);  // EQ, inv=NE
      }

      void emit_i64_ne() {
         emit_i64_relop(0x0);  // NE, inv=EQ
      }

      void emit_i64_lt_s() {
         emit_i64_relop(0xA);  // LT, inv=GE
      }

      void emit_i64_lt_u() {
         emit_i64_relop(0x2);  // LO, inv=HS
      }

      void emit_i64_gt_s() {
         emit_i64_relop(0xD);  // GT, inv=LE
      }

      void emit_i64_gt_u() {
         emit_i64_relop(0x9);  // HI, inv=LS
      }

      void emit_i64_le_s() {
         emit_i64_relop(0xC);  // LE, inv=GT
      }

      void emit_i64_le_u() {
         emit_i64_relop(0x8);  // LS, inv=HI
      }

      void emit_i64_ge_s() {
         emit_i64_relop(0xB);  // GE, inv=LT
      }

      void emit_i64_ge_u() {
         emit_i64_relop(0x3);  // HS, inv=LO
      }

      // ---- Softfloat support (same as x86_64) ----

#ifdef CORE_NET_VM_SOFTFLOAT
      static uint64_t adapt_result(bool val) {
         return val ? 1 : 0;
      }
      static uint64_t adapt_result(float32_t val) {
         uint64_t result = 0;
         std::memcpy(&result, &val, sizeof(float32_t));
         return result;
      }
      static float64_t adapt_result(float64_t val) {
         return val;
      }

      template<auto F>
      static auto adapt_f32_unop(float32_t arg) {
         return ::to_softfloat32(static_cast<decltype(F)>(F)(::from_softfloat32(arg)));
      }
      template<auto F>
      static auto adapt_f32_binop(float32_t lhs, float32_t rhs) {
         return ::to_softfloat32(static_cast<decltype(F)>(F)(::from_softfloat32(lhs), ::from_softfloat32(rhs)));
      }
      template<auto F>
      static auto adapt_f32_cmp(float32_t lhs, float32_t rhs) {
         return adapt_result(static_cast<decltype(F)>(F)(::from_softfloat32(lhs), ::from_softfloat32(rhs)));
      }

      template<auto F>
      static auto adapt_f64_unop(float64_t arg) {
         return ::to_softfloat64(static_cast<decltype(F)>(F)(::from_softfloat64(arg)));
      }
      template<auto F>
      static auto adapt_f64_binop(float64_t lhs, float64_t rhs) {
         return ::to_softfloat64(static_cast<decltype(F)>(F)(::from_softfloat64(lhs), ::from_softfloat64(rhs)));
      }
      template<auto F>
      static auto adapt_f64_cmp(float64_t lhs, float64_t rhs) {
         return adapt_result(static_cast<decltype(F)>(F)(::from_softfloat64(lhs), ::from_softfloat64(rhs)));
      }

      static float32_t to_softfloat(float arg) { return ::to_softfloat32(arg); }
      static float64_t to_softfloat(double arg) { return ::to_softfloat64(arg); }
      template<typename T>
      static T to_softfloat(T arg) { return arg; }
      static float from_softfloat(float32_t arg) { return ::from_softfloat32(arg); }
      static double from_softfloat(float64_t arg) { return ::from_softfloat64(arg); }
      template<typename T>
      static T from_softfloat(T arg) { return arg; }

      template<typename T>
      using softfloat_arg_t = decltype(to_softfloat(T{}));

      template<auto F, typename T>
      static auto adapt_float_convert(softfloat_arg_t<T> arg) {
         auto result = to_softfloat(F(from_softfloat(arg)));
         if constexpr (sizeof(result) == 4 && sizeof(T) == 8) {
            uint64_t buffer = 0;
            std::memcpy(&buffer, &result, sizeof(result));
            return buffer;
         } else {
            return result;
         }
      }

      template<auto F, typename R, typename T>
      static constexpr auto choose_unop(R(*)(T)) {
         if constexpr(sizeof(R) == 4 && sizeof(T) == 8) {
            return static_cast<uint64_t(*)(softfloat_arg_t<T>)>(&adapt_float_convert<F, T>);
         } else {
            return static_cast<softfloat_arg_t<R>(*)(softfloat_arg_t<T>)>(&adapt_float_convert<F, T>);
         }
      }

      template<auto F>
      constexpr auto choose_fn() {
         if constexpr (use_softfloat) {
            if constexpr (std::is_same_v<decltype(F), float(*)(float)>) {
               return &adapt_f32_unop<F>;
            } else if constexpr(std::is_same_v<decltype(F), float(*)(float,float)>) {
               return &adapt_f32_binop<F>;
            } else if constexpr(std::is_same_v<decltype(F), bool(*)(float,float)>) {
               return &adapt_f32_cmp<F>;
            } else if constexpr (std::is_same_v<decltype(F), double(*)(double)>) {
               return &adapt_f64_unop<F>;
            } else if constexpr(std::is_same_v<decltype(F), double(*)(double,double)>) {
               return &adapt_f64_binop<F>;
            } else if constexpr(std::is_same_v<decltype(F), bool(*)(double,double)>) {
               return &adapt_f64_cmp<F>;
            } else {
               return choose_unop<F>(F);
            }
         } else {
            return nullptr;
         }
      }

      template<auto F, typename R, typename... A>
      static R softfloat_trap_fn(A... a) {
         R result;
         longjmp_on_exception([&]() {
            result = F(a...);
         });
         return result;
      }

      template<auto F, typename R, typename... A>
      static constexpr auto make_softfloat_trap_fn(R(*)(A...)) -> R(*)(A...) {
         return softfloat_trap_fn<F, R, A...>;
      }

      template<auto F>
      static constexpr decltype(auto) softfloat_trap() {
         return *make_softfloat_trap_fn<F>(F);
      }

   #define CHOOSE_FN(name) choose_fn<&name>()
#else
      using float32_t = float;
      using float64_t = double;
   #define CHOOSE_FN(name) nullptr
#endif

      // ---- f32 comparison ----

      void emit_f32_eq() {
         if constexpr (use_softfloat) emit_f32_relop(CHOOSE_FN(_core_net_f32_eq), false, false);
         else unimplemented();
      }

      void emit_f32_ne() {
         if constexpr (use_softfloat) emit_f32_relop(CHOOSE_FN(_core_net_f32_eq), false, true);
         else unimplemented();
      }

      void emit_f32_lt() {
         if constexpr (use_softfloat) emit_f32_relop(CHOOSE_FN(_core_net_f32_lt), false, false);
         else unimplemented();
      }

      void emit_f32_gt() {
         if constexpr (use_softfloat) emit_f32_relop(CHOOSE_FN(_core_net_f32_lt), true, false);
         else unimplemented();
      }

      void emit_f32_le() {
         if constexpr (use_softfloat) emit_f32_relop(CHOOSE_FN(_core_net_f32_le), false, false);
         else unimplemented();
      }

      void emit_f32_ge() {
         if constexpr (use_softfloat) emit_f32_relop(CHOOSE_FN(_core_net_f32_le), true, false);
         else unimplemented();
      }

      // ---- f64 comparison ----

      void emit_f64_eq() {
         if constexpr (use_softfloat) emit_f64_relop(CHOOSE_FN(_core_net_f64_eq), false, false);
         else unimplemented();
      }

      void emit_f64_ne() {
         if constexpr (use_softfloat) emit_f64_relop(CHOOSE_FN(_core_net_f64_eq), false, true);
         else unimplemented();
      }

      void emit_f64_lt() {
         if constexpr (use_softfloat) emit_f64_relop(CHOOSE_FN(_core_net_f64_lt), false, false);
         else unimplemented();
      }

      void emit_f64_gt() {
         if constexpr (use_softfloat) emit_f64_relop(CHOOSE_FN(_core_net_f64_lt), true, false);
         else unimplemented();
      }

      void emit_f64_le() {
         if constexpr (use_softfloat) emit_f64_relop(CHOOSE_FN(_core_net_f64_le), false, false);
         else unimplemented();
      }

      void emit_f64_ge() {
         if constexpr (use_softfloat) emit_f64_relop(CHOOSE_FN(_core_net_f64_le), true, false);
         else unimplemented();
      }

      // ---- i32 unary ops ----

      void emit_i32_clz() {
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // CLZ w9, w9  = 0x5AC01000 | (9<<5) | 9
         emit_a64(0x5AC01129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i32_ctz() {
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // RBIT w9, w9  = 0x5AC00000 | (9<<5) | 9
         emit_a64(0x5AC00129);
         // CLZ w9, w9
         emit_a64(0x5AC01129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i32_popcnt() {
         // AArch64 doesn't have a scalar popcnt instruction.
         // Use FMOV + CNT + ADDV approach via NEON, or a software loop.
         // Simplest approach: use the NEON CNT instruction.
         // fmov s0, w9 -> cnt v0.8b, v0.8b -> addv b0, v0.8b -> umov w9, v0.b[0]
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // fmov s0, w9 = 0x1E270000 | (9 << 5) | 0
         emit_a64(0x1E270120);
         // cnt v0.8b, v0.8b = 0x0E205800 | (0 << 5) | 0
         emit_a64(0x0E205800);
         // addv b0, v0.8b = 0x0E31B800 | (0 << 5) | 0
         emit_a64(0x0E31B800);
         // umov w9, v0.b[0] = 0x0E013C00 | 9
         emit_a64(0x0E013C09);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      // ---- i32 binary ops ----

      void emit_i32_add() {
         // ADD Wd, Wn, Wm = 0x0B000000
         emit_i32_binop(0x0B000000);
      }

      void emit_i32_sub() {
         // SUB Wd, Wn, Wm = 0x4B000000
         emit_i32_binop(0x4B000000);
      }

      void emit_i32_mul() {
         // MUL Wd, Wn, Wm = 0x1B007C00
         emit_i32_binop(0x1B007C00);
      }

      void emit_i32_div_s() {
         // SDIV Wd, Wn, Wm = 0x1AC00C00
         emit_i32_binop(0x1AC00C00);
      }

      void emit_i32_div_u() {
         // UDIV Wd, Wn, Wm = 0x1AC00800
         emit_i32_binop(0x1AC00800);
      }

      void emit_i32_rem_s() {
         // Pop rhs (x10), pop lhs (x9)
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // Handle -1 divisor to avoid overflow: if rhs == -1, result = 0
         // cmn w10, #1  (compare w10 with -1, i.e. add w10, 1 and check Z)
         emit_a64(0x3100055F);
         // b.ne NORMAL
         void* normal = code;
         emit_a64(0x54000001); // b.ne placeholder
         // Result is 0 for rem -1
         // mov w9, #0
         emit_a64(0x52800009);
         // b END
         void* end = code;
         emit_a64(0x14000000); // b placeholder
         // NORMAL:
         fix_branch(normal, code);
         // sdiv w11, w9, w10 = 0x1AC00C00 | (10<<16) | (9<<5) | 11
         emit_a64(0x1AC00C00 | (10 << 16) | (9 << 5) | 11);
         // msub w9, w11, w10, w9  = w9 = w9 - w11*w10
         // MSUB Wd, Wn, Wm, Wa = 0x1B008000 | (Wm<<16) | (Wa<<10) | (Wn<<5) | Wd
         emit_a64(0x1B008000 | (10 << 16) | (9 << 10) | (11 << 5) | 9);
         // END:
         fix_branch(end, code);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i32_rem_u() {
         // Pop rhs (x10), pop lhs (x9)
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // udiv w11, w9, w10
         emit_a64(0x1AC00800 | (10 << 16) | (9 << 5) | 11);
         // msub w9, w11, w10, w9
         emit_a64(0x1B008000 | (10 << 16) | (9 << 10) | (11 << 5) | 9);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i32_and() {
         // AND Wd, Wn, Wm = 0x0A000000
         emit_i32_binop(0x0A000000);
      }

      void emit_i32_or() {
         // ORR Wd, Wn, Wm = 0x2A000000
         emit_i32_binop(0x2A000000);
      }

      void emit_i32_xor() {
         // EOR Wd, Wn, Wm = 0x4A000000
         emit_i32_binop(0x4A000000);
      }

      void emit_i32_shl() {
         // LSLV Wd, Wn, Wm = 0x1AC02000
         emit_i32_binop(0x1AC02000);
      }

      void emit_i32_shr_s() {
         // ASRV Wd, Wn, Wm = 0x1AC02800
         emit_i32_binop(0x1AC02800);
      }

      void emit_i32_shr_u() {
         // LSRV Wd, Wn, Wm = 0x1AC02400
         emit_i32_binop(0x1AC02400);
      }

      void emit_i32_rotl() {
         // AArch64 has RORV but no ROLV. rotl(x, n) = ror(x, 32-n)
         // Pop rhs (x10), pop lhs (x9)
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // neg w10, w10  (w10 = 32 - w10 mod 32, but neg works because ror masks to 5 bits)
         // Actually: sub w10, wzr, w10 = 0x4B0A03EA
         emit_a64(0x4B0A03EA);
         // ror w9, w9, w10
         // RORV Wd, Wn, Wm = 0x1AC02C00 | (Wm<<16) | (Wn<<5) | Wd
         emit_a64(0x1AC02C00 | (10 << 16) | (9 << 5) | 9);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i32_rotr() {
         // RORV Wd, Wn, Wm = 0x1AC02C00
         emit_i32_binop(0x1AC02C00);
      }

      // ---- i64 unary ops ----

      void emit_i64_clz() {
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // CLZ x9, x9  = 0xDAC01000 | (9<<5) | 9
         emit_a64(0xDAC01129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_ctz() {
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // RBIT x9, x9  = 0xDAC00000 | (9<<5) | 9
         emit_a64(0xDAC00129);
         // CLZ x9, x9
         emit_a64(0xDAC01129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_popcnt() {
         // ldp x9, xzr, [sp], #16
         emit_a64(0xA8C17FE9);
         // fmov d0, x9 = 0x9E670120
         emit_a64(0x9E670120);
         // cnt v0.8b, v0.8b
         emit_a64(0x0E205800);
         // addv b0, v0.8b
         emit_a64(0x0E31B800);
         // umov w9, v0.b[0]
         emit_a64(0x0E013C09);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      // ---- i64 binary ops ----

      void emit_i64_add() {
         // ADD Xd, Xn, Xm = 0x8B000000
         emit_i64_binop(0x8B000000);
      }

      void emit_i64_sub() {
         // SUB Xd, Xn, Xm = 0xCB000000
         emit_i64_binop(0xCB000000);
      }

      void emit_i64_mul() {
         // MUL Xd, Xn, Xm = 0x9B007C00
         emit_i64_binop(0x9B007C00);
      }

      void emit_i64_div_s() {
         // SDIV Xd, Xn, Xm = 0x9AC00C00
         emit_i64_binop(0x9AC00C00);
      }

      void emit_i64_div_u() {
         // UDIV Xd, Xn, Xm = 0x9AC00800
         emit_i64_binop(0x9AC00800);
      }

      void emit_i64_rem_s() {
         // Pop rhs (x10), pop lhs (x9)
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // Handle -1 divisor: if rhs == -1, result = 0
         // cmn x10, #1
         emit_a64(0xB100055F);
         // b.ne NORMAL
         void* normal = code;
         emit_a64(0x54000001); // b.ne placeholder
         // mov x9, #0
         emit_a64(0xD2800009);
         // b END
         void* end = code;
         emit_a64(0x14000000); // b placeholder
         // NORMAL:
         fix_branch(normal, code);
         // sdiv x11, x9, x10
         emit_a64(0x9AC00C00 | (10 << 16) | (9 << 5) | 11);
         // msub x9, x11, x10, x9
         emit_a64(0x9B008000 | (10 << 16) | (9 << 10) | (11 << 5) | 9);
         // END:
         fix_branch(end, code);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_rem_u() {
         // Pop rhs (x10), pop lhs (x9)
         emit_a64(0xA8C17FEA);
         emit_a64(0xA8C17FE9);
         // udiv x11, x9, x10
         emit_a64(0x9AC00800 | (10 << 16) | (9 << 5) | 11);
         // msub x9, x11, x10, x9
         emit_a64(0x9B008000 | (10 << 16) | (9 << 10) | (11 << 5) | 9);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_and() {
         // AND Xd, Xn, Xm = 0x8A000000
         emit_i64_binop(0x8A000000);
      }

      void emit_i64_or() {
         // ORR Xd, Xn, Xm = 0xAA000000
         emit_i64_binop(0xAA000000);
      }

      void emit_i64_xor() {
         // EOR Xd, Xn, Xm = 0xCA000000
         emit_i64_binop(0xCA000000);
      }

      void emit_i64_shl() {
         // LSLV Xd, Xn, Xm = 0x9AC02000
         emit_i64_binop(0x9AC02000);
      }

      void emit_i64_shr_s() {
         // ASRV Xd, Xn, Xm = 0x9AC02800
         emit_i64_binop(0x9AC02800);
      }

      void emit_i64_shr_u() {
         // LSRV Xd, Xn, Xm = 0x9AC02400
         emit_i64_binop(0x9AC02400);
      }

      void emit_i64_rotl() {
         // rotl(x, n) = ror(x, 64-n)
         emit_a64(0xA8C17FEA); // pop rhs into x10
         emit_a64(0xA8C17FE9); // pop lhs into x9
         // neg x10, x10  (sub x10, xzr, x10)
         emit_a64(0xCB0A03EA);
         // ror x9, x9, x10
         emit_a64(0x9AC02C00 | (10 << 16) | (9 << 5) | 9);
         // push result
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_rotr() {
         // RORV Xd, Xn, Xm = 0x9AC02C00
         emit_i64_binop(0x9AC02C00);
      }

      // ---- f32 unary ops ----

      void emit_f32_abs() {
         // Pop, clear sign bit, push
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // and w9, w9, #0x7FFFFFFF
         // A64 logical immediate: 0x7FFFFFFF = 31 ones starting at bit 0
         // AND Wd, Wn, #imm: 0x12000000 | (immr<<16) | (imms<<10) | (Rn<<5) | Rd
         // For 0x7FFFFFFF (32-bit): N=0, immr=0, imms=30(0x1E)
         emit_a64(0x12007929);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_f32_neg() {
         // Pop, flip sign bit, push
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // eor w9, w9, #0x80000000
         // A64 logical immediate: 0x80000000 = single 1 at bit 31
         // EOR Wd, Wn, #imm: 0x52000000 | (immr<<16) | (imms<<10) | (Rn<<5) | Rd
         // For 0x80000000 (32-bit): N=0, immr=1, imms=0
         // 0x52000000 | (1<<16) | (0<<10) | (9<<5) | 9 = 0x52010129
         emit_a64(0x52010129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_f32_ceil() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f32_ceil));
         else unimplemented();
      }

      void emit_f32_floor() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f32_floor));
         else unimplemented();
      }

      void emit_f32_trunc() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f32_trunc));
         else unimplemented();
      }

      void emit_f32_nearest() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f32_nearest));
         else unimplemented();
      }

      void emit_f32_sqrt() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f32_sqrt));
         else unimplemented();
      }

      // ---- f32 binary ops ----

      void emit_f32_add() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f32_add));
         else unimplemented();
      }

      void emit_f32_sub() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f32_sub));
         else unimplemented();
      }

      void emit_f32_mul() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f32_mul));
         else unimplemented();
      }

      void emit_f32_div() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f32_div));
         else unimplemented();
      }

      void emit_f32_min() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f32_min));
         else unimplemented();
      }

      void emit_f32_max() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f32_max));
         else unimplemented();
      }

      void emit_f32_copysign() {
         // Pop sign source (x10), pop magnitude (x9)
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // and w10, w10, #0x80000000  (extract sign bit)
         // AND Wd, Wn, #0x80000000: N=0, immr=1, imms=0
         // 0x12000000 | (1<<16) | (0<<10) | (10<<5) | 10 = 0x1201014A
         emit_a64(0x1201014A);
         // and w9, w9, #0x7FFFFFFF  (clear sign bit of magnitude)
         // 0x12000000 | (0<<16) | (30<<10) | (9<<5) | 9 = 0x12007929
         emit_a64(0x12007929);
         // orr w9, w9, w10  (combine)
         // ORR Wd, Wn, Wm = 0x2A000000 | (Wm<<16) | (Wn<<5) | Wd
         emit_a64(0x2A0A0129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      // ---- f64 unary ops ----

      void emit_f64_abs() {
         // Pop, clear sign bit, push
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // and x9, x9, #0x7FFFFFFFFFFFFFFF
         // A64 64-bit logical immediate for 0x7FFFFFFFFFFFFFFF:
         // 63 ones starting at bit 0: N=1, immr=0, imms=62(0x3E)
         // AND Xd, Xn, #imm: 0x92000000 | (N<<22) | (immr<<16) | (imms<<10) | (Rn<<5) | Rd
         // = 0x92400000 | (0<<16) | (62<<10) | (9<<5) | 9
         // = 0x92400000 | 0xF800 | 0x120 | 9 = 0x9240F929
         emit_a64(0x9240F929);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_f64_neg() {
         // Pop, flip sign bit, push
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // eor x9, x9, #0x8000000000000000
         // A64 64-bit logical immediate: single 1 at bit 63
         // N=1, immr=1, imms=0
         // EOR Xd, Xn, #imm: 0xD2000000 | (N<<22) | (immr<<16) | (imms<<10) | (Rn<<5) | Rd
         // = 0xD2400000 | (1<<16) | (0<<10) | (9<<5) | 9
         // = 0xD2410129
         emit_a64(0xD2410129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      void emit_f64_ceil() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f64_ceil));
         else unimplemented();
      }

      void emit_f64_floor() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f64_floor));
         else unimplemented();
      }

      void emit_f64_trunc() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f64_trunc));
         else unimplemented();
      }

      void emit_f64_nearest() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f64_nearest));
         else unimplemented();
      }

      void emit_f64_sqrt() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f64_sqrt));
         else unimplemented();
      }

      // ---- f64 binary ops ----

      void emit_f64_add() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f64_add));
         else unimplemented();
      }

      void emit_f64_sub() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f64_sub));
         else unimplemented();
      }

      void emit_f64_mul() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f64_mul));
         else unimplemented();
      }

      void emit_f64_div() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f64_div));
         else unimplemented();
      }

      void emit_f64_min() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f64_min));
         else unimplemented();
      }

      void emit_f64_max() {
         if constexpr (use_softfloat) emit_softfloat_binop(CHOOSE_FN(_core_net_f64_max));
         else unimplemented();
      }

      void emit_f64_copysign() {
         // Pop sign source (x10), pop magnitude (x9)
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // and x10, x10, #0x8000000000000000  (extract sign bit)
         // A64 64-bit logical: N=1, immr=1, imms=0
         // AND Xd, Xn, #imm: 0x92000000 | (N<<22) | (immr<<16) | (imms<<10) | (Rn<<5) | Rd
         // = 0x92400000 | (1<<16) | (0<<10) | (10<<5) | 10 = 0x9241014A
         emit_a64(0x9241014A);
         // and x9, x9, #0x7FFFFFFFFFFFFFFF  (clear sign bit)
         // N=1, immr=0, imms=62(0x3E)
         emit_a64(0x9240F929);
         // orr x9, x9, x10  (combine)
         // ORR Xd, Xn, Xm = 0xAA000000 | (Xm<<16) | (Xn<<5) | Xd
         emit_a64(0xAA0A0129);
         // stp x9, xzr, [sp, #-16]!
         emit_a64(0xA9BF7FE9);
      }

      // ---- Conversions ----

      void emit_i32_wrap_i64() {
         // Just zero out the upper 32 bits of the value on top of stack.
         // ldr x9, [sp]   -- peek
         emit_a64(0xF94003E9);
         // mov w9, w9      -- zero-extend 32 bits (implicit in A64: writing Wd zeroes upper 32)
         // Actually, AND w9, w9, #0xFFFFFFFF is a no-op since w9 is already 32-bit.
         // We need: uxtw x9, w9. But there's no explicit UXTW -- using w9 zeroes upper bits.
         // The simplest approach: str w9, [sp] then str wzr, [sp, #4]
         // Or just: and x9, x9, #0xFFFFFFFF
         // A64 logical immediate for 0xFFFFFFFF (64-bit):
         //   N=0, immr=0, imms=0x1F (31 ones)
         // AND Xd, Xn, #imm = 0x92000000 | ...
         // Actually, for 64-bit: 0xFFFFFFFF = N=0, immr=0, imms=31
         // AND Xd, Xn, #0xFFFFFFFF = 0x92400000 | (0<<16) | (31<<10) | (Rn<<5) | Rd
         // Hmm, let me just use:
         // mov w9, w9   (32-bit mov, zeroes upper 32 bits)
         // But MOV Wd, Wn is actually ORR Wd, WZR, Wn = 0x2A0003E0 | (Wn << 16) | Wd
         emit_a64(0x2A0903E9);
         // str x9, [sp]
         emit_a64(0xF90003E9);
      }

      void emit_i32_trunc_s_f32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f32_trunc_i32s>()));
         else unimplemented();
      }

      void emit_i32_trunc_u_f32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f32_trunc_i32u>()));
         else unimplemented();
      }

      void emit_i32_trunc_s_f64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f64_trunc_i32s>()));
         else unimplemented();
      }

      void emit_i32_trunc_u_f64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f64_trunc_i32u>()));
         else unimplemented();
      }

      void emit_i64_extend_s_i32() {
         // Sign-extend 32-bit to 64-bit
         // ldr x9, [sp]   -- peek
         emit_a64(0xF94003E9);
         // sxtw x9, w9  = 0x93407C00 | (9<<5) | 9
         emit_a64(0x93407D29);
         // str x9, [sp]
         emit_a64(0xF90003E9);
      }

      void emit_i64_extend_u_i32() {
         // On x86_64, writing to a 32-bit register (e.g., movl) always zeroes the
         // upper 32 bits, so i32 ops always leave clean values and this can be a no-op.
         // On AArch64, i32 ops that use Wd also zero the upper 32 bits. HOWEVER,
         // i32.load8_s / i32.load16_s use LDRSB Xt / LDRSH Xt which sign-extend to
         // the full 64-bit register. If a negative value is loaded with i32.load8_s
         // and then directly used with i64.extend_u_i32 (no intervening i32 op),
         // the upper 32 bits would be 0xFFFFFFFF instead of 0x00000000.
         // We must explicitly zero-extend here.
         // ldr x9, [sp]   -- peek
         emit_a64(0xF94003E9);
         // mov w9, w9      -- zero-extend (ORR Wd, WZR, Wn zeroes upper 32 bits)
         emit_a64(0x2A0903E9);
         // str x9, [sp]
         emit_a64(0xF90003E9);
      }

      void emit_i64_trunc_s_f32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f32_trunc_i64s>()));
         else unimplemented();
      }

      void emit_i64_trunc_u_f32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f32_trunc_i64u>()));
         else unimplemented();
      }

      void emit_i64_trunc_s_f64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f64_trunc_i64s>()));
         else unimplemented();
      }

      void emit_i64_trunc_u_f64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_core_net_f64_trunc_i64u>()));
         else unimplemented();
      }

      void emit_f32_convert_s_i32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_i32_to_f32));
         else unimplemented();
      }

      void emit_f32_convert_u_i32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_ui32_to_f32));
         else unimplemented();
      }

      void emit_f32_convert_s_i64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_i64_to_f32));
         else unimplemented();
      }

      void emit_f32_convert_u_i64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_ui64_to_f32));
         else unimplemented();
      }

      void emit_f32_demote_f64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f64_demote));
         else unimplemented();
      }

      void emit_f64_convert_s_i32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_i32_to_f64));
         else unimplemented();
      }

      void emit_f64_convert_u_i32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_ui32_to_f64));
         else unimplemented();
      }

      void emit_f64_convert_s_i64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_i64_to_f64));
         else unimplemented();
      }

      void emit_f64_convert_u_i64() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_ui64_to_f64));
         else unimplemented();
      }

      void emit_f64_promote_f32() {
         if constexpr (use_softfloat) emit_softfloat_unop(CHOOSE_FN(_core_net_f32_promote));
         else unimplemented();
      }

      // ---- Reinterpretations (no-ops, same as x86_64) ----

      void emit_i32_reinterpret_f32() { /* Nothing to do */ }
      void emit_i64_reinterpret_f64() { /* Nothing to do */ }
      void emit_f32_reinterpret_i32() { /* Nothing to do */ }
      void emit_f64_reinterpret_i64() { /* Nothing to do */ }

#undef CHOOSE_FN

      void emit_error() { emit_error_handler(&on_fp_error); }

      // ---- Branch fixup ----
      //
      // A64 branch instructions encode a PC-relative offset in instruction units (4 bytes).
      // B   (unconditional):  bits [25:0] = imm26, offset = imm26 * 4
      // B.cond (conditional): bits [23:5]  = imm19, offset = imm19 * 4
      // CBZ/CBNZ:             bits [23:5]  = imm19, offset = imm19 * 4
      // BL  (branch+link):    bits [25:0] = imm26, offset = imm26 * 4
      //
      // fix_branch patches the branch instruction at `branch` to jump to `target`.
      // It inspects the opcode to determine whether it is a B/BL (26-bit offset)
      // or B.cond/CBZ/CBNZ (19-bit offset).

      static void fix_branch(void* branch, void* target) {
         auto branch_ = static_cast<uint8_t*>(branch);
         auto target_ = static_cast<uint8_t*>(target);
         intptr_t byte_offset = target_ - branch_;
         // Must be aligned to 4 bytes
         assert((byte_offset & 0x3) == 0);
         int32_t instr_offset = static_cast<int32_t>(byte_offset >> 2);

         uint32_t instr;
         memcpy(&instr, branch, 4);

         uint32_t op = instr >> 26;
         if (op == 0x05 || op == 0x25) {
            // B (000101) or BL (100101): imm26
            assert(instr_offset >= -(1 << 25) && instr_offset < (1 << 25));
            uint32_t imm26 = static_cast<uint32_t>(instr_offset) & 0x03FFFFFF;
            instr = (instr & 0xFC000000) | imm26;
         } else {
            // B.cond (01010100), CBZ (x0110100), CBNZ (x0110101): imm19
            assert(instr_offset >= -(1 << 18) && instr_offset < (1 << 18));
            uint32_t imm19 = static_cast<uint32_t>(instr_offset) & 0x7FFFF;
            instr = (instr & 0xFF00001F) | (imm19 << 5);
         }

         memcpy(branch, &instr, 4);
      }

      using fn_type = native_value(*)(void* context, void* memory);

      void finalize(function_body& body) {
         _allocator.reclaim(code, _code_end - code);
         body.jit_code_offset = _code_start - (unsigned char*)_code_segment_base;
      }

      const void* get_addr() const {
         return code;
      }

      const void* get_base_addr() const { return _code_segment_base; }

    private:

      // ---- Instruction size tracking (matches x86_64 interface) ----

      auto fixed_size_instr(std::size_t expected_bytes) {
         return scope_guard{[this, expected_code=code+expected_bytes](){
#ifdef CORE_NET_VM_VALIDATE_JIT_SIZE
            assert(code == expected_code);
#endif
            ignore_unused_variable_warning(code, expected_code);
         }};
      }
      auto variable_size_instr(std::size_t min, std::size_t max) {
         return scope_guard{[this, min_code=code+min, max_code=code+max](){
#ifdef CORE_NET_VM_VALIDATE_JIT_SIZE
            assert(min_code <= code && code <= max_code);
#endif
            ignore_unused_variable_warning(code, min_code, max_code);
         }};
      }
      auto softfloat_instr(std::size_t hard_expected, std::size_t soft_expected, std::size_t softbt_expected) {
         return fixed_size_instr(use_softfloat ? (Context::async_backtrace() ? softbt_expected : soft_expected) : hard_expected);
      }

      // ---- Member variables ----

      module& _mod;
      growable_allocator& _allocator;
      void* _code_segment_base;
      const func_type* _ft;
      unsigned char* _code_start;
      unsigned char* _code_end;
      unsigned char* code;
      std::vector<std::variant<std::vector<void*>, void*>> _function_relocations;
      void* fpe_handler;
      void* call_indirect_handler;
      void* type_error_handler;
      void* stack_overflow_handler;
      void* jmp_table;
      uint32_t _local_count;
      uint32_t _table_element_size;

      // ---- Low-level A64 emission helpers ----

      // Emit a single 32-bit A64 instruction (little-endian)
      void emit_a64(uint32_t instr) {
         memcpy(code, &instr, 4);
         code += 4;
      }

      // Emit a NOP as a branch placeholder and return its address for later fixup.
      // The caller should then patch this with fix_branch().
      //
      // For unconditional branches (B/BL), we emit B with offset 0.
      // For conditional branches, the caller must have set up the condition
      // code before calling this, and the instruction is emitted directly
      // (e.g., cbz, cbnz, b.cond) with the caller providing the base opcode.
      //
      // This version emits an unconditional B placeholder.
      void* emit_branch_target() {
         void* result = code;
         // B #0 (unconditional branch to self, will be patched)
         emit_a64(0x14000000);
         return result;
      }

      // Emit ADR Xd, #0 (placeholder) and return its address for fixup via fix_adr().
      // ADR computes a PC-relative address into Xd. Range: ±1MB.
      void* emit_adr(uint8_t rd) {
         void* result = code;
         // ADR Xd, #0:  0x10000000 | Rd
         emit_a64(0x10000000 | rd);
         return result;
      }

      // Patch an ADR instruction at `adr_loc` to compute the address of `target`.
      // ADR encoding: immlo (bits [30:29]) | 10000 | immhi (bits [23:5]) | Rd (bits [4:0])
      // offset = (immhi << 2) | immlo, signed 21-bit, byte-granular.
      static void fix_adr(void* adr_loc, void* target) {
         auto adr_ = static_cast<uint8_t*>(adr_loc);
         auto target_ = static_cast<uint8_t*>(target);
         intptr_t offset = target_ - adr_;
         assert(offset >= -(1 << 20) && offset < (1 << 20)); // ±1MB
         uint32_t uoffset = static_cast<uint32_t>(offset) & 0x1FFFFF; // 21 bits
         uint32_t immlo = uoffset & 0x3;          // bits [1:0]
         uint32_t immhi = (uoffset >> 2) & 0x7FFFF; // bits [20:2]
         uint32_t instr;
         memcpy(&instr, adr_loc, 4);
         instr = (instr & 0x9F00001F) | (immlo << 29) | (immhi << 5);
         memcpy(adr_loc, &instr, 4);
      }

      // Emit multipop: adjust stack pointer up by count * 16 bytes.
      // Handles the 0x80000001 sentinel (depth_change that includes a return value).
      void emit_multipop(uint32_t count) {
         if(count > 0 && count != 0x80000001) {
            if (count & 0x80000000) {
               // ldp x9, xzr, [sp]  (no writeback) -- peek return value
               emit_a64(0xA9407FE9);
            }
            if(count & 0x70000000) {
               // Probably unreachable
               // brk #0
               emit_a64(0xD4200000);
            }

            uint64_t byte_count = (uint64_t)(count & 0x0FFFFFFF) * 16;

            if (byte_count <= 4095) {
               // add sp, sp, #byte_count
               emit_a64(0x910003FF | (static_cast<uint32_t>(byte_count) << 10));
            } else if (byte_count <= 0xFFF000) {
               // May need two-step add
               uint32_t low12 = byte_count & 0xFFF;
               uint32_t high12 = (byte_count >> 12) & 0xFFF;
               if (high12) {
                  // add sp, sp, #high12, lsl #12
                  emit_a64(0x914003FF | (high12 << 10));
               }
               if (low12) {
                  // add sp, sp, #low12
                  emit_a64(0x910003FF | (low12 << 10));
               }
            } else {
               // Use a register for very large offsets
               emit_mov_imm64(10, byte_count);
               // add sp, sp, x10
               emit_a64(0x8B0A03FF);
            }

            if (count & 0x80000000) {
               // stp x9, xzr, [sp, #-16]!  -- push return value back
               emit_a64(0xA9BF7FE9);
            }
         }
      }

      // Load a 64-bit immediate into register Xd using movz + up to 3 movk instructions.
      void emit_mov_imm64(uint8_t rd, uint64_t imm) {
         // movz Xd, #(imm & 0xFFFF)
         emit_a64(0xD2800000 | ((imm & 0xFFFF) << 5) | rd);
         if (imm > 0xFFFF) {
            // movk Xd, #((imm >> 16) & 0xFFFF), lsl #16
            emit_a64(0xF2A00000 | (((imm >> 16) & 0xFFFF) << 5) | rd);
         }
         if (imm > 0xFFFFFFFF) {
            // movk Xd, #((imm >> 32) & 0xFFFF), lsl #32
            emit_a64(0xF2C00000 | (((imm >> 32) & 0xFFFF) << 5) | rd);
         }
         if (imm > 0xFFFFFFFFFFFF) {
            // movk Xd, #((imm >> 48) & 0xFFFF), lsl #48
            emit_a64(0xF2E00000 | (((imm >> 48) & 0xFFFF) << 5) | rd);
         }
      }

      // ---- Call depth checking ----

      void emit_check_call_depth() {
         // subs w19, w19, #1  -- decrement call depth counter
         emit_a64(0x71000673);
         // b.eq stack_overflow_handler
         {
            void* br = code;
            emit_a64(0x54000000); // b.eq placeholder
            fix_branch(br, stack_overflow_handler);
         }
      }

      void emit_check_call_depth_end() {
         // add w19, w19, #1  -- increment call depth counter
         emit_a64(0x11000673);
      }

      static void unimplemented() { CORE_NET_VM_ASSERT(false, wasm_parse_exception, "Sorry, not implemented."); }

      // ---- Error handler emission ----
      //
      // Each error handler is 5 A64 instructions (20 bytes):
      //   movz x9, #lo16
      //   movk x9, #hi16, lsl 16
      //   movk x9, #hi32, lsl 32
      //   movk x9, #hi48, lsl 48
      //   blr x9

      void* emit_error_handler(void (*handler)()) {
         void* result = code;
         auto addr = reinterpret_cast<uint64_t>(handler);
         // movz x9, #(addr[15:0])
         emit_a64(0xD2800000 | ((addr & 0xFFFF) << 5) | 9);
         // movk x9, #(addr[31:16]), lsl #16
         emit_a64(0xF2A00000 | (((addr >> 16) & 0xFFFF) << 5) | 9);
         // movk x9, #(addr[47:32]), lsl #32
         emit_a64(0xF2C00000 | (((addr >> 32) & 0xFFFF) << 5) | 9);
         // movk x9, #(addr[63:48]), lsl #48
         emit_a64(0xF2E00000 | (((addr >> 48) & 0xFFFF) << 5) | 9);
         // blr x9
         emit_a64(0xD63F0120);
         return result;
      }

      // ---- Host function call emission ----
      //
      // AArch64 calling convention (AAPCS64):
      //   x0 = first argument  (context pointer, already in x0)
      //   x1 = second argument (stack pointer for args)
      //   x2 = third argument  (function index)
      //   x0 = return value

      void emit_host_call(uint32_t funcnum) {
         // Always save x29/x30: blr clobbers x30 (link register) and we
         // need it intact for ret to return to the calling JIT function.
         // stp x29, x30, [sp, #-16]!  -- save frame
         emit_a64(0xA9BF7BFD);

         if constexpr (Context::async_backtrace()) {
            // mov x9, sp
            emit_a64(0x910003E9);
            // str x9, [x0]  -- *(context) = sp
            emit_a64(0xF9000009);
         }

         // mov w2, #funcnum  -- function index as third argument
         emit_a64(0x52800000 | ((funcnum & 0xFFFF) << 5) | 2);
         if (funcnum > 0xFFFF) {
            emit_a64(0x72A00000 | (((funcnum >> 16) & 0xFFFF) << 5) | 2);
         }

         // stp x0, x1, [sp, #-16]!  -- save context (x0) and memory base (x1)
         emit_a64(0xA9BF07E0);

         // add x1, sp, #32  -- x1 = pointer to args on stack
         //   (above saved x0/x1 pair AND saved x29/x30 pair)
         emit_a64(0x910083E1); // add x1, sp, #32

         // Load address of call_host_function into x9
         auto addr = reinterpret_cast<uint64_t>(&call_host_function);
         emit_mov_imm64(9, addr);

         // blr x9  -- call host function
         emit_a64(0xD63F0120);

         // mov x9, x0  -- save return value
         emit_a64(0xAA0003E9);
         // ldp x0, x1, [sp], #16  -- restore context and memory base
         emit_a64(0xA8C107E0);
         // mov x0, x9  -- put return value back in x0 for ret to trampoline
         emit_a64(0xAA0903E0);

         // ldp x29, x30, [sp], #16  -- restore frame (including link register)
         emit_a64(0xA8C17BFD);

         // ret
         emit_a64(0xD65F03C0);
      }

      // ---- Backtrace support ----

      uint32_t emit_setup_backtrace() {
         if constexpr (Context::async_backtrace()) {
            // stp x29, x30, [sp, #-16]!
            emit_a64(0xA9BF7BFD);
            // mov x9, sp
            emit_a64(0x910003E9);
            // str x9, [x0]  -- store stack pointer into context
            emit_a64(0xF9000009);
            return 16;
         } else {
            return 0;
         }
      }

      void emit_restore_backtrace_basic() {
         if constexpr (Context::async_backtrace()) {
            // str xzr, [x0]  -- clear backtrace pointer
            emit_a64(0xF900001F);
         }
      }

      void emit_restore_backtrace() {
         if constexpr (Context::async_backtrace()) {
            emit_restore_backtrace_basic();
            // ldp x29, x30, [sp], #16  -- restore frame
            emit_a64(0xA8C17BFD);
         }
      }

      // ---- i32/i64 relop helpers ----
      //
      // cond_inv is the INVERTED condition for CSET:
      //   CSET Xd, cond  is  CSINC Xd, XZR, XZR, cond_inv

      void emit_i32_relop(uint8_t cond_inv) {
         // Pop two operands: x10 = rhs (top), x9 = lhs (second)
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // cmp w9, w10
         emit_a64(0x6B00001F | (10 << 16) | (9 << 5));
         // cset x9, cond  = CSINC x9, xzr, xzr, cond_inv
         emit_a64(0x9A9F07E0 | (static_cast<uint32_t>(cond_inv) << 12) | 9);
         // push result
         emit_a64(0xA9BF7FE9);
      }

      void emit_i64_relop(uint8_t cond_inv) {
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // cmp x9, x10
         emit_a64(0xEB00001F | (10 << 16) | (9 << 5));
         // cset x9, cond
         emit_a64(0x9A9F07E0 | (static_cast<uint32_t>(cond_inv) << 12) | 9);
         // push result
         emit_a64(0xA9BF7FE9);
      }

      // ---- i32/i64 binop helpers ----
      //
      // Generic: pop rhs into x10, pop lhs into x9, apply op, push result.
      // The opcode is the full A64 instruction base with Rd, Rn, Rm fields.

      void emit_i32_binop(uint32_t opcode) {
         emit_a64(0xA8C17FEA); // pop rhs -> x10
         emit_a64(0xA8C17FE9); // pop lhs -> x9
         // OP w9, w9, w10  (or x9, x9, x10 depending on encoding)
         emit_a64(opcode | (10 << 16) | (9 << 5) | 9);
         emit_a64(0xA9BF7FE9); // push x9
      }

      void emit_i64_binop(uint32_t opcode) {
         emit_a64(0xA8C17FEA); // pop rhs -> x10
         emit_a64(0xA8C17FE9); // pop lhs -> x9
         emit_a64(opcode | (10 << 16) | (9 << 5) | 9);
         emit_a64(0xA9BF7FE9); // push x9
      }

      // ---- Load/store helpers ----
      //
      // Load pattern: pop address from operand stack, add wasm offset, add memory base (x1), load, push result.
      // load_instr is the full encoding for LDR/LDRB/LDRH/etc with register offset [Xn, Xm].

      void emit_load_impl(uint32_t offset, uint32_t load_instr) {
         // Pop address into x9
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16
         // Zero-extend address to 64-bit (WASM addresses are 32-bit)
         // mov w9, w9  -- zero-extend (ORR w9, wzr, w9)
         emit_a64(0x2A0903E9);
         // Add wasm offset
         if (offset != 0) {
            if (offset < 4096) {
               // add x9, x9, #offset
               emit_a64(0x91000000 | (offset << 10) | (9 << 5) | 9);
            } else {
               // mov x10, #offset
               emit_mov_imm64(10, offset);
               // add x9, x9, x10
               emit_a64(0x8B0A0129);
            }
         }
         // Load from [x1 + x9] (x1 = memory base)
         // load_instr has the form: OP Rt, [Xn, Xm] with Rt=0, Xn=0, Xm=0
         // We set Rt=x9, Xn=x1, Xm=x9
         emit_a64(load_instr | (9 << 16) | (1 << 5) | 9);
         // Push result
         emit_a64(0xA9BF7FE9); // stp x9, xzr, [sp, #-16]!
      }

      void emit_store_impl(uint32_t offset, uint32_t store_instr) {
         // Pop value into x10, pop address into x9
         emit_a64(0xA8C17FEA); // ldp x10, xzr, [sp], #16  -- value
         emit_a64(0xA8C17FE9); // ldp x9, xzr, [sp], #16   -- address
         // Zero-extend address
         emit_a64(0x2A0903E9); // mov w9, w9
         // Add wasm offset
         if (offset != 0) {
            if (offset < 4096) {
               emit_a64(0x91000000 | (offset << 10) | (9 << 5) | 9);
            } else {
               emit_mov_imm64(11, offset);
               // add x9, x9, x11
               emit_a64(0x8B0B0129);
            }
         }
         // Store to [x1 + x9] (x1 = memory base)
         // store_instr: STR Rt, [Xn, Xm] -- we set Rt=x10, Xn=x1, Xm=x9
         emit_a64(store_instr | (9 << 16) | (1 << 5) | 10);
      }

      // ---- Local access helpers ----
      //
      // Load from [x29 + offset] into Xd.
      // Offset can be positive (params) or negative (locals).

      void emit_ldr_fp_offset(uint8_t rd, int32_t offset) {
         if (offset >= 0 && offset < 32760 && (offset & 7) == 0) {
            // ldr Xd, [x29, #offset]  -- unsigned offset, scaled by 8
            // LDR (immediate, unsigned offset): 0xF9400000 | (imm12<<10) | (Rn<<5) | Rt
            // imm12 = offset / 8
            uint32_t imm12 = static_cast<uint32_t>(offset) / 8;
            emit_a64(0xF9400000 | (imm12 << 10) | (29 << 5) | rd);
         } else if (offset >= -256 && offset <= 255) {
            // ldr Xd, [x29, #offset]  -- signed offset, unscaled
            // LDUR Xt, [Xn, #simm9] = 0xF8400000 | (imm9<<12) | (Rn<<5) | Rt
            uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FF;
            emit_a64(0xF8400000 | (imm9 << 12) | (29 << 5) | rd);
         } else {
            // Use register for large offsets
            emit_mov_imm64(rd, static_cast<uint64_t>(static_cast<int64_t>(offset)));
            // add Xd, x29, Xd
            emit_a64(0x8B000000 | (rd << 16) | (29 << 5) | rd);
            // ldr Xd, [Xd]
            emit_a64(0xF9400000 | (rd << 5) | rd);
         }
      }

      void emit_str_fp_offset(uint8_t rs, int32_t offset) {
         if (offset >= 0 && offset < 32760 && (offset & 7) == 0) {
            // str Xd, [x29, #offset]
            uint32_t imm12 = static_cast<uint32_t>(offset) / 8;
            emit_a64(0xF9000000 | (imm12 << 10) | (29 << 5) | rs);
         } else if (offset >= -256 && offset <= 255) {
            // stur Xt, [x29, #offset]
            uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FF;
            emit_a64(0xF8000000 | (imm9 << 12) | (29 << 5) | rs);
         } else {
            // Use x10 as scratch for the address calculation
            emit_mov_imm64(10, static_cast<uint64_t>(static_cast<int64_t>(offset)));
            // add x10, x29, x10
            emit_a64(0x8B0A03AA);
            // str Xs, [x10]
            emit_a64(0xF9000000 | (10 << 5) | rs);
         }
      }

      // ---- Softfloat call helpers ----
      //
      // These call softfloat functions through a register.
      // On AAPCS64, arguments go in x0/x1 (or w0/w1 for 32-bit).
      // We need to save/restore our x0 (context) and x1 (memory base).
      //
      // For unary ops: pop arg -> x9, save x0/x1, mov x0=x9, call, restore x0/x1, push result.
      // For binary ops: pop rhs -> x10, pop lhs -> x9, save x0/x1, mov x0=x9 x1=x10, call, restore, push result.

      template<typename T, typename U>
      void emit_softfloat_unop(T(*fn)(U)) {
         auto extra = emit_setup_backtrace();
         // stp x0, x1, [sp, #-16]!  -- save context and memory base
         emit_a64(0xA9BF07E0);
         // Load argument from operand stack (which is above our saved pair)
         // The value is at [sp + 16 + extra]
         {
            uint32_t stack_offset = 16 + extra;
            // ldr x9, [sp, #stack_offset]
            if (stack_offset < 32760 && (stack_offset & 7) == 0) {
               uint32_t imm12 = stack_offset / 8;
               emit_a64(0xF9400000 | (imm12 << 10) | (31 << 5) | 9);
            } else {
               emit_mov_imm64(9, stack_offset);
               emit_a64(0xF8696BE9); // ldr x9, [sp, x9]
            }
         }
         // mov arg to x0 (first AAPCS64 argument)
         if constexpr (sizeof(U) == 4) {
            emit_a64(0x2A0903E0); // mov w0, w9
         } else {
            emit_a64(0xAA0903E0); // mov x0, x9
         }
         // Load function address into x9
         emit_mov_imm64(9, reinterpret_cast<uint64_t>(fn));
         // blr x9
         emit_a64(0xD63F0120);
         // Save result
         // mov x9, x0
         emit_a64(0xAA0003E9);
         // ldp x0, x1, [sp], #16  -- restore context and memory base
         emit_a64(0xA8C107E0);
         emit_restore_backtrace();
         // Store result back to operand stack (overwrite the argument in-place)
         if constexpr (sizeof(T) == 4 && sizeof(U) == 4) {
            // 32-bit result: zero the upper half
            emit_a64(0x2A0903E9); // mov w9, w9  (zero-extend)
         }
         // str x9, [sp]  -- overwrite the value on top of operand stack
         emit_a64(0xF90003E9);
      }

      template<typename T, typename U, typename V>
      void emit_softfloat_binop_impl(T(*fn)(U, V)) {
         auto extra = emit_setup_backtrace();
         // stp x0, x1, [sp, #-16]!  -- save context and memory base
         emit_a64(0xA9BF07E0);
         // Load rhs and lhs from operand stack
         // rhs is at [sp + 16 + extra], lhs is at [sp + 32 + extra]
         {
            uint32_t rhs_offset = 16 + extra;
            uint32_t lhs_offset = 32 + extra;
            // Load rhs -> x10
            if (rhs_offset < 32760 && (rhs_offset & 7) == 0) {
               emit_a64(0xF9400000 | ((rhs_offset/8) << 10) | (31 << 5) | 10);
            } else {
               emit_mov_imm64(10, rhs_offset);
               // ldr x10, [sp, x10]
               emit_a64(0xF86A6BEA);
            }
            // Load lhs -> x9
            if (lhs_offset < 32760 && (lhs_offset & 7) == 0) {
               emit_a64(0xF9400000 | ((lhs_offset/8) << 10) | (31 << 5) | 9);
            } else {
               emit_mov_imm64(9, lhs_offset);
               // ldr x9, [sp, x9]
               emit_a64(0xF8696BE9);
            }
         }
         // AAPCS64: x0 = first arg (lhs), x1 = second arg (rhs)
         if constexpr (sizeof(U) == 4) {
            emit_a64(0x2A0903E0); // mov w0, w9
         } else {
            emit_a64(0xAA0903E0); // mov x0, x9
         }
         if constexpr (sizeof(V) == 4) {
            emit_a64(0x2A0A03E1); // mov w1, w10
         } else {
            emit_a64(0xAA0A03E1); // mov x1, x10
         }
         // Load function address
         emit_mov_imm64(9, reinterpret_cast<uint64_t>(fn));
         // blr x9
         emit_a64(0xD63F0120);
         // Save result
         emit_a64(0xAA0003E9); // mov x9, x0
         // ldp x0, x1, [sp], #16  -- restore context and memory base
         emit_a64(0xA8C107E0);
         emit_restore_backtrace();
         // Pop the two operands and push the result: net effect is pop one slot
         // add sp, sp, #16  -- remove the rhs slot
         emit_a64(0x91004000 | (31 << 5) | 31);
         // str x9, [sp]  -- overwrite the lhs slot with result
         emit_a64(0xF90003E9);
      }

      template<typename T>
      void emit_softfloat_binop(T(*fn)(T, T)) {
         emit_softfloat_binop_impl(fn);
      }

      void emit_softfloat_binop(uint64_t (*fn)(float32_t, float32_t)) {
         emit_softfloat_binop_impl(fn);
      }

      void emit_softfloat_binop(uint64_t (*fn)(float64_t, float64_t)) {
         emit_softfloat_binop_impl(fn);
      }

      // ---- Float relop helpers ----
      //
      // These use softfloat comparison functions.
      // switch_params: swap lhs/rhs for gt/ge (reusing lt/le comparators)
      // flip_result: invert result for ne (reusing eq comparator)

      void emit_f32_relop(uint64_t (*fn)(float32_t, float32_t), bool switch_params, bool flip_result) {
         auto extra = emit_setup_backtrace();
         emit_a64(0xA9BF07E0); // stp x0, x1, [sp, #-16]!
         uint32_t rhs_offset = 16 + extra;
         uint32_t lhs_offset = 32 + extra;
         // Load args
         if (rhs_offset < 32760 && (rhs_offset & 7) == 0) {
            emit_a64(0xF9400000 | ((rhs_offset/8) << 10) | (31 << 5) | 10);
         } else {
            emit_mov_imm64(10, rhs_offset);
            emit_a64(0xF86A6BEA);
         }
         if (lhs_offset < 32760 && (lhs_offset & 7) == 0) {
            emit_a64(0xF9400000 | ((lhs_offset/8) << 10) | (31 << 5) | 9);
         } else {
            emit_mov_imm64(9, lhs_offset);
            emit_a64(0xF8696BE9);
         }
         if (switch_params) {
            emit_a64(0x2A0A03E0); // mov w0, w10
            emit_a64(0x2A0903E1); // mov w1, w9
         } else {
            emit_a64(0x2A0903E0); // mov w0, w9
            emit_a64(0x2A0A03E1); // mov w1, w10
         }
         emit_mov_imm64(9, reinterpret_cast<uint64_t>(fn));
         emit_a64(0xD63F0120); // blr x9
         if (flip_result) {
            // eor x0, x0, #1
            // EOR Xd, Xn, #imm: N=1, immr=0, imms=0 -> value 0x1
            // 0xD2400000 | (0<<16) | (0<<10) | (0<<5) | 0
            emit_a64(0xD2400000); // eor x0, x0, #1
         }
         emit_a64(0xAA0003E9); // mov x9, x0
         emit_a64(0xA8C107E0); // ldp x0, x1, [sp], #16
         emit_restore_backtrace();
         // Pop two, push one: add sp, sp, #16 then str
         emit_a64(0x910043FF); // add sp, sp, #16
         emit_a64(0xF90003E9); // str x9, [sp]
      }

      void emit_f64_relop(uint64_t (*fn)(float64_t, float64_t), bool switch_params, bool flip_result) {
         auto extra = emit_setup_backtrace();
         emit_a64(0xA9BF07E0); // stp x0, x1, [sp, #-16]!
         uint32_t rhs_offset = 16 + extra;
         uint32_t lhs_offset = 32 + extra;
         if (rhs_offset < 32760 && (rhs_offset & 7) == 0) {
            emit_a64(0xF9400000 | ((rhs_offset/8) << 10) | (31 << 5) | 10);
         } else {
            emit_mov_imm64(10, rhs_offset);
            emit_a64(0xF86A6BEA);
         }
         if (lhs_offset < 32760 && (lhs_offset & 7) == 0) {
            emit_a64(0xF9400000 | ((lhs_offset/8) << 10) | (31 << 5) | 9);
         } else {
            emit_mov_imm64(9, lhs_offset);
            emit_a64(0xF8696BE9);
         }
         if (switch_params) {
            emit_a64(0xAA0A03E0); // mov x0, x10
            emit_a64(0xAA0903E1); // mov x1, x9
         } else {
            emit_a64(0xAA0903E0); // mov x0, x9
            emit_a64(0xAA0A03E1); // mov x1, x10
         }
         emit_mov_imm64(9, reinterpret_cast<uint64_t>(fn));
         emit_a64(0xD63F0120); // blr x9
         if (flip_result) {
            // eor x0, x0, #1
            emit_a64(0xD2400000 | (0 << 5) | 0);
         }
         emit_a64(0xAA0003E9); // mov x9, x0
         emit_a64(0xA8C107E0); // ldp x0, x1, [sp], #16
         emit_restore_backtrace();
         emit_a64(0x910043FF); // add sp, sp, #16
         emit_a64(0xF90003E9); // str x9, [sp]
      }

      // ---- Static helper functions (same as x86_64) ----

      bool is_host_function(uint32_t funcnum) { return funcnum < _mod.get_imported_functions_size(); }

      static native_value call_host_function(Context* context /*x0*/, native_value* stack /*x1*/, uint32_t idx /*w2*/) {
         native_value result;
         vm::longjmp_on_exception([&]() {
            result = context->call_host_function(stack, idx);
         });
         return result;
      }

      static int32_t current_memory(Context* context /*x0*/) {
         return context->current_linear_memory();
      }

      static int32_t grow_memory(Context* context /*x0*/, int32_t pages) {
         return context->grow_linear_memory(pages);
      }

      static int32_t get_global_i32(Context* context /*x0*/, uint32_t index /*w1*/) {
         return context->get_global_i32(index);
      }
      static int64_t get_global_i64(Context* context /*x0*/, uint32_t index /*w1*/) {
         return context->get_global_i64(index);
      }
      static uint32_t get_global_f32(Context* context /*x0*/, uint32_t index /*w1*/) {
         return context->get_global_f32(index);
      }
      static uint64_t get_global_f64(Context* context /*x0*/, uint32_t index /*w1*/) {
         return context->get_global_f64(index);
      }

      static void set_global_i32(Context* context /*x0*/, uint32_t index /*w1*/, int32_t value /*w2*/) {
         context->set_global_i32(index, value);
      }
      static void set_global_i64(Context* context /*x0*/, uint32_t index /*w1*/, int64_t value /*x2*/) {
         context->set_global_i64(index, value);
      }
      static void set_global_f32(Context* context /*x0*/, uint32_t index /*w1*/, uint32_t value /*w2*/) {
         context->set_global_f32(index, value);
      }
      static void set_global_f64(Context* context /*x0*/, uint32_t index /*w1*/, uint64_t value /*x2*/) {
         context->set_global_f64(index, value);
      }

      static void on_unreachable() { vm::throw_<wasm_interpreter_exception>( "unreachable" ); }
      static void on_fp_error() { vm::throw_<wasm_interpreter_exception>( "floating point error" ); }
      static void on_call_indirect_error() { vm::throw_<wasm_interpreter_exception>( "call_indirect out of range" ); }
      static void on_type_error() { vm::throw_<wasm_interpreter_exception>( "call_indirect incorrect function type" ); }
      static void on_stack_overflow() { vm::throw_<wasm_interpreter_exception>( "stack overflow" ); }
   };

}}
