#pragma once

#include "vijai/config.hpp"

#include <filesystem>
#include <string>

namespace vijai {

struct TaskResult {
  bool launched{false};
  int exit_code{-1};
  std::string standard_output;
  std::string standard_error;
  std::string error;
};

[[nodiscard]] TaskResult run_task(const TaskConfig& task,
                                  const std::filesystem::path& project_root);

}  // namespace vijai
