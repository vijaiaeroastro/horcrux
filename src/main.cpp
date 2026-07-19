#include "vijai/cli.hpp"
#include "vijai/document.hpp"
#include "vijai/git.hpp"
#include "vijai/paths.hpp"
#include "vijai/project.hpp"
#include "vijai/tooling.hpp"
#include "vijai/workspace.hpp"

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

  const auto parsed = vijai::parse_cli(arguments);
  if (parsed.error) {
    std::cerr << "vijai: " << *parsed.error << "\nTry 'vijai --help' for usage.\n";
    return 2;
  }
  const auto& options = *parsed.options;
  if (options.action == vijai::CliAction::help) {
    std::cout << vijai::help_text();
    return 0;
  }
  if (options.action == vijai::CliAction::version) {
    std::cout << "vijai 0.1.0-dev\n";
    return 0;
  }
  if (options.action == vijai::CliAction::health) {
    const auto paths = vijai::default_app_paths();
    std::cout << "Vijai health\n"
              << "config: " << paths.config_file.string() << "\n"
              << "state:  " << paths.state_directory.string() << "\n\n"
              << "Detected tools (nothing is installed automatically):\n";
    for (const auto& tool : vijai::detect_developer_tools()) {
      std::cout << "  " << tool.name << ": "
                << (tool.available() ? tool.path.string() : "missing") << "\n";
    }
    return 0;
  }

  vijai::Document document = vijai::Document::untitled();
  std::optional<std::filesystem::path> requested_path;
  if (options.path) {
    const std::filesystem::path path(*options.path);
    requested_path = path;
    if (std::filesystem::is_directory(path)) {
      // A directory opens a project workspace. The file sidebar/multi-buffer
      // layer will provide document selection; for now it starts untitled.
    } else if (std::filesystem::exists(path)) {
      std::string error;
      const auto opened = vijai::Document::open(path, error);
      if (!opened) {
        std::cerr << "vijai: " << error << "\n";
        return 1;
      }
      document = std::move(*opened);
    } else {
      document = vijai::Document::create(path);
    }
  }

  const auto paths = vijai::default_app_paths();
  vijai::ProjectContext project;
  const auto project_start = requested_path
                                 ? *requested_path
                                 : (document.has_path() ? document.path()
                                                        : std::filesystem::current_path());
  project.root = vijai::discover_project_root(project_start);
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
    project.trusted = vijai::is_project_trusted(paths.state_directory, *project.root);
    const auto config_path = *project.root / "vijai.json";
    if (project.trusted && std::filesystem::exists(config_path)) {
      project.config = vijai::load_project_config(config_path, project.config_error);
    }
  }

  const auto encoding_name = [](const vijai::TextEncoding encoding) {
    switch (encoding) {
      case vijai::TextEncoding::utf8: return "UTF-8";
      case vijai::TextEncoding::utf8_bom: return "UTF-8 with BOM";
      case vijai::TextEncoding::utf16_le: return "UTF-16 LE";
      case vijai::TextEncoding::utf16_be: return "UTF-16 BE";
    }
    return "unknown";
  };

  if (vijai::interactive_workspace_available()) {
    return vijai::run_interactive_workspace(document, project, paths.state_directory, options.line,
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
    const auto repository = vijai::read_git_repository_info(*project.workspace_root);
    if (repository.available) {
      std::cout << "git: " << repository.branch << "  staged " << repository.staged
                << "  modified " << repository.modified << "  untracked " << repository.untracked
                << "\n";
    }
  }
  std::cout << "interactive workspace: unavailable (not attached to a terminal)\n";
  return 0;
}
