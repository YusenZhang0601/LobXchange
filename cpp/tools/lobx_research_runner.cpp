#include "lobx/simulation/research_cli.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

  const lobx::sim::ResearchCliResult result = lobx::sim::run_research_cli(args);

  if (!result.stdout_text.empty()) std::cout << result.stdout_text;
  if (!result.stderr_text.empty()) std::cerr << result.stderr_text;

  return result.exit_code;
}
