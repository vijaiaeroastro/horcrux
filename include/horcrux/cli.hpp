#pragma once

#include <optional>
#include <string>
#include <vector>

namespace horcrux {

enum class CliAction { launch, help, version, health };

struct CliOptions {
  CliAction action{CliAction::launch};
  std::optional<std::string> path;
  std::optional<unsigned long> line;
  std::optional<std::string> config_path;
  bool wait{false};
  bool safe_mode{false};
  bool restore_session{true};
};

struct ParseResult {
  std::optional<CliOptions> options;
  std::optional<std::string> error;
};

ParseResult parse_cli(const std::vector<std::string>& arguments);
std::string help_text();

}  // namespace horcrux
