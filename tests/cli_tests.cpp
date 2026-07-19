#include "vijai/cli.hpp"
#include "vijai/paths.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
  {
    const auto result = vijai::parse_cli({"vijai", "+42", "notes.cpp", "--wait"});
    assert(result.options);
    assert(result.options->path == "notes.cpp");
    assert(result.options->line == 42UL);
    assert(result.options->wait);
  }
  {
    const auto result = vijai::parse_cli({"vijai", "--config", "settings.json", "--safe"});
    assert(result.options);
    assert(result.options->config_path == "settings.json");
    assert(result.options->safe_mode);
  }
  {
    const auto result = vijai::parse_cli({"vijai", "+0"});
    assert(result.error == "line must be a positive integer");
  }
  {
    const auto result = vijai::parse_cli({"vijai", "one", "two"});
    assert(result.error == "only one path may be opened at a time");
  }
  {
    const auto paths = vijai::default_app_paths();
    assert(!paths.config_file.empty());
    assert(!paths.state_directory.empty());
  }
}
