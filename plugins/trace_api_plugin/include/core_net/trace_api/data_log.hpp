#pragma once
#include <fc/variant.hpp>
#include <core_net/trace_api/trace.hpp>
#include <core_net/chain/abi_def.hpp>
#include <core_net/chain/protocol_feature_activation.hpp>

namespace core_net { namespace trace_api {

   using data_log_entry = std::variant<
      block_trace_v0,
      block_trace_v1,
      block_trace_v2
   >;

}}
