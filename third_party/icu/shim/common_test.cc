// Basic smoke test to ensure that ICU shim works properly.

#include "unicode/uidna.h"
#include <cstdlib>
#include <iostream>

#define ASSERT_EQ(v1, v2) \
    if ((v1) != (v2)) { \
      std::cerr << "Expected equality of" << std::endl \
        << "  " << #v1 << " (equal to " << (v1) << ")" << std::endl \
        << "and" << std::endl \
        << "  " << #v2 << " (equal to " << (v2) << ")" << std::endl; \
      return 1; \
    }

int main(int argc, char** argv) {
  UIDNAInfo info = UIDNA_INFO_INITIALIZER;
  UErrorCode err = U_ZERO_ERROR;

  {
    uidna_nameToASCII(nullptr, nullptr, 0, nullptr, 0, &info, &err);

    ASSERT_EQ(info.errors, 0x80);
    ASSERT_EQ(err, U_ILLEGAL_ARGUMENT_ERROR);
  }

  {
    UChar data[] = {'1', '2', '3'};
    UChar* src = &data[0];
    uidna_nameToASCII(nullptr, src, 2, nullptr, *src, &info, &err);

    ASSERT_EQ(info.errors, 0x80);
    ASSERT_EQ(err, U_ILLEGAL_ARGUMENT_ERROR);
  }

  return 0;
}
