#include "vijai/cli.hpp"

#include <charconv>
#include <string_view>

namespace vijai {
namespace {

std::optional<unsigned long> parse_line(std::string_view value) {
  unsigned long line = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), line);
  if (error != std::errc{} || end != value.data() + value.size() || line == 0) {
    return std::nullopt;
  }
  return line;
}

}  // namespace

ParseResult parse_cli(const std::vector<std::string>& arguments) {
  CliOptions options;
  bool accept_options = true;

  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const std::string& argument = arguments[index];
    if (accept_options && argument == "--") {
      accept_options = false;
    } else if (accept_options && (argument == "--help" || argument == "-h")) {
      options.action = CliAction::help;
    } else if (accept_options && argument == "--version") {
      options.action = CliAction::version;
    } else if (accept_options && argument == "--health") {
      options.action = CliAction::health;
    } else if (accept_options && argument == "--wait") {
      options.wait = true;
    } else if (accept_options && argument == "--safe") {
      options.safe_mode = true;
    } else if (accept_options && argument == "--no-restore") {
      options.restore_session = false;
    } else if (accept_options && argument == "--config") {
      if (++index == arguments.size()) {
        return {.options = std::nullopt, .error = "--config needs a path"};
      }
      options.config_path = arguments[index];
    } else if (accept_options && argument.starts_with('+')) {
      const auto line = parse_line(std::string_view(argument).substr(1));
      if (!line) {
        return {.options = std::nullopt, .error = "line must be a positive integer"};
      }
      options.line = *line;
    } else if (accept_options && argument.starts_with('-')) {
      return {.options = std::nullopt, .error = "unknown option: " + argument};
    } else if (!options.path) {
      options.path = argument;
    } else {
      return {.options = std::nullopt, .error = "only one path may be opened at a time"};
    }
  }
  return {.options = std::move(options), .error = std::nullopt};
}

std::string help_text() {
  return R"(Vijai — personal terminal code editor

Usage: vijai [OPTIONS] [PATH]

  +LINE          Open PATH at a positive line number
  --wait          Wait until the opened buffer is closed
  --safe          Do not trust project configuration or restore project state
  --no-restore    Start without restoring the previous workspace
  --config PATH   Use this global configuration file
  --health        Report detected tooling and configuration paths
  --version       Print Vijai version
  -h, --help      Show this help
)";
}

}  // namespace vijai
