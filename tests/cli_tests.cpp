#include "horcrux/cli.hpp"
#include "horcrux/paths.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
  {
    const auto result = horcrux::parse_cli({"horcrux", "+42", "notes.cpp", "--wait"});
    assert(result.options);
    assert(result.options->path == "notes.cpp");
    assert(result.options->line == 42UL);
    assert(result.options->wait);
  }
  {
    const auto result = horcrux::parse_cli({"horcrux", "--config", "settings.json", "--safe"});
    assert(result.options);
    assert(result.options->config_path == "settings.json");
    assert(result.options->safe_mode);
  }
  {
    const auto result = horcrux::parse_cli({"horcrux", "+0"});
    assert(result.error == "line must be a positive integer");
  }
  {
    const auto result = horcrux::parse_cli({"horcrux", "one", "two"});
    assert(result.error == "only one path may be opened at a time");
  }
  {
    const auto paths = horcrux::default_app_paths();
    assert(!paths.config_file.empty());
    assert(!paths.state_directory.empty());
  }
}
