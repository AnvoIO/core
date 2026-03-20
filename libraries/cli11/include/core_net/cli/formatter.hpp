#pragma once

// CoreNetFormatter — custom CLI help formatter for Anvo Network tools.
// Provides prettier --help output with:
//   - Unicode tree characters for subcommand hierarchy
//   - Compact column width for denser display
//   - Inherited options from parent commands shown in subcommand help
//
// This lives outside the CLI11 submodule so we don't need to maintain
// a fork. It extends CLI::Formatter from upstream CLI11.

#include <CLI/CLI.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace core_net::cli {

class CoreNetFormatter : public CLI::Formatter {
   // Unicode box-drawing characters for subcommand tree display
   const char* tree_line  = "\xe2\x94\x82";  // │
   const char* tree_angle = "\xe2\x94\x94";  // └
   const char* tree_fork  = "\xe2\x94\x9c";  // ├

public:
   CoreNetFormatter() : Formatter() {
      column_width(25);  // more compact than the default 30
   }
   CoreNetFormatter(const CoreNetFormatter&) = default;
   CoreNetFormatter(CoreNetFormatter&&) = default;

   /// Print subcommands with tree-style hierarchy
   [[nodiscard]] std::string
   make_subcommands(const CLI::App* app, CLI::AppFormatMode mode) const override {
      std::stringstream out;

      std::vector<const CLI::App*> subcommands = app->get_subcommands({});

      // Collect unique groups in definition order
      std::vector<std::string> subcmd_groups_seen;
      for(const CLI::App* com : subcommands) {
         if(com->get_name().empty()) {
            if(!com->get_group().empty()) {
               out << make_expanded(com, mode);
            }
            continue;
         }
         std::string group_key = com->get_group();
         if(!group_key.empty() &&
            std::find_if(subcmd_groups_seen.begin(), subcmd_groups_seen.end(),
                         [&group_key](const std::string& a) {
                            return CLI::detail::to_lower(a) == CLI::detail::to_lower(group_key);
                         }) == subcmd_groups_seen.end())
            subcmd_groups_seen.push_back(group_key);
      }

      // Print each group with tree decorations
      for(const std::string& group : subcmd_groups_seen) {
         out << "\n" << group << ":\n";

         std::vector<const CLI::App*> subcommands_group =
             app->get_subcommands([&group](const CLI::App* sub_app) {
                return CLI::detail::to_lower(sub_app->get_group()) ==
                       CLI::detail::to_lower(group);
             });

         for(const CLI::App* new_com : subcommands_group) {
            if(new_com->get_name().empty())
               continue;

            bool is_last = (subcommands_group.back() == new_com);
            std::string prefix = is_last ? tree_angle : tree_fork;

            if(mode == CLI::AppFormatMode::All) {
               out << prefix << new_com->help(new_com->get_name(), CLI::AppFormatMode::Sub);
               out << (is_last ? "" : tree_line) << "\n";
            } else {
               out << make_subcommand(new_com);
            }
         }
      }

      return out.str();
   }

   /// Print expanded subcommand with tree indentation
   [[nodiscard]] std::string
   make_expanded(const CLI::App* sub, CLI::AppFormatMode mode) const override {
      std::stringstream out;

      out << sub->get_display_name(true) << "\n";
      out << make_description(sub);

      if(sub->get_name().empty() && !sub->get_aliases().empty()) {
         CLI::detail::format_aliases(out, sub->get_aliases(), column_width_ + 2);
      }

      out << make_positionals(sub);
      out << make_groups(sub, mode);
      out << make_subcommands(sub, mode);

      // Clean up double newlines and trailing newline
      std::string tmp = CLI::detail::find_and_replace(out.str(), "\n\n", "\n");
      if(!tmp.empty() && tmp.back() == '\n')
         tmp.pop_back();

      // Determine tree continuation character based on position in parent
      std::string subc_symbol = " ";
      if(sub->get_parent() != nullptr) {
         auto group = sub->get_group();
         std::vector<const CLI::App*> sc_group =
             sub->get_parent()->get_subcommands([&group](const CLI::App* sub_app) {
                return CLI::detail::to_lower(sub_app->get_group()) ==
                       CLI::detail::to_lower(group);
             });
         if(!sc_group.empty() && sc_group.back() != sub) {
            subc_symbol = tree_line;
         }
      }

      // Indent continuation lines for tree structure
      return CLI::detail::find_and_replace(tmp, "\n", "\n" + subc_symbol + "  ") + "\n";
   }

   /// Print option group with title
   [[nodiscard]] std::string
   make_group(std::string group, bool is_positional, std::vector<const CLI::Option*> opts) const override {
      std::stringstream out;
      out << "\n" << group << ":\n";
      for(const CLI::Option* opt : opts) {
         out << make_option(opt, is_positional);
      }
      return out.str();
   }
};

} // namespace core_net::cli
