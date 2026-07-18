#pragma once

#include <filesystem>

namespace horcrux {

struct AppPaths {
  std::filesystem::path config_file;
  std::filesystem::path state_directory;
};

AppPaths default_app_paths();

}  // namespace horcrux
