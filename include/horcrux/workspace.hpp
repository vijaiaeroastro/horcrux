#pragma once

#include "horcrux/document.hpp"
#include "horcrux/project.hpp"

#include <cstddef>
#include <optional>

namespace horcrux {

[[nodiscard]] bool interactive_workspace_available() noexcept;
int run_interactive_workspace(Document& document, ProjectContext& project,
                              const std::filesystem::path& state_directory,
                              std::optional<std::size_t> initial_line,
                              bool restore_session);

}  // namespace horcrux
