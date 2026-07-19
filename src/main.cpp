#include "horcrux/cli.hpp"
#include "horcrux/document.hpp"
#include "horcrux/git.hpp"
#include "horcrux/paths.hpp"
#include "horcrux/project.hpp"
#include "horcrux/tooling.hpp"
#include "horcrux/workspace.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }

  const auto parsed = horcrux::parse_cli(arguments);
  if (parsed.error) {
    std::cerr << "vijai: " << *parsed.error << "\nTry 'vijai --help' for usage.\n";
    return 2;
  }
  const auto& options = *parsed.options;
  if (options.action == horcrux::CliAction::help) {
    std::cout << horcrux::help_text();
    return 0;
  }
  if (options.action == horcrux::CliAction::version) {
    std::cout << "vijai 0.1.0-dev\n";
    return 0;
  }
  if (options.action == horcrux::CliAction::health) {
    const auto paths = horcrux::default_app_paths();
    std::cout << "Vijai health\n"
              << "config: " << paths.config_file.string() << "\n"
              << "state:  " << paths.state_directory.string() << "\n\n"
              << "Detected tools (nothing is installed automatically):\n";
    for (const auto& tool : horcrux::detect_developer_tools()) {
      std::cout << "  " << tool.name << ": "
                << (tool.available() ? tool.path.string() : "missing") << "\n";
    }
    return 0;
  }

  horcrux::Document document = horcrux::Document::untitled();
  std::optional<std::filesystem::path> requested_path;
  if (options.path) {
    const std::filesystem::path path(*options.path);
    requested_path = path;
    if (std::filesystem::is_directory(path)) {
      // A directory opens a project workspace. The file sidebar/multi-buffer
      // layer will provide document selection; for now it starts untitled.
    } else if (std::filesystem::exists(path)) {
      std::string error;
      const auto opened = horcrux::Document::open(path, error);
      if (!opened) {
        std::cerr << "horcrux: " << error << "\n";
        return 1;
      }
      document = std::move(*opened);
    } else {
      document = horcrux::Document::create(path);
    }
  }

  const auto paths = horcrux::default_app_paths();
  horcrux::ProjectContext project;
  const auto project_start = requested_path
                                 ? *requested_path
                                 : (document.has_path() ? document.path()
                                                        : std::filesystem::current_path());
  project.root = horcrux::discover_project_root(project_start);
  std::error_code workspace_error;
  if (requested_path && std::filesystem::is_directory(*requested_path)) {
    project.workspace_root = std::filesystem::weakly_canonical(*requested_path, workspace_error);
  } else if (project.root) {
    project.workspace_root = *project.root;
  } else if (document.has_path()) {
    project.workspace_root = std::filesystem::weakly_canonical(
        document.path().parent_path().empty() ? std::filesystem::current_path()
                                              : document.path().parent_path(),
        workspace_error);
  }
  if (project.root && !options.safe_mode) {
    project.trusted = horcrux::is_project_trusted(paths.state_directory, *project.root);
    const auto config_path = *project.root / "vijai.json";
    if (project.trusted && std::filesystem::exists(config_path)) {
      project.config = horcrux::load_project_config(config_path, project.config_error);
    }
  }

  const auto encoding_name = [](const horcrux::TextEncoding encoding) {
    switch (encoding) {
      case horcrux::TextEncoding::utf8: return "UTF-8";
      case horcrux::TextEncoding::utf8_bom: return "UTF-8 with BOM";
      case horcrux::TextEncoding::utf16_le: return "UTF-16 LE";
      case horcrux::TextEncoding::utf16_be: return "UTF-16 BE";
    }
    return "unknown";
  };

  if (horcrux::interactive_workspace_available()) {
    return horcrux::run_interactive_workspace(document, project, paths.state_directory, options.line,
                                            options.restore_session && !options.safe_mode);
  }

  std::cout << "Vijai project summary\n";
  if (project.workspace_root) {
    std::cout << "workspace: " << project.workspace_root->string() << "\n";
  } else {
    std::cout << "workspace: [none]\n";
  }
  std::cout << "document: " << (document.has_path() ? document.path().string() : "scratch document")
            << "\n"
            << "encoding: " << encoding_name(document.encoding()) << "\n"
            << "lines: " << document.buffer().line_count() << "\n";
  if (project.root) {
    std::cout << "config: " << (project.trusted ? "trusted" : "untrusted") << "\n";
  } else if (project.workspace_root) {
    std::cout << "config: no project config\n";
  }
  if (project.workspace_root) {
    const auto repository = horcrux::read_git_repository_info(*project.workspace_root);
    if (repository.available) {
      std::cout << "git: " << repository.branch << "  staged " << repository.staged
                << "  modified " << repository.modified << "  untracked " << repository.untracked
                << "\n";
    }
  }
  std::cout << "interactive workspace: unavailable (not attached to a terminal)\n";
  return 0;
}
