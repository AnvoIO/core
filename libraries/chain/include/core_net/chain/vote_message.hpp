#pragma once

#include <core_net/chain/types.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>


namespace core_net::chain {

   inline fc::logger vote_logger{"vote"};

   using bls_public_key          = fc::crypto::blslib::bls_public_key;
   using bls_signature           = fc::crypto::blslib::bls_signature;

   struct vote_message {
      block_id_type       block_id;
      bool                strong{false};
      bls_public_key      finalizer_key;
      bls_signature       sig;

      auto operator<=>(const vote_message&) const = default;
      bool operator==(const vote_message&) const = default;
   };

   using vote_message_ptr = std::shared_ptr<vote_message>;

} // namespace core_net::chain

FC_REFLECT(core_net::chain::vote_message, (block_id)(strong)(finalizer_key)(sig));
