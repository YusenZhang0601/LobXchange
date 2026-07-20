#include "test_helpers/test_framework.hpp"

#include <string>

int main(int argc, char** argv) {
  const std::string filter = argc > 1 ? argv[1] : "";
  return lobx_test::run_all_tests(filter);
}
