#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace horcrux {

struct ToolStatus {
  std::string name;
  std::filesystem::path path;

  [[nodiscard]] bool available() const noexcept { return !path.empty(); }
};

[[nodiscard]] std::filesystem::path find_executable(const std::string& name);
[[nodiscard]] std::vector<ToolStatus> detect_developer_tools();

}  // namespace horcrux
