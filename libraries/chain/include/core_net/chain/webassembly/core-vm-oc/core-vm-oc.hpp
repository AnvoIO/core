#pragma once

#include <core_net/chain/types.hpp>
#include <core_net/chain/webassembly/core-vm-oc/core-vm-oc.h>

#include <exception>

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

#include <vector>
#include <list>

namespace core_net::chain::corevmoc {

struct no_offset{};
struct code_offset {
   size_t offset; 
};    
struct intrinsic_ordinal { 
   size_t ordinal; 
};

using corevmoc_optional_offset_or_import_t = std::variant<no_offset, code_offset, intrinsic_ordinal>;

struct code_descriptor {
   digest_type code_hash;
   uint8_t vm_version;
   uint8_t codegen_version;
   size_t code_begin;
   corevmoc_optional_offset_or_import_t start;
   unsigned apply_offset;
   unsigned table_offset;  // offset of indirect call table within code blob
   int starting_memory_pages;
   size_t initdata_begin;
   unsigned initdata_size;
   unsigned initdata_prologue_size;
};

enum corevmoc_exitcode : int {
   COREVMOC_EXIT_CLEAN_EXIT = 1,
   COREVMOC_EXIT_CHECKTIME_FAIL,
   COREVMOC_EXIT_SEGV,
   COREVMOC_EXIT_EXCEPTION
};

static constexpr uint8_t current_codegen_version = 3;

}

FC_REFLECT(core_net::chain::corevmoc::no_offset, );
FC_REFLECT(core_net::chain::corevmoc::code_offset, (offset));
FC_REFLECT(core_net::chain::corevmoc::intrinsic_ordinal, (ordinal));
FC_REFLECT(core_net::chain::corevmoc::code_descriptor, (code_hash)(vm_version)(codegen_version)(code_begin)(start)(apply_offset)(table_offset)(starting_memory_pages)(initdata_begin)(initdata_size)(initdata_prologue_size));

#define COREVMOC_INTRINSIC_INIT_PRIORITY __attribute__((init_priority(198)))
