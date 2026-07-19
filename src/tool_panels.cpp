#include "vijai/tool_panels.hpp"

namespace vijai {

const std::array<ToolPanelDescriptor, 6>& ToolPanelRegistry::panels() noexcept {
  static constexpr std::array<ToolPanelDescriptor, 6> panels{{
      {ToolWindow::find, "find", " Find "},
      {ToolWindow::search, "search", " Search "},
      {ToolWindow::tests, "tests", " Tests "},
      {ToolWindow::git, "git", " Git "},
      {ToolWindow::tasks, "tasks", " Tasks "},
      {ToolWindow::shell, "shell", " Shell "},
  }};
  return panels;
}

const ToolPanelDescriptor& ToolPanelRegistry::descriptor(const ToolWindow panel) noexcept {
  return panels()[static_cast<std::size_t>(panel)];
}

ToolWindow ToolPanelRegistry::from_config(const std::string_view value) noexcept {
  for (const auto& panel : panels()) {
    if (panel.config_name == value) return panel.id;
  }
  return ToolWindow::search;
}

ToolWindow ToolPanelRegistry::next(const ToolWindow panel, const int direction) noexcept {
  const int count = static_cast<int>(panels().size());
  auto index = static_cast<int>(panel);
  index = (index + direction % count + count) % count;
  return panels()[static_cast<std::size_t>(index)].id;
}

}  // namespace vijai
