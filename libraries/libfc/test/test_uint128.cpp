#include <fc/uint128.hpp>
#include <fc/crypto/bigint.hpp>

#include <boost/test/unit_test.hpp>

using fc::uint128;
using fc::bigint;

BOOST_AUTO_TEST_SUITE(uint128_test_suite)

BOOST_AUTO_TEST_CASE(popcount_matches_stdbitset)
{
   // Patterns chosen to exercise every 64-bit lane and all even
   // positions inside a lane.
   const uint128 cases[] = {
      uint128{0ULL, 0ULL},
      uint128{0ULL, 1ULL},
      uint128{1ULL, 0ULL},
      uint128{0xFFFFFFFFFFFFFFFFULL, 0ULL},
      uint128{0ULL, 0xFFFFFFFFFFFFFFFFULL},
      uint128{0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
      uint128{0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL},
      uint128{0x1234567890ABCDEFULL, 0xFEDCBA0987654321ULL}
   };
   for (const auto& u : cases) {
      uint8_t expected = static_cast<uint8_t>(__builtin_popcountll(u.lo)
                                            + __builtin_popcountll(u.hi));
      BOOST_CHECK_EQUAL(u.popcount(), expected);
   }
}

BOOST_AUTO_TEST_CASE(bigint_roundtrip)
{
   // A uint128 -> bigint -> uint128 roundtrip must preserve the value.
   // This regresses both directions at once; if either the forward or
   // the reverse path misorders bytes, the roundtrip value diverges.
   const uint128 cases[] = {
      uint128{0ULL, 0ULL},
      uint128{0ULL, 1ULL},
      uint128{0ULL, 0xFFFFFFFFFFFFFFFFULL},
      uint128{1ULL, 0ULL},
      uint128{0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
      uint128{0x1234567890ABCDEFULL, 0xFEDCBA0987654321ULL}
   };
   for (const auto& original : cases) {
      bigint  bi  = static_cast<bigint>(original);
      uint128 rt  = uint128(bi);
      BOOST_CHECK_MESSAGE(rt == original,
                          "uint128 bigint roundtrip diverged: original=("
                          << original.hi << "," << original.lo << ") rt=("
                          << rt.hi << "," << rt.lo << ")");
   }
}

BOOST_AUTO_TEST_SUITE_END()
