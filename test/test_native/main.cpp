// GoogleTest runner. PlatformIO does not inject one when googletest is pulled
// in via lib_deps, so we provide the standard entry point.
#include <gtest/gtest.h>

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
