#include <core_net/trace_api/configuration_utils.hpp>
#include <core_net/chain/exceptions.hpp>
#include <regex>
#include <fc/io/json.hpp>


namespace core_net::trace_api::configuration_utils {
   using namespace core_net;

   chain::abi_def abi_def_from_file(const std::string& file_name, const std::filesystem::path& data_dir )
   {
      fc::variant abi_variant;
      auto abi_path = std::filesystem::path(file_name);
      if (abi_path.is_relative()) {
         abi_path = data_dir / abi_path;
      }

      CORE_ASSERT(std::filesystem::exists(abi_path) && !std::filesystem::is_directory(abi_path), chain::plugin_config_exception, "${path} does not exist or is not a file", ("path", abi_path));
      try {
         abi_variant = fc::json::from_file(abi_path);
      } CORE_RETHROW_EXCEPTIONS(chain::json_parse_exception, "Fail to parse JSON from file: ${file}", ("file", abi_path));

      chain::abi_def result;
      fc::from_variant(abi_variant, result);
      return result;
   }

   std::pair<std::string, std::string> parse_kv_pairs( const std::string& input ) {
      CORE_ASSERT(!input.empty(), chain::plugin_config_exception, "Key-Value Pair is Empty");
      auto delim = input.find("=");
      CORE_ASSERT(delim != std::string::npos, chain::plugin_config_exception, "Missing \"=\"");
      CORE_ASSERT(delim != 0, chain::plugin_config_exception, "Missing Key");
      CORE_ASSERT(delim + 1 != input.size(), chain::plugin_config_exception, "Missing Value");
      return std::make_pair(input.substr(0, delim), input.substr(delim + 1));
   }

}