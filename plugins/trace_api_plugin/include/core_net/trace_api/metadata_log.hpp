#pragma once
#include <fc/variant.hpp>
#include <core_net/trace_api/trace.hpp>
#include <core_net/chain/abi_def.hpp>
#include <core_net/chain/protocol_feature_activation.hpp>

namespace core_net { namespace trace_api {
   struct block_entry_v0 {
      chain::block_id_type   id;
      uint32_t               number;
      uint64_t               offset;
   };

   struct lib_entry_v0 {
      uint32_t               lib;
   };

   using metadata_log_entry = std::variant<
      block_entry_v0,
      lib_entry_v0,
      block_trxs_entry
   >;

}}

FC_REFLECT(core_net::trace_api::block_entry_v0, (id)(number)(offset));
FC_REFLECT(core_net::trace_api::lib_entry_v0, (lib));
