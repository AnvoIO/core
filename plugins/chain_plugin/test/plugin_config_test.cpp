#include <array>
#include <boost/test/unit_test.hpp>
#include <core_net/chain/application.hpp>
#include <core_net/chain_plugin/chain_plugin.hpp>
#include <stdint.h>

BOOST_AUTO_TEST_CASE(chain_plugin_default_tests) {
   fc::temp_directory  tmp;
   appbase::scoped_app app;

   auto tmp_path = tmp.path().string();
   std::array          args = {
       "test_chain_plugin", "--blocks-log-stride", "10", "--data-dir", tmp_path.c_str(),
   };

   BOOST_CHECK(app->initialize<core_net::chain_plugin>(args.size(), const_cast<char**>(args.data())));
   auto& plugin = app->get_plugin<core_net::chain_plugin>();

   auto* config = std::get_if<core_net::chain::partitioned_blocklog_config>(&plugin.chain_config().blog);
   BOOST_REQUIRE(config);
   BOOST_CHECK_EQUAL(config->max_retained_files, UINT32_MAX);

   // test default eos-vm-oc-whitelist
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"xsat"}));
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"vaulta"}));
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"core.vaulta"}));
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"xs.vaulta"}));
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"xsat.vaulta"}));
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"vaulta.xsat"}));
   BOOST_CHECK(!plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"vault"}));
   BOOST_CHECK(!plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"xs"}));
   BOOST_CHECK(!plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{""}));
}

BOOST_AUTO_TEST_CASE(chain_plugin_core_vm_oc_whitelist) {
   fc::temp_directory  tmp;
   appbase::scoped_app app;

   auto tmp_path = tmp.path().string();
   std::array          args = {
      "test_chain_plugin", "--eos-vm-oc-whitelist", "hello", "--data-dir", tmp_path.c_str(),
  };

   BOOST_CHECK(app->initialize<core_net::chain_plugin>(args.size(), const_cast<char**>(args.data())));
   auto& plugin = app->get_plugin<core_net::chain_plugin>();
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"hello"}));
   BOOST_CHECK(plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"xs.hello"}));
   BOOST_CHECK(!plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"xsat"}));
   BOOST_CHECK(!plugin.chain().is_core_vm_oc_whitelisted(core_net::chain::name{"vaulta"}));
}
