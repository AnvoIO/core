#include <core_net/http_client_plugin/http_client_plugin.hpp>
#include <core_net/chain/exceptions.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <fstream>

namespace core_net {

http_client_plugin::http_client_plugin():my(new http_client()){}
http_client_plugin::~http_client_plugin(){}

void http_client_plugin::set_program_options(options_description&, options_description& cfg) {
}

void http_client_plugin::plugin_initialize(const variables_map& options) {
}

void http_client_plugin::plugin_startup() {

}

void http_client_plugin::plugin_shutdown() {

}

}
