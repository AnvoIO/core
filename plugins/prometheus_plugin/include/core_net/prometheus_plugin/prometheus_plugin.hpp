#pragma once

#include <core_net/chain/application.hpp>
#include <core_net/http_plugin/http_plugin.hpp>
#include <core_net/chain_plugin/chain_plugin.hpp>
#include <core_net/producer_plugin/producer_plugin.hpp>
#include <core_net/net_plugin/net_plugin.hpp>

namespace core_net {

   using namespace appbase;

   class prometheus_plugin : public appbase::plugin<prometheus_plugin> {
   public:
      prometheus_plugin();
      ~prometheus_plugin() override;

      APPBASE_PLUGIN_REQUIRES((http_plugin)(chain_plugin)(producer_plugin)(net_plugin))

      void set_program_options(options_description&, options_description& cfg) override;

      void plugin_initialize(const variables_map& options);
      void plugin_startup();
      void plugin_shutdown();

   private:
      std::unique_ptr<struct prometheus_plugin_impl> my;
   };

}
