#include "horcrux/paths.hpp"

#include <cstdlib>

namespace horcrux {
namespace {

std::filesystem::path environment_path(const char* name, const std::filesystem::path& fallback) {
  if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
    return value;
  }
  return fallback;
}

}  // namespace

AppPaths default_app_paths() {
#ifdef _WIN32
  const auto app_data = environment_path("APPDATA", ".");
  const auto local_app_data = environment_path("LOCALAPPDATA", app_data);
  return {.config_file = app_data / "Vijai" / "config.json",
          .state_directory = local_app_data / "Vijai"};
#elif defined(__APPLE__)
  const auto home = environment_path("HOME", ".");
  const auto base = home / "Library" / "Application Support" / "Vijai";
  return {.config_file = base / "config.json", .state_directory = base / "state"};
#else
  const auto home = environment_path("HOME", ".");
  const auto config_home = environment_path("XDG_CONFIG_HOME", home / ".config");
  const auto state_home = environment_path("XDG_STATE_HOME", home / ".local" / "state");
  return {.config_file = config_home / "vijai" / "config.json",
          .state_directory = state_home / "vijai"};
#endif
}

}  // namespace horcrux
