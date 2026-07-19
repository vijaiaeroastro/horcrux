#include "vijai/tool_panels.hpp"

#include <cassert>

namespace {

void tool_panel_registry_tests() {
  const auto& panels = vijai::ToolPanelRegistry::panels();
  assert(panels.size() == 6U);
  assert(vijai::ToolPanelRegistry::from_config("git") == vijai::ToolWindow::git);
  assert(vijai::ToolPanelRegistry::from_config("unknown") == vijai::ToolWindow::search);
  assert(vijai::ToolPanelRegistry::descriptor(vijai::ToolWindow::shell).label == " Shell ");
  assert(vijai::ToolPanelRegistry::next(vijai::ToolWindow::shell, 1) == vijai::ToolWindow::find);
  assert(vijai::ToolPanelRegistry::next(vijai::ToolWindow::find, -1) == vijai::ToolWindow::shell);
}

struct RunToolPanelRegistryTests {
  RunToolPanelRegistryTests() { tool_panel_registry_tests(); }
};

RunToolPanelRegistryTests run_tool_panel_registry_tests;

}  // namespace
