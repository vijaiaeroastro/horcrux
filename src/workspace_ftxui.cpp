#include "horcrux/workspace.hpp"

#include "horcrux/config.hpp"
#include "horcrux/editor_buffer.hpp"
#include "horcrux/file_tree.hpp"
#include "horcrux/git.hpp"
#include "horcrux/project_search.hpp"
#include "horcrux/project_search_job.hpp"
#include "horcrux/recovery.hpp"
#include "horcrux/session.hpp"
#include "horcrux/syntax_highlighter.hpp"
#include "horcrux/task_runner.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace horcrux {
namespace {

using namespace ftxui;

constexpr int sidebar_width = 30;
enum class ThemeKind { midnight, high_contrast, paper };
ThemeKind active_theme = ThemeKind::midnight;

std::string_view theme_name() {
  switch (active_theme) {
    case ThemeKind::midnight: return "Midnight";
    case ThemeKind::high_contrast: return "High Contrast";
    case ThemeKind::paper: return "Paper";
  }
  return "Midnight";
}

// FTXUI's colour support is initialized with its terminal app.  These must be
// functions, not global Color objects, or Color::RGB runs before that setup.
Color theme_background() {
  if (active_theme == ThemeKind::high_contrast) return Color::RGB(0, 0, 0);
  if (active_theme == ThemeKind::paper) return Color::RGB(248, 248, 248);
  return Color::RGB(20, 23, 27);
}
Color theme_panel() {
  if (active_theme == ThemeKind::high_contrast) return Color::RGB(20, 20, 20);
  if (active_theme == ThemeKind::paper) return Color::RGB(235, 238, 242);
  return Color::RGB(27, 31, 37);
}
Color theme_raised() {
  if (active_theme == ThemeKind::high_contrast) return Color::RGB(48, 48, 48);
  if (active_theme == ThemeKind::paper) return Color::RGB(215, 220, 226);
  return Color::RGB(35, 40, 47);
}
Color theme_foreground() {
  if (active_theme == ThemeKind::high_contrast) return Color::RGB(255, 255, 255);
  if (active_theme == ThemeKind::paper) return Color::RGB(35, 38, 43);
  return Color::RGB(219, 224, 230);
}
Color theme_muted() {
  if (active_theme == ThemeKind::high_contrast) return Color::RGB(190, 190, 190);
  if (active_theme == ThemeKind::paper) return Color::RGB(98, 106, 116);
  return Color::RGB(130, 141, 153);
}
Color theme_teal() {
  if (active_theme == ThemeKind::high_contrast) return Color::RGB(0, 229, 255);
  if (active_theme == ThemeKind::paper) return Color::RGB(0, 115, 132);
  return Color::RGB(73, 196, 181);
}
Color theme_amber() {
  if (active_theme == ThemeKind::high_contrast) return Color::RGB(255, 196, 0);
  if (active_theme == ThemeKind::paper) return Color::RGB(165, 91, 0);
  return Color::RGB(229, 174, 83);
}
Color syntax_color(const SyntaxKind kind) {
  switch (kind) {
    case SyntaxKind::keyword: return Color::RGB(198, 120, 221);
    case SyntaxKind::type: return Color::RGB(86, 182, 194);
    case SyntaxKind::string: return Color::RGB(152, 195, 121);
    case SyntaxKind::comment: return Color::RGB(106, 132, 151);
    case SyntaxKind::number: return Color::RGB(229, 174, 83);
    case SyntaxKind::preprocessor: return Color::RGB(224, 108, 117);
    case SyntaxKind::plain: return theme_foreground();
  }
  return theme_foreground();
}

std::tm utc_time(const std::time_t value) {
  std::tm result{};
#ifdef _WIN32
  gmtime_s(&result, &value);
#else
  gmtime_r(&value, &result);
#endif
  return result;
}

int weekday(const int year, const int month, const int day) {
  // Sakamoto's algorithm.  Its result is Sunday = 0 through Saturday = 6.
  constexpr std::array<int, 12> month_offsets{0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  const int adjusted_year = year - (month < 3 ? 1 : 0);
  return (adjusted_year + adjusted_year / 4 - adjusted_year / 100 + adjusted_year / 400 +
          month_offsets[static_cast<std::size_t>(month - 1)] + day) %
         7;
}

int last_sunday(const int year, const int month, const int days_in_month) {
  return days_in_month - weekday(year, month, days_in_month);
}

bool central_european_summer_time(const std::tm& utc) {
  const int year = utc.tm_year + 1900;
  const int month = utc.tm_mon + 1;
  const int day = utc.tm_mday;
  const int march_transition = last_sunday(year, 3, 31);
  const int october_transition = last_sunday(year, 10, 31);
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
  if (month == 3) {
    return day > march_transition || (day == march_transition && utc.tm_hour >= 1);
  }
  return day < october_transition || (day == october_transition && utc.tm_hour < 1);
}

std::string format_clock(const std::time_t now, const int offset_seconds) {
  const auto shifted = utc_time(now + offset_seconds);
  std::ostringstream stream;
  stream << std::put_time(&shifted, "%d %b %H:%M");
  return stream.str();
}

std::string dual_clock_text() {
  const auto now = std::time(nullptr);
  const auto utc = utc_time(now);
  const bool summer_time = central_european_summer_time(utc);
  const int central_offset = (summer_time ? 2 : 1) * 60 * 60;
  const std::string central_label = summer_time ? "CEST" : "CET";
  return central_label + " " + format_clock(now, central_offset) +
         "  |  IST " + format_clock(now, 5 * 60 * 60 + 30 * 60);
}

struct Position {
  std::size_t line{0};
  std::size_t column{0};
  std::size_t line_start{0};
  std::size_t line_end{0};
};

Position position_at(const std::string& text_value, const std::size_t offset_value) {
  const std::size_t offset = std::min(offset_value, text_value.size());
  Position result;
  for (std::size_t index = 0; index < offset; ++index) {
    if (text_value[index] == '\n') {
      ++result.line;
      result.line_start = index + 1U;
    }
  }
  result.column = offset - result.line_start;
  result.line_end = text_value.find('\n', result.line_start);
  if (result.line_end == std::string::npos) result.line_end = text_value.size();
  if (result.line_end > result.line_start && text_value[result.line_end - 1U] == '\r') {
    --result.line_end;
  }
  return result;
}

std::size_t line_start_at(const std::string& text_value, const std::size_t target_line) {
  if (target_line == 0U) return 0U;
  std::size_t line = 0U;
  for (std::size_t index = 0; index < text_value.size(); ++index) {
    if (text_value[index] == '\n' && ++line == target_line) return index + 1U;
  }
  return text_value.size();
}

std::size_t offset_at(const std::string& text_value, const std::size_t line,
                      const std::size_t column) {
  const auto start = line_start_at(text_value, line);
  auto end = text_value.find('\n', start);
  if (end == std::string::npos) end = text_value.size();
  if (end > start && text_value[end - 1U] == '\r') --end;
  return std::min(start + column, end);
}

std::string display_name(const Document& document) {
  if (!document.has_path()) return "Untitled";
  const auto filename = document.path().filename().string();
  return filename.empty() ? document.path().string() : filename;
}

enum class Focus { tree, editor, tools };
enum class InteractionMode { edit, navigate };
enum class Prompt {
  none, find, project_search, open_file, shell_command, commit, save_as, quit_dirty, close_dirty, help
};
enum class ToolWindow { find, search, git, tasks, shell };

struct HitTarget {
  ftxui::Box box;
  std::size_t index{0};
};

struct FindMatch {
  std::size_t offset{0};
  std::size_t line{0};
  std::size_t column{0};
  std::string preview;
};

class Workspace {
 public:
  Workspace(Document initial_document, ProjectContext& project,
            std::filesystem::path state_directory, const bool restore_session)
      : project_(project), state_directory_(std::move(state_directory)) {
    buffers_.push_back(std::make_unique<EditorBuffer>(
        std::move(initial_document), state_directory_, restore_session));
    if (project_.workspace_root) {
      tree_.emplace(*project_.workspace_root);
      std::string error;
      if (!tree_->refresh(error)) status_ = error;
    }
    refresh_repository_info();
  }

  void go_to_line(const std::size_t one_based_line) {
    if (one_based_line == 0U) return;
    active().set_cursor(
        line_start_at(active().document().buffer().text(), one_based_line - 1U));
    active().set_desired_column(0U);
    active().set_top_line(one_based_line - 1U);
  }

  EditorBuffer& active() { return *buffers_[active_buffer_]; }
  const EditorBuffer& active() const { return *buffers_[active_buffer_]; }

  Element render() {
    highlighter_.set_path(active().document().path());
    highlighter_.set_source(active().document().buffer().text());
    const auto dimensions = Terminal::Size();
    const int body_height = std::max(4, dimensions.dimy - 4);
    const int editor_width =
        std::max(20, dimensions.dimx - (tree_visible_ && tree_ ? sidebar_width + 1 : 0));

    const int tool_height = output_visible_ ? std::min(12, std::max(5, body_height / 3)) : 0;
    const int editor_height = body_height - tool_height;
    Element body = render_editor(editor_width, editor_height);
    if (tree_visible_ && tree_) {
      body = hbox({
          render_tree(editor_height) | size(WIDTH, EQUAL, sidebar_width),
          separator() | color(theme_raised()),
          std::move(body) | flex,
      });
    }

    if (output_visible_) {
      body = vbox({
          std::move(body) | flex,
          separator() | color(theme_raised()),
          render_tool_tabs(),
          render_output(dimensions.dimx, std::max(1, tool_height - 2)),
      });
    }

    auto content = vbox({
        render_tabs(),
        std::move(body) | flex,
        render_status(),
        render_keys(),
    });
    if (prompt_ != Prompt::none) {
      content = dbox({std::move(content), render_modal(dimensions.dimx, dimensions.dimy)});
    }
    if (theme_picker_visible_) {
      content = dbox({std::move(content), render_theme_picker()});
    }
    return content | bgcolor(theme_background()) | color(theme_foreground());
  }

  bool event(const Event& event, App& app) {
    if (theme_picker_visible_) return theme_picker_event(event);
    if (prompt_ != Prompt::none) return prompt_event(event, app);
    if (event.is_mouse() && mouse_event(event)) return true;

    if (event == Event::Escape) {
      if (interaction_mode_ == InteractionMode::edit) {
        interaction_mode_ = InteractionMode::navigate;
        status_ = "NAV mode · i to edit";
      } else if (output_visible_) {
        output_visible_ = false;
        output_focus_ = false;
        find_focus_ = false;
        git_focus_ = false;
        status_ = "Tools hidden";
      } else {
        interaction_mode_ = InteractionMode::edit;
        focus_ = Focus::editor;
        status_ = "EDIT mode";
      }
      return true;
    }
    if (interaction_mode_ == InteractionMode::navigate && event == Event::i) {
      interaction_mode_ = InteractionMode::edit;
      focus_ = Focus::editor;
      status_ = "EDIT mode";
      return true;
    }
    if (interaction_mode_ == InteractionMode::navigate && event == Event::Tab) {
      cycle_focus();
      return true;
    }

    if (focus_ == Focus::tools && active_tool_window_ == ToolWindow::search && output_focus_ && output_visible_ &&
        !search_results_.empty()) {
      if (event == Event::ArrowUp || event == Event::k) {
        if (search_selected_ > 0U) --search_selected_;
        return true;
      }
      if (event == Event::ArrowDown || event == Event::j) {
        if (search_selected_ + 1U < search_results_.size()) ++search_selected_;
        return true;
      }
      if (event == Event::PageUp) {
        search_selected_ = search_selected_ > 8U ? search_selected_ - 8U : 0U;
        return true;
      }
      if (event == Event::PageDown) {
        search_selected_ = std::min(search_results_.size() - 1U, search_selected_ + 8U);
        return true;
      }
      if (event == Event::Return) {
        open_selected_search_result();
        return true;
      }
      if (event.is_character()) return true;
    }

    if (focus_ == Focus::tools && active_tool_window_ == ToolWindow::find && find_focus_ && output_visible_ &&
        !find_results_.empty()) {
      if (event == Event::ArrowUp || event == Event::k) {
        if (find_selected_ > 0U) --find_selected_;
        return true;
      }
      if (event == Event::ArrowDown || event == Event::j) {
        if (find_selected_ + 1U < find_results_.size()) ++find_selected_;
        return true;
      }
      if (event == Event::PageUp) {
        find_selected_ = find_selected_ > 8U ? find_selected_ - 8U : 0U;
        return true;
      }
      if (event == Event::PageDown) {
        find_selected_ = std::min(find_results_.size() - 1U, find_selected_ + 8U);
        return true;
      }
      if (event == Event::Return) {
        open_selected_find_result();
        return true;
      }
      if (event.is_character()) return true;
    }

    if (focus_ == Focus::tools && active_tool_window_ == ToolWindow::git && git_focus_ && output_visible_) {
      if (event == Event::ArrowUp || event == Event::k) {
        if (git_selected_ > 0U) --git_selected_;
        return true;
      }
      if (event == Event::ArrowDown || event == Event::j) {
        if (git_selected_ + 1U < git_status_.entries.size()) ++git_selected_;
        return true;
      }
      if (event == Event::Return) {
        toggle_selected_git_entry();
        return true;
      }
      if (event == Event::r || event == Event::R) {
        show_git_status();
        return true;
      }
      if (event == Event::c || event == Event::C) {
        prompt_text_.clear();
        prompt_ = Prompt::commit;
        return true;
      }
      if (event.is_character()) return true;
    }

    if (event == Event::CtrlQ) {
      if (has_dirty_buffers()) {
        prompt_ = Prompt::quit_dirty;
      } else {
        quit(app);
      }
      return true;
    }
    if (event == Event::CtrlS) {
      save_active();
      return true;
    }
    if (event == Event::CtrlF) {
      prompt_text_.clear();
      prompt_ = Prompt::find;
      return true;
    }
    if (event == Event::CtrlB) {
      if (tree_) {
        tree_visible_ = true;
        focus_ = focus_ == Focus::tree ? Focus::editor : Focus::tree;
      }
      return true;
    }
    if (event == Event::CtrlN) {
      switch_buffer(1);
      return true;
    }
    if (event == Event::CtrlW) {
      request_close_buffer(active_buffer_);
      return true;
    }
    if (event == Event::CtrlP) {
      switch_buffer(-1);
      return true;
    }
    if (event == Event::CtrlZ) {
      if (active().document().buffer().undo()) {
        clamp_cursor();
        journal_active();
        status_ = "Undo";
      }
      return true;
    }
    if (event == Event::CtrlY) {
      if (active().document().buffer().redo()) {
        clamp_cursor();
        journal_active();
        status_ = "Redo";
      }
      return true;
    }
    if (event == Event::F1) {
      prompt_ = Prompt::help;
      return true;
    }
    if (event == Event::F2) {
      restore_recovery();
      return true;
    }
    if (event == Event::F3) {
      find_next();
      return true;
    }
    if (event == Event::F4) {
      trust_project();
      return true;
    }
    if (event == Event::F5) {
      run_default_task();
      return true;
    }
    if (event == Event::F6) {
      show_git_status();
      return true;
    }
    if (event == Event::F7) {
      toggle_stage_selected();
      return true;
    }
    if (event == Event::F8) {
      prompt_text_.clear();
      prompt_ = Prompt::commit;
      return true;
    }
    if (event == Event::CtrlO) {
      prompt_text_.clear();
      prompt_ = Prompt::open_file;
      return true;
    }
    if (event == Event::CtrlT) {
      prompt_text_.clear();
      prompt_ = Prompt::shell_command;
      return true;
    }
    if (event == Event::CtrlL || event == Event::F9) {
      toggle_tool_panel();
      if (output_visible_) {
        focus_ = Focus::tools;
        interaction_mode_ = InteractionMode::navigate;
      }
      return true;
    }
    if (event == Event::CtrlG || event == Event::F10) {
      if (!project_.workspace_root) {
        status_ = "No workspace to search";
        return true;
      }
      prompt_text_.clear();
      prompt_ = Prompt::project_search;
      return true;
    }
    if (focus_ == Focus::tree && tree_) return tree_event(event);
    if (interaction_mode_ == InteractionMode::navigate) {
      if (event == Event::ArrowUp || event == Event::k) scroll_editor(-1);
      if (event == Event::ArrowDown || event == Event::j) scroll_editor(1);
      return true;
    }
    return editor_event(event);
  }

  Document release_document() {
    save_sessions();
    return std::move(active().document());
  }

  void wait_for_background_work() { search_job_.wait(); }

 private:
  Element render_tabs() {
    Elements tabs;
    tab_hits_.clear();
    close_tab_hits_.clear();
    tabs.push_back(text(" HORCRUX ") | bold | color(theme_background()) | bgcolor(theme_teal()));
    for (std::size_t index = 0; index < buffers_.size(); ++index) {
      const auto& buffer = *buffers_[index];
      const std::string marker = buffer.document().is_dirty() ? " ●" : "";
      auto tab_hit = std::make_unique<HitTarget>();
      tab_hit->index = index;
      auto close_hit = std::make_unique<HitTarget>();
      close_hit->index = index;
      auto tab = hbox({
          text(" " + display_name(buffer.document()) + marker + " ") | reflect(tab_hit->box),
          text(" × ") | reflect(close_hit->box),
      });
      tab_hits_.push_back(std::move(tab_hit));
      close_tab_hits_.push_back(std::move(close_hit));
      tab = index == active_buffer_ ? tab | bold | bgcolor(theme_raised()) | color(theme_teal())
                                    : tab | color(theme_muted());
      tabs.push_back(std::move(tab));
    }
    tabs.push_back(filler());
    const std::string root =
        project_.workspace_root ? project_.workspace_root->filename().string() : "no workspace";
    tabs.push_back(text(" " + root + " ") | color(theme_muted()));
    return hbox(std::move(tabs)) | bgcolor(theme_panel()) | size(HEIGHT, EQUAL, 1);
  }

  Element render_tree(const int body_height) {
    Elements lines;
    tree_hits_.clear();
    const std::string title =
        tree_->root().filename().empty() ? tree_->root().string()
                                        : tree_->root().filename().string();
    lines.push_back(text(" EXPLORER  " + title) | bold |
                    color(focus_ == Focus::tree ? theme_teal() : theme_muted()));

    const auto& entries = tree_->entries();
    const std::size_t selected = tree_->selected_index();
    const std::size_t visible = static_cast<std::size_t>(std::max(1, body_height - 1));
    const std::size_t first =
        selected >= visible ? selected - visible + 1U : 0U;
    const std::size_t last = std::min(entries.size(), first + visible);
    for (std::size_t index = first; index < last; ++index) {
      const auto& entry = entries[index];
      std::string label(entry.depth * 2U, ' ');
      label += entry.directory ? (entry.expanded ? "▾ " : "▸ ") : "  ";
      label += entry.relative_path.filename().string();
      auto hit = std::make_unique<HitTarget>();
      hit->index = index;
      auto line = text(std::move(label)) | reflect(hit->box);
      tree_hits_.push_back(std::move(hit));
      if (index == selected) {
        line = line | bgcolor(theme_raised()) |
               color(focus_ == Focus::tree ? theme_teal() : theme_foreground());
      } else if (entry.directory) {
        line = line | color(theme_amber());
      }
      lines.push_back(std::move(line));
    }
    while (lines.size() < static_cast<std::size_t>(body_height)) {
      lines.push_back(text(" "));
    }
    return vbox(std::move(lines)) | bgcolor(theme_panel());
  }

  Element render_source_line(const std::string& value, const std::size_t source_offset,
                             const bool cursor_line,
                             const std::size_t cursor_column,
                             const std::size_t left_column,
                             const std::size_t available_width) const {
    const auto visible_start = std::min(left_column, value.size());
    const auto visible_end = std::min(value.size(), visible_start + available_width);
    const auto spans = highlighter_.highlight_line(value, source_offset);
    const auto kind_at = [&spans](const std::size_t position) {
      const auto found = std::find_if(spans.begin(), spans.end(), [position](const SyntaxSpan& span) {
        return position >= span.start && position < span.start + span.length;
      });
      return found == spans.end() ? SyntaxKind::plain : found->kind;
    };

    Elements elements;
    std::size_t index = visible_start;
    while (index < visible_end) {
      if (cursor_line && index == cursor_column) {
        elements.push_back(text(value.substr(index, 1U)) | bgcolor(theme_teal()) |
                           color(theme_background()));
        ++index;
        continue;
      }
      const auto kind = kind_at(index);
      auto end = index + 1U;
      while (end < visible_end && (!cursor_line || end != cursor_column) && kind_at(end) == kind) {
        ++end;
      }
      elements.push_back(text(value.substr(index, end - index)) | color(syntax_color(kind)));
      index = end;
    }
    if (cursor_line && cursor_column == value.size() && cursor_column >= visible_start &&
        cursor_column <= visible_end) {
      elements.push_back(text(" ") | bgcolor(theme_teal()) | color(theme_background()));
    }
    if (elements.empty()) elements.push_back(text(" "));
    return hbox(std::move(elements));
  }

  Element render_editor(const int editor_width, const int body_height) {
    auto& buffer = active();
    const auto& contents = buffer.document().buffer().text();
    const auto cursor_position = position_at(contents, buffer.cursor());
    const std::size_t visible_lines = static_cast<std::size_t>(std::max(1, body_height));
    editor_visible_lines_ = visible_lines;
    const auto line_count = buffer.document().buffer().line_count();
    const auto last_top_line = line_count > visible_lines ? line_count - visible_lines : 0U;
    if (buffer.top_line() > last_top_line) buffer.set_top_line(last_top_line);
    if (follow_cursor_ && cursor_position.line < buffer.top_line()) {
      buffer.set_top_line(cursor_position.line);
    }
    if (follow_cursor_ && cursor_position.line >= buffer.top_line() + visible_lines) {
      buffer.set_top_line(cursor_position.line - visible_lines + 1U);
    }
    const std::size_t number_width =
        std::max<std::size_t>(3U, std::to_string(buffer.document().buffer().line_count()).size());
    const std::size_t source_width =
        static_cast<std::size_t>(std::max(1, editor_width - static_cast<int>(number_width) - 3));
    if (cursor_position.column < buffer.left_column()) buffer.set_left_column(cursor_position.column);
    if (cursor_position.column >= buffer.left_column() + source_width) {
      buffer.set_left_column(cursor_position.column - source_width + 1U);
    }

    Elements lines;
    for (std::size_t row = 0; row < visible_lines; ++row) {
      const std::size_t line_index = buffer.top_line() + row;
      if (line_index >= buffer.document().buffer().line_count()) {
        lines.push_back(hbox({text("~") | color(theme_muted()), filler()}));
        continue;
      }
      const auto start = line_start_at(contents, line_index);
      auto end = contents.find('\n', start);
      if (end == std::string::npos) end = contents.size();
      if (end > start && contents[end - 1U] == '\r') --end;
      std::string source = contents.substr(start, end - start);
      std::string number = std::to_string(line_index + 1U);
      number.insert(0U, number_width - number.size(), ' ');
      auto line_number = text(number + " │ ") |
                         color(line_index == cursor_position.line ? theme_teal() : theme_muted());
      lines.push_back(hbox({
          std::move(line_number),
          render_source_line(source, start, line_index == cursor_position.line,
                             cursor_position.column, buffer.left_column(), source_width) | flex,
      }));
    }
    return vbox(std::move(lines)) | reflect(editor_box_) | bgcolor(theme_background());
  }

  Element render_status() const {
    const auto& buffer = active();
    const auto position = position_at(buffer.document().buffer().text(), buffer.cursor());
    const std::string trust = project_.has_project()
                                  ? (project_.trusted ? "trusted" : "untrusted")
                                  : (project_.workspace_root ? "no project config" : "no project");
    const std::string encoding = buffer.document().encoding() == TextEncoding::utf8
                                     ? "UTF-8"
                                     : "Unicode";
    Elements elements{
               text(" " + (status_.empty() ? "ready" : status_)) | flex,
    };
    elements.push_back(text(interaction_mode_ == InteractionMode::edit ? " EDIT " : " NAV ") |
                       bold | color(interaction_mode_ == InteractionMode::edit ? theme_teal()
                                                                                  : theme_amber()));
    if (repository_info_.available) {
      std::string repository = "  " + repository_info_.branch;
      if (repository_info_.staged > 0U) repository += " +" + std::to_string(repository_info_.staged);
      if (repository_info_.modified > 0U) repository += " ~" + std::to_string(repository_info_.modified);
      if (repository_info_.untracked > 0U) repository += " ?" + std::to_string(repository_info_.untracked);
      elements.push_back(text(std::move(repository)) | color(theme_teal()));
    }
    elements.insert(elements.end(), {
               text(" " + trust + " "),
               text(" " + encoding + " "),
               text(" Ln " + std::to_string(position.line + 1U) + ", Col " +
                    std::to_string(position.column + 1U) + " "),
    });
    return hbox(std::move(elements)) |
           bgcolor(theme_raised()) | color(theme_foreground()) | size(HEIGHT, EQUAL, 1);
  }

  Element render_tool_tabs() {
    struct ToolTab {
      ToolWindow window;
      const char* label;
    };
    constexpr std::array<ToolTab, 5> tabs{{
        {ToolWindow::find, " Find "},
        {ToolWindow::search, " Search "},
        {ToolWindow::git, " Git "},
        {ToolWindow::tasks, " Tasks "},
        {ToolWindow::shell, " Shell "},
    }};
    tool_tab_hits_.clear();
    Elements elements;
    for (std::size_t index = 0; index < tabs.size(); ++index) {
      auto hit = std::make_unique<HitTarget>();
      hit->index = index;
      auto tab = text(tabs[index].label) | reflect(hit->box);
      tool_tab_hits_.push_back(std::move(hit));
      if (active_tool_window_ == tabs[index].window) {
        tab = tab | bold | color(theme_teal()) | bgcolor(theme_raised());
      } else {
        tab = tab | color(theme_muted());
      }
      elements.push_back(std::move(tab));
    }
    elements.push_back(filler());
    elements.push_back(text(" F9/Esc close ") | color(theme_muted()));
    return hbox(std::move(elements)) | bgcolor(theme_panel()) | size(HEIGHT, EQUAL, 1);
  }

  Element render_text_output(const std::string& title, const std::string& output,
                             const int width, const int height) const {
    Elements lines;
    lines.push_back(hbox({
        text(" " + (title.empty() ? std::string("OUTPUT") : title)) | bold |
            color(theme_amber()),
        filler(),
    }));
    std::size_t start = 0U;
    for (int row = 1; row < height; ++row) {
      if (start > output.size()) {
        lines.push_back(text(" "));
        continue;
      }
      auto end = output.find('\n', start);
      if (end == std::string::npos) end = output.size();
      auto line = output.substr(start, end - start);
      if (line.size() > static_cast<std::size_t>(std::max(1, width - 2))) {
        line.resize(static_cast<std::size_t>(std::max(1, width - 2)));
      }
      lines.push_back(text(" " + line));
      if (end == output.size()) {
        start = output.size() + 1U;
      } else {
        start = end + 1U;
      }
    }
    return vbox(std::move(lines)) | bgcolor(theme_panel());
  }

  Element render_output(const int width, const int height) {
    switch (active_tool_window_) {
      case ToolWindow::find:
        return render_find_results(width, height);
      case ToolWindow::search:
        if (search_started_at_) return render_search_progress(width, height);
        if (!search_results_.empty()) return render_search_results(width, height);
        return render_text_output(output_title_, output_, width, height);
      case ToolWindow::git:
        return render_git_status(width, height);
      case ToolWindow::tasks:
        return render_text_output(task_output_title_, task_output_, width, height);
      case ToolWindow::shell:
        return render_text_output("SHELL · Ctrl+T command",
                                  shell_output_.empty()
                                      ? "No commands yet. Ctrl+T runs a command in this project."
                                      : shell_output_,
                                  width, height);
    }
    return text(" ");
  }

  Element render_git_status(const int width, const int height) {
    Elements lines;
    lines.push_back(hbox({
        text(" GIT") | bold | color(theme_amber()), filler(),
        text(" j/k select · Enter stage · r refresh · c commit ") | color(theme_muted()),
    }));
    if (!git_status_.error.empty()) {
      lines.push_back(text(" " + git_status_.error) | color(Color::RGB(224, 108, 117)));
    } else if (git_status_.entries.empty()) {
      lines.push_back(text(" Working tree clean.") | color(theme_muted()));
    } else {
      const auto visible = static_cast<std::size_t>(std::max(1, height - 1));
      const auto first = git_selected_ >= visible ? git_selected_ - visible + 1U : 0U;
      const auto last = std::min(git_status_.entries.size(), first + visible);
      for (std::size_t index = first; index < last; ++index) {
        const auto& entry = git_status_.entries[index];
        const bool selected = git_focus_ && index == git_selected_;
        auto row = hbox({
            text(selected ? "› " : "  ") | color(theme_teal()),
            text(std::string{entry.index_status, entry.worktree_status} + " ") | color(theme_amber()),
            text(entry.path.string()) | color(theme_foreground()) | flex,
        });
        if (selected) row = row | bgcolor(theme_raised());
        lines.push_back(std::move(row));
      }
    }
    while (lines.size() < static_cast<std::size_t>(height)) lines.push_back(text(" "));
    return vbox(std::move(lines)) | bgcolor(theme_panel()) | size(WIDTH, EQUAL, std::max(1, width));
  }

  Element render_find_results(const int width, const int height) {
    Elements lines;
    lines.push_back(hbox({
        text(" FIND · " + last_find_) | bold | color(theme_amber()),
        filler(),
        text(" j/k or ↑/↓ select · Enter open ") | color(theme_muted()),
    }));
    if (find_results_.empty()) {
      lines.push_back(text(" No matches in this document.") | color(theme_muted()));
    } else {
      const auto visible = static_cast<std::size_t>(std::max(1, height - 1));
      const auto first = find_selected_ >= visible ? find_selected_ - visible + 1U : 0U;
      const auto last = std::min(find_results_.size(), first + visible);
      find_result_hits_.clear();
      for (std::size_t index = first; index < last; ++index) {
        const auto& match = find_results_[index];
        const bool selected = find_focus_ && index == find_selected_;
        auto hit = std::make_unique<HitTarget>();
        hit->index = index;
        auto row = hbox({
            text(selected ? "› " : "  ") | color(theme_teal()),
            text("Ln " + std::to_string(match.line + 1U) + ", Col " +
                 std::to_string(match.column + 1U) + " ") | color(theme_amber()),
            text(match.preview) | color(theme_foreground()) | flex,
        }) | reflect(hit->box);
        find_result_hits_.push_back(std::move(hit));
        if (selected) row = row | bgcolor(theme_raised());
        lines.push_back(std::move(row));
      }
    }
    while (lines.size() < static_cast<std::size_t>(height)) lines.push_back(text(" "));
    return vbox(std::move(lines)) | bgcolor(theme_panel()) |
           size(WIDTH, EQUAL, std::max(1, width));
  }

  Element center_window(Element window) const {
    return vbox({
        filler(),
        hbox({filler(), std::move(window), filler()}),
        filler(),
    });
  }

  Element render_search_progress(const int width, const int height) const {
    if (auto* app = App::Active()) app->RequestAnimationFrame();
    constexpr std::array<std::string_view, 10> spinner{
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const auto elapsed = std::chrono::steady_clock::now() - *search_started_at_;
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    const auto frame = static_cast<std::size_t>((milliseconds / 90) %
                                                static_cast<long long>(spinner.size()));
    const auto seconds = milliseconds / 1000;
    Elements lines;
    lines.push_back(hbox({
        text(" SEARCH · working ") | bold | color(theme_amber()),
        filler(),
        text("ripgrep is on the case · " + std::to_string(seconds) + "s ") | color(theme_muted()),
    }));
    lines.push_back(hbox({
        text(" " + std::string(spinner[frame]) + " ") | bold | color(theme_teal()),
        text("Searching the project — hang tight, good things are being found.") |
            color(theme_foreground()),
    }));
    while (lines.size() < static_cast<std::size_t>(height)) lines.push_back(text(" "));
    return vbox(std::move(lines)) | bgcolor(theme_panel()) |
           size(WIDTH, EQUAL, std::max(1, width));
  }

  Element render_search_results(const int width, const int height) {
    Elements lines;
    lines.push_back(hbox({
        text(" " + output_title_) | bold | color(theme_amber()),
        filler(),
        text(" ↑/↓ select · Enter open · Esc close ") | color(theme_muted()),
    }));
    const auto visible = static_cast<std::size_t>(std::max(1, height - 1));
    const auto first = search_selected_ >= visible ? search_selected_ - visible + 1U : 0U;
    const auto last = std::min(search_results_.size(), first + visible);
    search_result_hits_.clear();
    for (std::size_t index = first; index < last; ++index) {
      const auto& match = search_results_[index];
      const bool selected = output_focus_ && index == search_selected_;
      auto hit = std::make_unique<HitTarget>();
      hit->index = index;
      auto row = hbox({
          text(selected ? "› " : "  ") | color(theme_teal()),
          text(match.path.string()) | color(selected ? theme_teal() : Color::RGB(133, 190, 255)),
          text(":" + std::to_string(match.line) + ":" + std::to_string(match.column) + " ") |
              color(theme_amber()),
          text(match.preview) | color(theme_foreground()) | flex,
      }) | reflect(hit->box);
      search_result_hits_.push_back(std::move(hit));
      if (selected) row = row | bgcolor(theme_raised());
      lines.push_back(std::move(row));
    }
    while (lines.size() < static_cast<std::size_t>(height)) lines.push_back(text(" "));
    return vbox(std::move(lines)) | bgcolor(theme_panel()) |
           size(WIDTH, EQUAL, std::max(1, width));
  }

  Element render_keys() {
    // Keep the footer clock fresh even while the editor is otherwise idle.
    if (auto* app = App::Active()) app->RequestAnimationFrame();
    auto tools = hbox({text("^L") | color(theme_teal()), text(" tools  ")}) |
                 reflect(tool_toggle_box_);
    auto theme = text(" ◐ " + std::string(theme_name()) + " ") |
                 color(theme_muted()) | reflect(theme_toggle_box_);
    return hbox({
               text(" ^S") | color(theme_teal()), text(" save  "),
               text("^Q") | color(theme_teal()), text(" quit  "),
               text("^B") | color(theme_teal()), text(" explorer  "),
               text("^F") | color(theme_teal()), text(" find  "),
               text("^G") | color(theme_teal()), text(" search  "),
               text("^O") | color(theme_teal()), text(" open  "),
               text("^T") | color(theme_teal()), text(" shell  "),
               std::move(tools),
               text("F1") | color(theme_teal()), text(" help  "),
               filler(),
               std::move(theme),
               text(" " + dual_clock_text() + " ") | color(theme_muted()),
           }) |
           bgcolor(theme_panel()) | size(HEIGHT, EQUAL, 1);
  }

  Element render_theme_picker() {
    constexpr std::array<std::string_view, 3> names{"Midnight", "High Contrast", "Paper"};
    theme_option_hits_.clear();
    Elements options;
    options.push_back(text(" Theme ") | bold | color(theme_teal()));
    options.push_back(separator());
    for (std::size_t index = 0; index < names.size(); ++index) {
      auto hit = std::make_unique<HitTarget>();
      hit->index = index;
      auto option = text(std::string(index == theme_selected_ ? "› " : "  ") +
                         std::string(names[index])) | reflect(hit->box);
      theme_option_hits_.push_back(std::move(hit));
      if (index == theme_selected_) option = option | bgcolor(theme_raised()) | color(theme_teal());
      options.push_back(std::move(option));
    }
    options.push_back(separator());
    options.push_back(text(" Enter apply · Esc cancel ") | color(theme_muted()));
    auto menu = vbox(std::move(options)) | border | bgcolor(theme_panel()) |
                size(WIDTH, EQUAL, 28);
    return vbox({
        filler(),
        hbox({filler(), std::move(menu), filler()}),
        filler(),
    });
  }

  bool theme_picker_event(Event event) {
    if (event == Event::Escape) {
      theme_picker_visible_ = false;
      return true;
    }
    if (event == Event::ArrowUp || event == Event::k) {
      if (theme_selected_ > 0U) --theme_selected_;
      return true;
    }
    if (event == Event::ArrowDown || event == Event::j) {
      if (theme_selected_ < 2U) ++theme_selected_;
      return true;
    }
    if (event == Event::Return) {
      active_theme = static_cast<ThemeKind>(theme_selected_);
      theme_picker_visible_ = false;
      status_ = "Theme: " + std::string(theme_name());
      return true;
    }
    if (event.is_mouse()) {
      const auto& mouse = event.mouse();
      const int x = mouse.x - 1;
      const int y = mouse.y - 1;
      const auto contains_mouse = [&mouse, x, y](const Box& box) {
        return box.Contain(x, y) || box.Contain(mouse.x, mouse.y);
      };
      if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        const auto selected = std::find_if(theme_option_hits_.begin(), theme_option_hits_.end(),
                                           [&contains_mouse](const auto& hit) {
                                             return contains_mouse(hit->box);
                                           });
        if (selected != theme_option_hits_.end()) {
          theme_selected_ = (*selected)->index;
          active_theme = static_cast<ThemeKind>(theme_selected_);
          theme_picker_visible_ = false;
          status_ = "Theme: " + std::string(theme_name());
        }
      }
    }
    return true;
  }

  Element render_modal(const int width, const int height) const {
    if (prompt_ == Prompt::help) {
      auto window = vbox({
                 text(" Horcrux keyboard ") | bold | color(theme_teal()),
                 separator(),
                 text("Ctrl-S save       Ctrl-Q quit       Ctrl-B explorer"),
                 text("Ctrl-F find       Ctrl-N/P buffers Ctrl-Z/Y undo/redo"),
                 text("F2 recover        F3 find next     F4 trust project"),
                 text("F5 run task       F6 Git status    F7 stage/unstage"),
                 text("F8 Git commit     Ctrl-O open      Ctrl-L tools"),
               }) |
             border | bgcolor(theme_raised()) |
             size(WIDTH, LESS_THAN, std::max(20, width - 4)) |
             size(HEIGHT, LESS_THAN, std::max(8, height - 4));
      return center_window(std::move(window));
    }

    std::string title;
    std::string hint = "Enter confirm · Esc cancel";
    switch (prompt_) {
      case Prompt::find: title = " Find "; break;
      case Prompt::project_search: title = " Project search "; break;
      case Prompt::open_file: title = " Open file "; break;
      case Prompt::shell_command: title = " Shell command "; break;
      case Prompt::commit: title = " Git commit message "; break;
      case Prompt::save_as: title = " Save as "; break;
      case Prompt::quit_dirty:
        title = " Unsaved buffers ";
        hint = "S save all and quit · D discard and quit · Esc cancel";
        break;
      case Prompt::close_dirty:
        title = " Unsaved tab ";
        hint = "S save and close · D discard and close · Esc cancel";
        break;
      default: break;
    }
    Elements content{
        text(title) | bold | color(theme_teal()),
        separator(),
    };
    if (prompt_ != Prompt::quit_dirty && prompt_ != Prompt::close_dirty) {
      content.push_back(hbox({
          text("> ") | color(theme_teal()),
          text(prompt_text_),
          text(" ") | bgcolor(theme_teal()),
      }));
    }
    content.push_back(text(hint) | color(theme_muted()));
    auto window = vbox(std::move(content)) | border | bgcolor(theme_raised()) |
                  size(WIDTH, EQUAL, std::min(70, std::max(30, width - 8)));
    return center_window(std::move(window));
  }

  bool prompt_event(const Event& event, App& app) {
    if (event == Event::Escape) {
      prompt_ = Prompt::none;
      prompt_text_.clear();
      status_ = "Cancelled";
      return true;
    }
    if (prompt_ == Prompt::help) {
      prompt_ = Prompt::none;
      return true;
    }
    if (prompt_ == Prompt::quit_dirty) {
      if (event == Event::s || event == Event::S) {
        if (save_all()) quit(app);
        return true;
      }
      if (event == Event::d || event == Event::D) {
        quit(app);
        return true;
      }
      return true;
    }
    if (prompt_ == Prompt::close_dirty) {
      if (event == Event::s || event == Event::S) {
        const auto index = pending_close_buffer_;
        prompt_ = Prompt::none;
        if (index && save_buffer(*index)) close_buffer(*index);
        return true;
      }
      if (event == Event::d || event == Event::D) {
        const auto index = pending_close_buffer_;
        prompt_ = Prompt::none;
        if (index) close_buffer(*index);
        return true;
      }
      return true;
    }
    if (event == Event::Backspace) {
      if (!prompt_text_.empty()) prompt_text_.pop_back();
      return true;
    }
    if (event == Event::Return) {
      const auto value = prompt_text_;
      const auto completed_prompt = prompt_;
      prompt_ = Prompt::none;
      prompt_text_.clear();
      if (completed_prompt == Prompt::find) {
        start_find(value);
      } else if (completed_prompt == Prompt::project_search) {
        start_project_search(value, app);
      } else if (completed_prompt == Prompt::open_file) {
        open_requested_file(value);
      } else if (completed_prompt == Prompt::shell_command) {
        run_shell_command(value);
      } else if (completed_prompt == Prompt::commit) {
        commit(value);
      } else if (completed_prompt == Prompt::save_as) {
        save_as(value);
      }
      return true;
    }
    if (event.is_character()) {
      const auto character = event.character();
      if (!character.empty() &&
          std::all_of(character.begin(), character.end(), [](const unsigned char value) {
            return value >= 32U || value >= 128U;
          })) {
        prompt_text_ += character;
      }
      return true;
    }
    return true;
  }

  bool tree_event(const Event& event) {
    if (event == Event::ArrowUp || event == Event::k) {
      tree_->select_previous();
      return true;
    }
    if (event == Event::ArrowDown || event == Event::j) {
      tree_->select_next();
      return true;
    }
    if (event == Event::ArrowLeft) {
      const auto* selected = tree_->selected_entry();
      if (selected && selected->directory && selected->expanded) {
        std::string error;
        tree_->toggle_selected(error);
        if (!error.empty()) status_ = error;
      }
      return true;
    }
    if (event == Event::ArrowRight || event == Event::Return) {
      const auto* selected = tree_->selected_entry();
      if (!selected) return true;
      if (selected->directory) {
        std::string error;
        tree_->toggle_selected(error);
        status_ = error.empty() ? "Explorer updated" : error;
      } else {
        open_from_explorer(tree_->root() / selected->relative_path);
      }
      return true;
    }
    return false;
  }

  bool mouse_event(Event event) {
    const auto& mouse = event.mouse();
    const int x = mouse.x - 1;
    const int y = mouse.y - 1;
    const auto contains_mouse = [&mouse, x, y](const Box& box) {
      // FTXUI boxes use zero-based render coordinates, while terminal mouse
      // protocols vary between zero- and one-based coordinates.
      return box.Contain(x, y) || box.Contain(mouse.x, mouse.y);
    };

    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed &&
        contains_mouse(tool_toggle_box_)) {
      toggle_tool_panel();
      return true;
    }
    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed &&
        contains_mouse(theme_toggle_box_)) {
      theme_selected_ = static_cast<std::size_t>(active_theme);
      theme_picker_visible_ = true;
      return true;
    }

    const auto search_result_hit = std::find_if(
        search_result_hits_.begin(), search_result_hits_.end(),
        [&contains_mouse](const auto& hit) { return contains_mouse(hit->box); });
    const auto find_result_hit = std::find_if(
        find_result_hits_.begin(), find_result_hits_.end(),
        [&contains_mouse](const auto& hit) { return contains_mouse(hit->box); });
    if (output_visible_ && active_tool_window_ == ToolWindow::find &&
        find_result_hit != find_result_hits_.end()) {
      if (mouse.button == Mouse::WheelUp) {
        if (find_selected_ > 0U) --find_selected_;
        find_focus_ = true;
        return true;
      }
      if (mouse.button == Mouse::WheelDown) {
        if (find_selected_ + 1U < find_results_.size()) ++find_selected_;
        find_focus_ = true;
        return true;
      }
      if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        find_selected_ = (*find_result_hit)->index;
        find_focus_ = true;
        open_selected_find_result();
        return true;
      }
    }
    if (output_visible_ && active_tool_window_ == ToolWindow::search &&
        search_result_hit != search_result_hits_.end()) {
      if (mouse.button == Mouse::WheelUp) {
        if (search_selected_ > 0U) --search_selected_;
        output_focus_ = true;
        return true;
      }
      if (mouse.button == Mouse::WheelDown) {
        if (search_selected_ + 1U < search_results_.size()) ++search_selected_;
        output_focus_ = true;
        return true;
      }
      if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        search_selected_ = (*search_result_hit)->index;
        output_focus_ = true;
        open_selected_search_result();
        return true;
      }
    }

    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
      for (const auto& hit : close_tab_hits_) {
        if (contains_mouse(hit->box)) {
          request_close_buffer(hit->index);
          return true;
        }
      }
      for (const auto& hit : tab_hits_) {
        if (contains_mouse(hit->box)) {
          active_buffer_ = hit->index;
          focus_ = Focus::editor;
          status_ = display_name(active().document());
          return true;
        }
      }
      for (const auto& hit : tool_tab_hits_) {
        if (contains_mouse(hit->box)) {
          active_tool_window_ = static_cast<ToolWindow>(hit->index);
          output_visible_ = true;
          focus_ = Focus::tools;
          interaction_mode_ = InteractionMode::navigate;
          output_focus_ = active_tool_window_ == ToolWindow::search &&
                          !search_results_.empty();
          find_focus_ = active_tool_window_ == ToolWindow::find && !find_results_.empty();
          git_focus_ = active_tool_window_ == ToolWindow::git && !git_status_.entries.empty();
          status_ = "Opened tool window";
          return true;
        }
      }
    }

    if ((mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) &&
        contains_mouse(editor_box_)) {
      scroll_editor(mouse.button == Mouse::WheelUp ? -3 : 3);
      return true;
    }

    if (!tree_visible_ || !tree_ || mouse.x > sidebar_width) return false;

    if (mouse.button == Mouse::WheelUp) {
      tree_->select_previous();
      focus_ = Focus::tree;
      return true;
    }
    if (mouse.button == Mouse::WheelDown) {
      tree_->select_next();
      focus_ = Focus::tree;
      return true;
    }
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) return true;
    const auto tree_hit = std::find_if(tree_hits_.begin(), tree_hits_.end(),
                                       [&contains_mouse](const auto& hit) {
                                         return contains_mouse(hit->box);
                                       });
    if (tree_hit == tree_hits_.end()) return true;

    tree_->select((*tree_hit)->index);
    focus_ = Focus::tree;
    const auto* entry = tree_->selected_entry();
    if (entry->directory) {
      std::string error;
      tree_->toggle_selected(error);
      status_ = error.empty() ? "Explorer updated" : error;
    } else {
      open_from_explorer(tree_->root() / entry->relative_path);
    }
    return true;
  }

  bool editor_event(const Event& event) {
    auto& buffer = active();
    auto& text_buffer = buffer.document().buffer();
    const auto& contents = text_buffer.text();
    const auto position = position_at(contents, buffer.cursor());

    if (event == Event::ArrowLeft) {
      if (buffer.cursor() > 0U) buffer.set_cursor(buffer.cursor() - 1U);
    } else if (event == Event::ArrowRight) {
      if (buffer.cursor() < contents.size()) buffer.set_cursor(buffer.cursor() + 1U);
    } else if (event == Event::ArrowUp) {
      if (position.line > 0U) {
        buffer.set_cursor(offset_at(contents, position.line - 1U, buffer.desired_column()));
      }
    } else if (event == Event::ArrowDown) {
      if (position.line + 1U < text_buffer.line_count()) {
        buffer.set_cursor(offset_at(contents, position.line + 1U, buffer.desired_column()));
      }
    } else if (event == Event::Home) {
      buffer.set_cursor(position.line_start);
    } else if (event == Event::End) {
      buffer.set_cursor(position.line_end);
    } else if (event == Event::PageUp) {
      const std::size_t amount = 20U;
      const std::size_t line = position.line > amount ? position.line - amount : 0U;
      buffer.set_cursor(offset_at(contents, line, buffer.desired_column()));
    } else if (event == Event::PageDown) {
      const std::size_t line =
          std::min(text_buffer.line_count() - 1U, position.line + 20U);
      buffer.set_cursor(offset_at(contents, line, buffer.desired_column()));
    } else if (event == Event::Backspace) {
      if (buffer.cursor() > 0U) {
        text_buffer.erase(buffer.cursor() - 1U, 1U);
        buffer.set_cursor(buffer.cursor() - 1U);
        changed();
      }
    } else if (event == Event::Delete) {
      if (buffer.cursor() < contents.size()) {
        text_buffer.erase(buffer.cursor(), 1U);
        changed();
      }
    } else if (event == Event::Return) {
      text_buffer.insert(buffer.cursor(), "\n");
      buffer.set_cursor(buffer.cursor() + 1U);
      changed();
    } else if (event == Event::Tab) {
      text_buffer.insert(buffer.cursor(), "    ");
      buffer.set_cursor(buffer.cursor() + 4U);
      changed();
    } else if (event.is_character()) {
      const auto character = event.character();
      if (!character.empty() && character != "\t" &&
          static_cast<unsigned char>(character.front()) >= 32U) {
        text_buffer.insert(buffer.cursor(), character);
        buffer.set_cursor(buffer.cursor() + character.size());
        changed();
      } else {
        return false;
      }
    } else {
      return false;
    }

    follow_cursor_ = true;
    const auto updated = position_at(text_buffer.text(), buffer.cursor());
    if (event != Event::ArrowUp && event != Event::ArrowDown &&
        event != Event::PageUp && event != Event::PageDown) {
      buffer.set_desired_column(updated.column);
    }
    return true;
  }

  void scroll_editor(const int lines) {
    auto& buffer = active();
    const auto line_count = buffer.document().buffer().line_count();
    const auto last_top_line =
        line_count > editor_visible_lines_ ? line_count - editor_visible_lines_ : 0U;
    if (lines < 0) {
      const auto amount = static_cast<std::size_t>(-lines);
      buffer.set_top_line(buffer.top_line() > amount ? buffer.top_line() - amount : 0U);
    } else {
      const auto amount = static_cast<std::size_t>(lines);
      buffer.set_top_line(std::min(last_top_line, buffer.top_line() + amount));
    }
    follow_cursor_ = false;
    status_ = "Scrolled";
  }

  void toggle_tool_panel() {
    output_visible_ = !output_visible_;
    output_focus_ = output_visible_ && active_tool_window_ == ToolWindow::search &&
                    !search_results_.empty();
    find_focus_ = output_visible_ && active_tool_window_ == ToolWindow::find &&
                  !find_results_.empty();
    git_focus_ = output_visible_ && active_tool_window_ == ToolWindow::git &&
                 !git_status_.entries.empty();
    status_ = output_visible_ ? "Tools shown" : "Tools hidden";
  }

  void cycle_focus() {
    if (focus_ == Focus::tree) {
      focus_ = Focus::editor;
    } else if (focus_ == Focus::editor && output_visible_) {
      focus_ = Focus::tools;
    } else if (focus_ == Focus::editor) {
      focus_ = tree_ ? Focus::tree : Focus::editor;
    } else {
      focus_ = tree_ ? Focus::tree : Focus::editor;
    }
    status_ = focus_ == Focus::tree ? "NAV · Explorer" :
              focus_ == Focus::tools ? "NAV · Tools" : "NAV · Editor";
  }

  void cycle_theme() {
    switch (active_theme) {
      case ThemeKind::midnight: active_theme = ThemeKind::high_contrast; break;
      case ThemeKind::high_contrast: active_theme = ThemeKind::paper; break;
      case ThemeKind::paper: active_theme = ThemeKind::midnight; break;
    }
    status_ = "Theme: " + std::string(theme_name());
  }

  void changed() {
    journal_active();
    status_ = "Modified";
  }

  void journal_active() {
    std::string error;
    if (!active().checkpoint(error)) {
      status_ = error;
    }
  }

  void clamp_cursor() {
    active().set_cursor(active().cursor());
  }

  void open_requested_file(const std::string& value) {
    if (value.empty()) {
      status_ = "Open cancelled: path is empty";
      return;
    }
    std::filesystem::path path(value);
    if (path.is_relative() && project_.workspace_root) path = *project_.workspace_root / path;
    std::error_code filesystem_error;
    if (std::filesystem::is_directory(path, filesystem_error)) {
      status_ = "Opening another project folder is not implemented yet";
      return;
    }
    open_file(path);
  }

  void run_shell_command(const std::string& command) {
    if (command.empty()) {
      status_ = "Shell command is empty";
      return;
    }
    const auto root = project_.workspace_root ? project_.workspace_root : project_.root;
    if (!root) {
      status_ = "No project folder for shell command";
      return;
    }
    const TaskConfig task{.name = "shell", .command = {command}, .cwd = std::nullopt, .shell = true};
    const auto result = run_task(task, *root);
    if (!shell_output_.empty()) shell_output_ += "\n";
    shell_output_ += "$ " + command + "\n";
    shell_output_ += result.standard_output;
    if (!result.standard_error.empty()) shell_output_ += result.standard_error;
    if (!result.error.empty()) shell_output_ += result.error + "\n";
    shell_output_ += "[exit " + std::to_string(result.exit_code) + "]\n";
    active_tool_window_ = ToolWindow::shell;
    output_visible_ = true;
    output_focus_ = false;
    find_focus_ = false;
    status_ = result.launched ? "Shell command finished" : "Shell command failed";
  }

  void open_file(const std::filesystem::path& path) {
    std::error_code filesystem_error;
    const auto normalized = std::filesystem::weakly_canonical(path, filesystem_error);
    const auto candidate = filesystem_error ? path : normalized;
    for (std::size_t index = 0; index < buffers_.size(); ++index) {
      if (buffers_[index]->document().has_path() &&
          buffers_[index]->document().path() == candidate) {
        active_buffer_ = index;
        status_ = "Switched to " + display_name(active().document());
        return;
      }
    }
    std::string error;
    auto opened = Document::open(candidate, error);
    if (!opened) {
      status_ = error;
      return;
    }
    buffers_.push_back(std::make_unique<EditorBuffer>(
        std::move(*opened), state_directory_, true));
    active_buffer_ = buffers_.size() - 1U;
    status_ = "Opened " + candidate.string();
  }

  void open_from_explorer(const std::filesystem::path& path) {
    open_file(path);
    focus_ = Focus::editor;
    interaction_mode_ = InteractionMode::navigate;
    if (status_.starts_with("Opened ") || status_.starts_with("Switched to ")) {
      status_ += " · NAV mode (i to edit)";
    }
  }

  void switch_buffer(const int direction) {
    if (buffers_.size() < 2U) return;
    if (direction > 0) {
      active_buffer_ = (active_buffer_ + 1U) % buffers_.size();
    } else {
      active_buffer_ =
          active_buffer_ == 0U ? buffers_.size() - 1U : active_buffer_ - 1U;
    }
    status_ = display_name(active().document());
  }

  void request_close_buffer(const std::size_t index) {
    if (index >= buffers_.size()) return;
    if (buffers_[index]->document().is_dirty()) {
      pending_close_buffer_ = index;
      prompt_ = Prompt::close_dirty;
      return;
    }
    close_buffer(index);
  }

  bool save_buffer(const std::size_t index) {
    if (index >= buffers_.size()) return false;
    auto& buffer = *buffers_[index];
    if (!buffer.document().has_path()) {
      status_ = "Untitled tab needs a filename before it can be closed";
      return false;
    }
    std::string error;
    if (!buffer.document().save(error)) {
      status_ = error;
      return false;
    }
    buffer.clear_checkpoint(error);
    if (!error.empty()) {
      status_ = error;
      return false;
    }
    return true;
  }

  void close_buffer(const std::size_t index) {
    if (index >= buffers_.size()) return;
    if (buffers_.size() == 1U) {
      buffers_.front() = std::make_unique<EditorBuffer>(
          Document::untitled(), state_directory_, false);
      active_buffer_ = 0U;
      status_ = "Closed tab";
      return;
    }
    buffers_.erase(buffers_.begin() + static_cast<std::ptrdiff_t>(index));
    if (active_buffer_ > index) --active_buffer_;
    if (active_buffer_ >= buffers_.size()) active_buffer_ = buffers_.size() - 1U;
    status_ = "Closed tab";
  }

  void save_active() {
    if (!active().document().has_path()) {
      prompt_text_ = project_.workspace_root
                         ? (*project_.workspace_root / "untitled.txt").string()
                         : "untitled.txt";
      prompt_ = Prompt::save_as;
      return;
    }
    std::string error;
    if (!active().document().save(error)) {
      status_ = error;
      return;
    }
    active().clear_checkpoint(error);
    status_ = error.empty() ? "Saved " + active().document().path().string() : error;
  }

  void save_as(const std::string& path) {
    if (path.empty()) {
      status_ = "Save cancelled: path is empty";
      return;
    }
    std::string error;
    if (!active().document().save_as(path, error)) {
      status_ = error;
      return;
    }
    active().clear_checkpoint(error);
    status_ = error.empty() ? "Saved " + path : error;
  }

  bool save_all() {
    for (auto& buffer : buffers_) {
      if (!buffer->document().is_dirty()) continue;
      if (!buffer->document().has_path()) {
        active_buffer_ =
            static_cast<std::size_t>(&buffer - buffers_.data());
        status_ = "Untitled buffer needs a filename";
        prompt_ = Prompt::save_as;
        return false;
      }
      std::string error;
      if (!buffer->document().save(error)) {
        status_ = error;
        return false;
      }
      buffer->clear_checkpoint(error);
      if (!error.empty()) status_ = error;
    }
    return true;
  }

  bool has_dirty_buffers() const {
    return std::any_of(buffers_.begin(), buffers_.end(),
                       [](const auto& buffer) { return buffer->document().is_dirty(); });
  }

  void restore_recovery() {
    if (!active().recovery_snapshot()) {
      status_ = "No recovery snapshot";
      return;
    }
    active().restore_recovery();
    status_ = "Recovery snapshot restored";
  }

  void start_find(const std::string& query) {
    last_find_ = query;
    rebuild_find_results();
    active_tool_window_ = ToolWindow::find;
    output_visible_ = true;
    output_focus_ = false;
    find_focus_ = !find_results_.empty();
    status_ = find_results_.empty() ? "No matches: " + last_find_
                                    : std::to_string(find_results_.size()) + " matches";
  }

  void rebuild_find_results() {
    find_results_.clear();
    find_selected_ = 0U;
    if (last_find_.empty()) return;
    const auto& contents = active().document().buffer().text();
    std::size_t offset = 0U;
    while (offset <= contents.size() && find_results_.size() < 500U) {
      const auto found = contents.find(last_find_, offset);
      if (found == std::string::npos) break;
      const auto position = position_at(contents, found);
      find_results_.push_back({.offset = found,
                               .line = position.line,
                               .column = position.column,
                               .preview = contents.substr(position.line_start,
                                                          position.line_end - position.line_start)});
      offset = found + std::max<std::size_t>(1U, last_find_.size());
    }
    const auto next = std::find_if(find_results_.begin(), find_results_.end(),
                                   [this](const FindMatch& match) {
                                     return match.offset >= active().cursor();
                                   });
    if (next != find_results_.end()) {
      find_selected_ = static_cast<std::size_t>(next - find_results_.begin());
    }
  }

  void open_selected_find_result() {
    if (find_selected_ >= find_results_.size()) return;
    const auto& match = find_results_[find_selected_];
    active().set_cursor(match.offset);
    active().set_desired_column(match.column);
    active().set_top_line(match.line);
    follow_cursor_ = true;
    find_focus_ = false;
    status_ = "Found " + last_find_ + " at " + std::to_string(match.line + 1U) + ":" +
              std::to_string(match.column + 1U);
  }

  void find_next() {
    if (last_find_.empty()) {
      status_ = "Find text is empty";
      return;
    }
    rebuild_find_results();
    if (find_results_.empty()) {
      status_ = "Not found: " + last_find_;
      return;
    }
    const auto next = std::find_if(find_results_.begin(), find_results_.end(),
                                   [this](const FindMatch& match) {
                                     return match.offset > active().cursor();
                                   });
    find_selected_ = next == find_results_.end()
                         ? 0U
                         : static_cast<std::size_t>(next - find_results_.begin());
    active_tool_window_ = ToolWindow::find;
    output_visible_ = true;
    open_selected_find_result();
  }

  void start_project_search(const std::string& query, App& app) {
    if (!project_.workspace_root) {
      status_ = "No workspace to search";
      return;
    }
    if (search_job_.running()) {
      status_ = "Search already in progress";
      return;
    }
    output_title_ = "SEARCH · " + query;
    output_ = "Searching…";
    search_results_.clear();
    search_selected_ = 0U;
    active_tool_window_ = ToolWindow::search;
    output_visible_ = true;
    output_focus_ = false;
    search_started_at_ = std::chrono::steady_clock::now();
    status_ = "Searching…";
    const auto root = *project_.workspace_root;
    if (!search_job_.start(root, query, 500U,
                           [this, &app, query](ProjectSearchResult result) mutable {
                             app.Post([this, query = std::move(query), result = std::move(result)]() mutable {
                               finish_project_search(std::move(query), std::move(result));
                             });
                             app.PostEvent(Event::Custom);
                           })) {
      status_ = "Search already in progress";
      search_started_at_.reset();
    }
  }

  void finish_project_search(std::string query, ProjectSearchResult result) {
    search_started_at_.reset();
    output_title_ = "SEARCH · " + query;
    output_.clear();
    if (!result.error.empty()) {
      output_ = result.error;
      status_ = result.error;
    } else {
      const auto match_count = result.matches.size();
      search_results_ = std::move(result.matches);
      if (search_results_.empty()) output_ = "No matches.";
      output_focus_ = !search_results_.empty();
      status_ = std::to_string(match_count) + " matches" +
                (result.used_git_ignore ? " (Git ignore rules)" : "");
    }
    output_visible_ = true;
  }

  void open_selected_search_result() {
    if (!project_.workspace_root || search_selected_ >= search_results_.size()) return;
    const auto& match = search_results_[search_selected_];
    open_file(*project_.workspace_root / match.path);
    const auto line = match.line > 0U ? match.line - 1U : 0U;
    const auto column = match.column > 0U ? match.column - 1U : 0U;
    active().set_cursor(offset_at(active().document().buffer().text(), line, column));
    active().set_desired_column(column);
    active().set_top_line(line);
    output_visible_ = true;
    output_focus_ = false;
    status_ = "Opened " + match.path.string() + ":" + std::to_string(match.line);
  }

  void trust_project() {
    if (!project_.root) {
      status_ = "No project root";
      return;
    }
    std::string error;
    if (!set_project_trusted(state_directory_, *project_.root, true, error)) {
      status_ = error;
      return;
    }
    project_.trusted = true;
    const auto config_path = *project_.root / "horcrux.json";
    if (std::filesystem::exists(config_path)) {
      project_.config = load_project_config(config_path, project_.config_error);
    }
    status_ = project_.config_error.empty() ? "Project trusted" : project_.config_error;
  }

  void run_default_task() {
    if (!project_.trusted) {
      status_ = "Trust the project with F4 before running tasks";
      return;
    }
    if (!project_.root || !project_.config || project_.config->tasks.empty()) {
      status_ = "No configured task";
      return;
    }
    const auto& task = project_.config->tasks.front();
    const auto result = run_task(task, *project_.root);
    task_output_title_ = "TASK · " + task.name;
    task_output_ = result.standard_output;
    if (!result.standard_error.empty()) {
      if (!task_output_.empty()) task_output_ += '\n';
      task_output_ += result.standard_error;
    }
    if (!result.error.empty()) task_output_ = result.error;
    active_tool_window_ = ToolWindow::tasks;
    output_visible_ = true;
    output_focus_ = false;
    status_ = result.launched ? "Task exited " + std::to_string(result.exit_code)
                              : "Task failed";
  }

  void show_git_status() {
    if (!project_.root) {
      status_ = "No Git project";
      return;
    }
    git_status_ = read_git_status(*project_.root);
    refresh_repository_info();
    git_selected_ = 0U;
    active_tool_window_ = ToolWindow::git;
    output_visible_ = true;
    focus_ = Focus::tools;
    interaction_mode_ = InteractionMode::navigate;
    output_focus_ = false;
    git_focus_ = !git_status_.entries.empty();
    status_ = git_status_.error.empty() ? "Git status refreshed" : git_status_.error;
  }

  void toggle_selected_git_entry() {
    if (!project_.root || git_selected_ >= git_status_.entries.size()) return;
    const auto& entry = git_status_.entries[git_selected_];
    const bool staged = entry.index_status != ' ' && entry.index_status != '?';
    const auto result = staged ? unstage_git_file(*project_.root, entry.path)
                               : stage_git_file(*project_.root, entry.path);
    status_ = result.succeeded ? (staged ? "Unstaged " : "Staged ") + entry.path.string()
                               : result.error;
    show_git_status();
  }

  void refresh_repository_info() {
    repository_info_ = {};
    if (project_.root) repository_info_ = read_git_repository_info(*project_.root);
  }

  void toggle_stage_selected() {
    if (!project_.root || !tree_ || !tree_->selected_entry() ||
        tree_->selected_entry()->directory) {
      status_ = "Select a file in the explorer";
      return;
    }
    const auto path = tree_->selected_entry()->relative_path;
    const auto status = read_git_status(*project_.root);
    const auto found = std::find_if(status.entries.begin(), status.entries.end(),
                                    [&path](const GitStatusEntry& entry) {
                                      return entry.path == path;
                                    });
    const bool staged = found != status.entries.end() && found->index_status != ' ' &&
                        found->index_status != '?';
    const auto result = staged ? unstage_git_file(*project_.root, path)
                               : stage_git_file(*project_.root, path);
    status_ = result.succeeded ? (staged ? "Unstaged " : "Staged ") + path.string()
                               : result.error;
    show_git_status();
  }

  void commit(const std::string& message) {
    if (!project_.root) {
      status_ = "No Git project";
      return;
    }
    if (message.empty()) {
      status_ = "Commit message cannot be empty";
      return;
    }
    const auto result = commit_git(*project_.root, message);
    status_ = result.succeeded ? "Commit created" : result.error;
    show_git_status();
  }

  void save_sessions() {
    for (const auto& buffer : buffers_) {
      std::string error;
      buffer->save_session(error);
    }
  }

  void quit(App& app) {
    save_sessions();
    app.Exit();
  }

  ProjectContext& project_;
  std::filesystem::path state_directory_;
  std::vector<std::unique_ptr<EditorBuffer>> buffers_;
  SyntaxHighlighter highlighter_;
  ProjectSearchJob search_job_;
  std::size_t active_buffer_{0};
  std::optional<FileTree> tree_;
  std::vector<std::unique_ptr<HitTarget>> tree_hits_;
  std::vector<std::unique_ptr<HitTarget>> tab_hits_;
  std::vector<std::unique_ptr<HitTarget>> close_tab_hits_;
  std::vector<std::unique_ptr<HitTarget>> tool_tab_hits_;
  std::vector<std::unique_ptr<HitTarget>> search_result_hits_;
  std::vector<std::unique_ptr<HitTarget>> find_result_hits_;
  std::vector<std::unique_ptr<HitTarget>> theme_option_hits_;
  Box editor_box_;
  Box tool_toggle_box_;
  Box theme_toggle_box_;
  Focus focus_{Focus::editor};
  InteractionMode interaction_mode_{InteractionMode::edit};
  Prompt prompt_{Prompt::none};
  std::optional<std::size_t> pending_close_buffer_;
  bool tree_visible_{true};
  bool output_visible_{false};
  bool output_focus_{false};
  bool find_focus_{false};
  bool git_focus_{false};
  bool theme_picker_visible_{false};
  bool follow_cursor_{true};
  std::size_t editor_visible_lines_{1U};
  std::size_t theme_selected_{0U};
  ToolWindow active_tool_window_{ToolWindow::search};
  std::string prompt_text_;
  std::string last_find_;
  std::string status_;
  std::string output_;
  std::string output_title_;
  GitStatusResult git_status_;
  GitRepositoryInfo repository_info_;
  std::size_t git_selected_{0};
  std::string task_output_;
  std::string task_output_title_{"TASKS"};
  std::string shell_output_;
  std::vector<FindMatch> find_results_;
  std::size_t find_selected_{0};
  std::vector<ProjectSearchMatch> search_results_;
  std::size_t search_selected_{0};
  std::optional<std::chrono::steady_clock::time_point> search_started_at_;
};

}  // namespace

bool interactive_workspace_available() noexcept {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0;
#else
  return isatty(fileno(stdin)) != 0 && isatty(fileno(stdout)) != 0;
#endif
}

int run_interactive_workspace(Document& document, ProjectContext& project,
                              const std::filesystem::path& state_directory,
                              const std::optional<std::size_t> initial_line,
                              const bool restore_session) {
  Workspace workspace(std::move(document), project, state_directory, restore_session);
  if (initial_line && *initial_line > 0U) {
    workspace.go_to_line(*initial_line);
  }

  auto app = App::Fullscreen();
  app.TrackMouse(true);
  app.ForceHandleCtrlZ(false);
  auto component = Renderer([&] { return workspace.render(); });
  component = CatchEvent(component, [&](const Event& event) {
    return workspace.event(event, app);
  });
  app.Loop(component);
  workspace.wait_for_background_work();
  document = workspace.release_document();
  return 0;
}

}  // namespace horcrux
