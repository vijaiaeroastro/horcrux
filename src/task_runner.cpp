#include "vijai/task_runner.hpp"

#include "vijai/tooling.hpp"

#include <boost/asio.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/process.hpp>

#include <algorithm>
#include <exception>
#include <vector>

namespace vijai {
namespace {

bool is_within(const std::filesystem::path& child, const std::filesystem::path& parent) {
  const auto relative = child.lexically_relative(parent);
  return !relative.empty() && *relative.begin() != "..";
}

std::filesystem::path resolve_working_directory(const TaskConfig& task,
                                                const std::filesystem::path& project_root,
                                                std::string& error) {
  std::error_code filesystem_error;
  const auto root = std::filesystem::weakly_canonical(project_root, filesystem_error);
  if (filesystem_error) {
    error = "could not resolve project root";
    return {};
  }
  const auto requested = task.cwd ? root / *task.cwd : root;
  const auto working_directory = std::filesystem::weakly_canonical(requested, filesystem_error);
  if (filesystem_error || !std::filesystem::is_directory(working_directory)) {
    error = "task working directory does not exist";
    return {};
  }
  if (!is_within(working_directory, root) && working_directory != root) {
    error = "task working directory escapes the project root";
    return {};
  }
  return working_directory;
}

}  // namespace

TaskResult run_task(const TaskConfig& task, const std::filesystem::path& project_root) {
  TaskResult result;
  if (task.command.empty()) {
    result.error = "task command is empty";
    return result;
  }

  std::string cwd_error;
  const auto working_directory = resolve_working_directory(task, project_root, cwd_error);
  if (working_directory.empty()) {
    result.error = std::move(cwd_error);
    return result;
  }

  std::filesystem::path executable;
  std::vector<std::string> arguments;
  if (task.shell) {
    if (task.command.size() != 1U) {
      result.error = "shell tasks require one command string";
      return result;
    }
#ifdef _WIN32
    executable = find_executable("cmd.exe");
    arguments = {"/C", task.command.front()};
#else
    executable = find_executable("bash");
    arguments = {"-lc", task.command.front()};
#endif
  } else {
    executable = find_executable(task.command.front());
    arguments.assign(task.command.begin() + 1, task.command.end());
  }
  if (executable.empty()) {
    result.error = "task executable not found: " +
#ifdef _WIN32
                   (task.shell ? std::string("cmd.exe") : task.command.front());
#else
                   (task.shell ? std::string("bash") : task.command.front());
#endif
    return result;
  }

  try {
    namespace asio = boost::asio;
    namespace process = boost::process;
    asio::io_context context;
    asio::readable_pipe stdout_pipe(context);
    asio::readable_pipe stderr_pipe(context);
    boost::system::error_code stdout_error;
    boost::system::error_code stderr_error;

    process::process child(
        context, boost::filesystem::path(executable.string()), arguments,
        process::process_stdio{nullptr, stdout_pipe, stderr_pipe},
        process::process_start_dir(boost::filesystem::path(working_directory.string())));
    result.launched = true;

    asio::async_read(stdout_pipe, asio::dynamic_buffer(result.standard_output),
                     [&stdout_error](const boost::system::error_code& error, std::size_t) {
                       stdout_error = error;
                     });
    asio::async_read(stderr_pipe, asio::dynamic_buffer(result.standard_error),
                     [&stderr_error](const boost::system::error_code& error, std::size_t) {
                       stderr_error = error;
                     });
    context.run();
    result.exit_code = child.wait();
    if (stdout_error && stdout_error != asio::error::eof) {
      result.error = "failed while reading task stdout: " + stdout_error.message();
    } else if (stderr_error && stderr_error != asio::error::eof) {
      result.error = "failed while reading task stderr: " + stderr_error.message();
    }
  } catch (const std::exception& exception) {
    result.error = exception.what();
  }
  return result;
}

}  // namespace vijai
