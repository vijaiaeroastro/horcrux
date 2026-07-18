#include "horcrux/document.hpp"

#include <chrono>
#include <fstream>
#include <system_error>

namespace horcrux {
namespace {

constexpr char utf8_bom[] = "\xEF\xBB\xBF";

void append_utf8(std::string& output, const char32_t code_point) {
  if (code_point <= 0x7F) {
    output += static_cast<char>(code_point);
  } else if (code_point <= 0x7FF) {
    output += static_cast<char>(0xC0 | (code_point >> 6));
    output += static_cast<char>(0x80 | (code_point & 0x3F));
  } else if (code_point <= 0xFFFF) {
    output += static_cast<char>(0xE0 | (code_point >> 12));
    output += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
    output += static_cast<char>(0x80 | (code_point & 0x3F));
  } else {
    output += static_cast<char>(0xF0 | (code_point >> 18));
    output += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
    output += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
    output += static_cast<char>(0x80 | (code_point & 0x3F));
  }
}

std::optional<std::string> decode_utf16(const std::string& bytes, const bool little_endian,
                                        std::string& error) {
  if ((bytes.size() % 2U) != 0U) {
    error = "UTF-16 file has an incomplete code unit";
    return std::nullopt;
  }
  std::string output;
  for (std::size_t offset = 0; offset < bytes.size(); offset += 2U) {
    const auto first = static_cast<unsigned char>(bytes[offset]);
    const auto second = static_cast<unsigned char>(bytes[offset + 1U]);
    const auto unit = static_cast<char32_t>(little_endian ? (first | (second << 8U))
                                                           : ((first << 8U) | second));
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (offset + 3U >= bytes.size()) {
        error = "UTF-16 file ends with a high surrogate";
        return std::nullopt;
      }
      const auto third = static_cast<unsigned char>(bytes[offset + 2U]);
      const auto fourth = static_cast<unsigned char>(bytes[offset + 3U]);
      const auto low = static_cast<char32_t>(little_endian ? (third | (fourth << 8U))
                                                            : ((third << 8U) | fourth));
      if (low < 0xDC00 || low > 0xDFFF) {
        error = "UTF-16 high surrogate is not followed by a low surrogate";
        return std::nullopt;
      }
      append_utf8(output, 0x10000 + ((unit - 0xD800) << 10U) + (low - 0xDC00));
      offset += 2U;
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      error = "UTF-16 file has an unpaired low surrogate";
      return std::nullopt;
    } else {
      append_utf8(output, unit);
    }
  }
  return output;
}

bool decode_utf8_code_point(const std::string& text, std::size_t& offset, char32_t& output) {
  const auto first = static_cast<unsigned char>(text[offset++]);
  if (first < 0x80) {
    output = first;
    return true;
  }
  const unsigned int length = first < 0xE0 ? 2U : first < 0xF0 ? 3U : first < 0xF8 ? 4U : 0U;
  if (length == 0U || offset + (length - 1U) > text.size()) {
    return false;
  }
  char32_t code_point = first & ((1U << (7U - length)) - 1U);
  for (unsigned int index = 1; index < length; ++index) {
    const auto next = static_cast<unsigned char>(text[offset++]);
    if ((next & 0xC0U) != 0x80U) {
      return false;
    }
    code_point = (code_point << 6U) | (next & 0x3FU);
  }
  if ((length == 2U && code_point < 0x80) || (length == 3U && code_point < 0x800) ||
      (length == 4U && (code_point < 0x10000 || code_point > 0x10FFFF)) ||
      (code_point >= 0xD800 && code_point <= 0xDFFF)) {
    return false;
  }
  output = code_point;
  return true;
}

std::optional<std::string> encode_utf16(const std::string& text, const bool little_endian,
                                         std::string& error) {
  std::string output;
  const auto append_unit = [&output, little_endian](const char32_t unit) {
    const auto low = static_cast<char>(unit & 0xFFU);
    const auto high = static_cast<char>((unit >> 8U) & 0xFFU);
    if (little_endian) {
      output += low;
      output += high;
    } else {
      output += high;
      output += low;
    }
  };
  for (std::size_t offset = 0; offset < text.size();) {
    char32_t code_point = 0;
    if (!decode_utf8_code_point(text, offset, code_point)) {
      error = "document contains invalid UTF-8 and cannot be saved as UTF-16";
      return std::nullopt;
    }
    if (code_point <= 0xFFFF) {
      append_unit(code_point);
    } else {
      const auto pair = code_point - 0x10000;
      append_unit(0xD800 + (pair >> 10U));
      append_unit(0xDC00 + (pair & 0x3FFU));
    }
  }
  return output;
}

std::optional<std::string> read_file(const std::filesystem::path& path, std::string& error) {
  std::error_code filesystem_error;
  if (!std::filesystem::is_regular_file(path, filesystem_error)) {
    error = filesystem_error ? "could not inspect " + path.string()
                             : "path is not a regular file: " + path.string();
    return std::nullopt;
  }
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      error = "could not open " + path.string();
      return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(input), {});
  } catch (const std::ios_base::failure& exception) {
    error = "could not read " + path.string() + ": " + exception.what();
    return std::nullopt;
  }
}

bool write_atomically(const std::filesystem::path& path, const std::string& text, std::string& error) {
  std::error_code ec;
  const auto directory = path.parent_path().empty() ? std::filesystem::current_path() : path.parent_path();
  const auto temporary = directory / ("." + path.filename().string() + ".horcrux-write");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "could not create temporary save file " + temporary.string();
      return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) {
      error = "could not write temporary save file " + temporary.string();
      return false;
    }
  }
  std::filesystem::rename(temporary, path, ec);
  if (!ec) {
    return true;
  }
  std::filesystem::remove(temporary, ec);
  error = "could not replace " + path.string();
  return false;
}

}  // namespace

Document::Document(std::filesystem::path path, TextBuffer buffer, const TextEncoding encoding)
    : path_(std::move(path)), buffer_(std::move(buffer)), encoding_(encoding), clean_text_(buffer_.text()) {}

std::optional<Document> Document::open(const std::filesystem::path& path, std::string& error) {
  const auto contents = read_file(path, error);
  if (!contents) {
    return std::nullopt;
  }
  if (contents->starts_with("\xFF\xFE")) {
    const auto text = decode_utf16(contents->substr(2), true, error);
    if (!text) return std::nullopt;
    return Document(path, TextBuffer::from_text(*text), TextEncoding::utf16_le);
  }
  if (contents->starts_with("\xFE\xFF")) {
    const auto text = decode_utf16(contents->substr(2), false, error);
    if (!text) return std::nullopt;
    return Document(path, TextBuffer::from_text(*text), TextEncoding::utf16_be);
  }
  if (contents->find('\0') != std::string::npos) {
    error = "refusing to open binary file " + path.string();
    return std::nullopt;
  }
  const bool has_bom = contents->starts_with(utf8_bom);
  const auto text = has_bom ? contents->substr(3) : *contents;
  return Document(path, TextBuffer::from_text(text), has_bom ? TextEncoding::utf8_bom : TextEncoding::utf8);
}

Document Document::untitled() { return Document({}, TextBuffer::from_text(""), TextEncoding::utf8); }

Document Document::create(const std::filesystem::path& path) {
  return Document(path, TextBuffer::from_text(""), TextEncoding::utf8);
}

const std::filesystem::path& Document::path() const noexcept { return path_; }
bool Document::has_path() const noexcept { return !path_.empty(); }
bool Document::is_dirty() const noexcept { return buffer_.text() != clean_text_; }
TextEncoding Document::encoding() const noexcept { return encoding_; }
const TextBuffer& Document::buffer() const noexcept { return buffer_; }
TextBuffer& Document::buffer() noexcept { return buffer_; }

bool Document::save(std::string& error) {
  if (!has_path()) {
    error = "untitled documents need Save As";
    return false;
  }
  return save_as(path_, error);
}

bool Document::save_as(const std::filesystem::path& path, std::string& error) {
  std::string contents;
  if (encoding_ == TextEncoding::utf8_bom) {
    contents = utf8_bom;
    contents += buffer_.text();
  } else if (encoding_ == TextEncoding::utf16_le || encoding_ == TextEncoding::utf16_be) {
    const bool little_endian = encoding_ == TextEncoding::utf16_le;
    const auto encoded = encode_utf16(buffer_.text(), little_endian, error);
    if (!encoded) return false;
    contents = little_endian ? "\xFF\xFE" : "\xFE\xFF";
    contents += *encoded;
  } else {
    contents = buffer_.text();
  }
  if (!write_atomically(path, contents, error)) {
    return false;
  }
  path_ = path;
  mark_clean();
  return true;
}

void Document::restore_text(std::string text) { buffer_ = TextBuffer::from_text(std::move(text)); }

void Document::mark_clean() noexcept { clean_text_ = buffer_.text(); }

}  // namespace horcrux
