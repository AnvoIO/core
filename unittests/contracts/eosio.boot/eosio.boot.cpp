#include "eosio.boot.hpp"
#include <core_net/privileged.hpp>

namespace eosioboot {

void boot::onerror( ignore<uint128_t>, ignore<std::vector<char>> ) {
   check( false, "the onerror action cannot be called directly" );
}

void boot::activate( const core_net::checksum256& feature_digest ) {
   require_auth( get_self() );
   core_net::preactivate_feature( feature_digest );
}

void boot::reqactivated( const core_net::checksum256& feature_digest ) {
   check( core_net::is_feature_activated( feature_digest ), "protocol feature is not activated" );
}

}
