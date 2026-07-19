#pragma once

#include "vijai/document.hpp"
#include "vijai/project.hpp"

#include <cstddef>
#include <optional>

namespace vijai {

[[nodiscard]] bool interactive_workspace_available() noexcept;
int run_interactive_workspace(Document& document, ProjectContext& project,
                              const std::filesystem::path& state_directory,
                              std::optional<std::size_t> initial_line,
                              bool restore_session);

}  // namespace vijai
