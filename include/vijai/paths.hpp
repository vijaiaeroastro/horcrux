#pragma once

#include <filesystem>

namespace vijai {

struct AppPaths {
  std::filesystem::path config_file;
  std::filesystem::path state_directory;
};

AppPaths default_app_paths();

}  // namespace vijai
