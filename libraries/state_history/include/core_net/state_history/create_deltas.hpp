#pragma once

#include <core_net/state_history/types.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>

namespace core_net {
namespace state_history {

void pack_deltas(boost::iostreams::filtering_ostreambuf& ds, const chainbase::database& db, bool full_snapshot);


} // namespace state_history
} // namespace core_net
