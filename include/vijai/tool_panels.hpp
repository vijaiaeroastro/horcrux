#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace vijai {

// The docking registry is deliberately independent of the FTXUI workspace.
// A future built-in or external tool only needs a descriptor plus a panel
// implementation; it does not need to be wired through several UI lists.
enum class ToolWindow { find, search, tests, git, tasks, shell };

struct ToolPanelDescriptor {
  ToolWindow id;
  std::string_view config_name;
  std::string_view label;
};

class ToolPanelRegistry {
 public:
  [[nodiscard]] static const std::array<ToolPanelDescriptor, 6>& panels() noexcept;
  [[nodiscard]] static const ToolPanelDescriptor& descriptor(ToolWindow panel) noexcept;
  [[nodiscard]] static ToolWindow from_config(std::string_view value) noexcept;
  [[nodiscard]] static ToolWindow next(ToolWindow panel, int direction) noexcept;
};

}  // namespace vijai
