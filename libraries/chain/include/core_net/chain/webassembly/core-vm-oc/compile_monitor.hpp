#pragma once

#include <core_net/chain/webassembly/core-vm-oc/config.hpp>

#include <boost/asio/local/datagram_protocol.hpp>
#include <core_net/chain/webassembly/core-vm-oc/ipc_helpers.hpp>

namespace core_net { namespace chain { namespace corevmoc {

wrapped_fd get_connection_to_compile_monitor(int cache_fd);

}}}