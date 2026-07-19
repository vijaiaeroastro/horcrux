#include "vijai/file_tree.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace vijai {
namespace {

std::string uppercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

bool likely_license_filename(const std::filesystem::path& path) {
  const auto name = uppercase(path.filename().string());
  return name == "LICENSE" || name == "LICENCE" || name.starts_with("LICENSE.") ||
         name.starts_with("LICENCE.") || name == "COPYING" || name.starts_with("COPYING.");
}

}  // namespace

std::string detect_license_kind(const std::string_view contents) {
  const auto text = uppercase(std::string(contents));
  const auto has = [&text](const std::string_view value) { return text.find(value) != std::string::npos; };
  if (const auto spdx = text.find("SPDX-LICENSE-IDENTIFIER:"); spdx != std::string::npos) {
    const auto start = spdx + std::string_view("SPDX-LICENSE-IDENTIFIER:").size();
    const auto end = text.find_first_of("\r\n", start);
    auto identifier = std::string(contents.substr(start, end == std::string::npos ? contents.size() - start
                                                                                   : end - start));
    while (!identifier.empty() && std::isspace(static_cast<unsigned char>(identifier.front()))) {
      identifier.erase(identifier.begin());
    }
    if (!identifier.empty()) return identifier;
  }
  if (has("MIT LICENSE")) return "MIT";
  if (has("APACHE LICENSE") && has("VERSION 2.0")) return "Apache-2.0";
  if (has("GNU GENERAL PUBLIC LICENSE") && has("VERSION 3")) return "GPL-3.0";
  if (has("GNU GENERAL PUBLIC LICENSE") && has("VERSION 2")) return "GPL-2.0";
  if (has("GNU LESSER GENERAL PUBLIC LICENSE") && has("VERSION 3")) return "LGPL-3.0";
  if (has("BSD 3-CLAUSE") || has("REDISTRIBUTION AND USE IN SOURCE AND BINARY FORMS")) {
    return "BSD";
  }
  if (has("MOZILLA PUBLIC LICENSE")) return "MPL-2.0";
  if (has("ECLIPSE PUBLIC LICENSE")) return "EPL";
  if (has("BOOST SOFTWARE LICENSE")) return "BSL-1.0";
  if (has("THE UNLICENSE")) return "Unlicense";
  if (has("ISC LICENSE")) return "ISC";
  return "License";
}

FileTree::FileTree(std::filesystem::path root, const std::size_t maximum_entries)
    : root_(std::move(root)), maximum_entries_(maximum_entries) {}

bool FileTree::refresh(std::string& error) {
  error.clear();
  std::error_code filesystem_error;
  root_ = std::filesystem::weakly_canonical(root_, filesystem_error);
  if (filesystem_error || !std::filesystem::is_directory(root_)) {
    error = "workspace root is not a readable directory";
    return false;
  }
  const auto selected_path =
      selected_entry() ? std::optional(selected_entry()->relative_path) : std::nullopt;
  entries_.clear();
  if (!append_directory({}, 0U, error)) return false;
  selected_index_ = 0U;
  if (selected_path) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&selected_path](const FileTreeEntry& entry) {
                                      return entry.relative_path == *selected_path;
                                    });
    if (found != entries_.end()) {
      selected_index_ = static_cast<std::size_t>(std::distance(entries_.begin(), found));
    }
  }
  return true;
}

const std::filesystem::path& FileTree::root() const noexcept { return root_; }

const std::vector<FileTreeEntry>& FileTree::entries() const noexcept { return entries_; }

std::size_t FileTree::selected_index() const noexcept { return selected_index_; }

const FileTreeEntry* FileTree::selected_entry() const noexcept {
  return selected_index_ < entries_.size() ? &entries_[selected_index_] : nullptr;
}

void FileTree::select(const std::size_t index) noexcept {
  if (!entries_.empty()) selected_index_ = std::min(index, entries_.size() - 1U);
}

void FileTree::select_previous() noexcept {
  if (selected_index_ > 0U) --selected_index_;
}

void FileTree::select_next() noexcept {
  if (selected_index_ + 1U < entries_.size()) ++selected_index_;
}

bool FileTree::toggle_selected(std::string& error) {
  const auto* entry = selected_entry();
  if (entry == nullptr || !entry->directory) return false;
  if (expanded_directories_.contains(entry->relative_path)) {
    expanded_directories_.erase(entry->relative_path);
  } else {
    expanded_directories_.insert(entry->relative_path);
  }
  return refresh(error);
}

bool FileTree::toggle_hidden_files(std::string& error) {
  showing_hidden_files_ = !showing_hidden_files_;
  return refresh(error);
}

bool FileTree::showing_hidden_files() const noexcept { return showing_hidden_files_; }

bool FileTree::append_directory(const std::filesystem::path& relative_directory,
                                const std::size_t depth, std::string& error) {
  std::error_code filesystem_error;
  std::vector<std::filesystem::directory_entry> children;
  for (std::filesystem::directory_iterator iterator(root_ / relative_directory, filesystem_error), end;
       !filesystem_error && iterator != end; iterator.increment(filesystem_error)) {
    const auto relative = relative_directory / iterator->path().filename();
    if (!should_ignore(relative)) children.push_back(*iterator);
  }
  if (filesystem_error) {
    error = "could not read workspace directory " + (root_ / relative_directory).string();
    return false;
  }
  std::sort(children.begin(), children.end(),
            [](const auto& left, const auto& right) {
              std::error_code left_error;
              std::error_code right_error;
              const bool left_directory = left.is_directory(left_error);
              const bool right_directory = right.is_directory(right_error);
              if (left_directory != right_directory) return left_directory > right_directory;
              return left.path().filename().string() < right.path().filename().string();
            });

  for (const auto& child : children) {
    if (entries_.size() >= maximum_entries_) {
      error = "workspace file tree exceeded " + std::to_string(maximum_entries_) + " entries";
      return false;
    }
    const auto relative = relative_directory / child.path().filename();
    const bool directory = child.is_directory(filesystem_error) && !child.is_symlink(filesystem_error);
    if (filesystem_error) {
      filesystem_error.clear();
      continue;
    }
    const bool expanded = directory && expanded_directories_.contains(relative);
    entries_.push_back({
        .relative_path = relative,
        .depth = depth,
        .directory = directory,
        .expanded = expanded,
        .license_kind = directory ? std::string{} : detect_license_file(child.path()),
    });
    if (expanded && !append_directory(relative, depth + 1U, error)) return false;
  }
  return true;
}

std::string FileTree::detect_license_file(const std::filesystem::path& path) const {
  if (!likely_license_filename(path)) return {};
  std::ifstream input(path, std::ios::binary);
  if (!input) return "License";
  std::string contents(64U * 1024U, '\0');
  input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  contents.resize(static_cast<std::size_t>(input.gcount()));
  return detect_license_kind(contents);
}

bool FileTree::should_ignore(const std::filesystem::path& relative_path) const {
  const auto name = relative_path.filename().string();
  if (!showing_hidden_files_ && name.starts_with('.')) return true;
  return name == ".git" || name == ".deps" || name == "vcpkg_installed" ||
         name == ".cache" || name == "__pycache__" || name == "build" || name.starts_with("build-") ||
         name.starts_with("cmake-build-");
}

}  // namespace vijai
