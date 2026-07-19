#include "horcrux/recovery.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace horcrux {
namespace {

constexpr std::string_view magic = "VIJAI-JOURNAL-1\n";
constexpr std::uint64_t maximum_snapshot_bytes = 512U * 1024U * 1024U;

void write_u64(std::ostream& output, const std::uint64_t value) {
  std::array<char, 8> bytes{};
  for (unsigned int index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool read_u64(std::istream& input, std::uint64_t& value) {
  std::array<unsigned char, 8> bytes{};
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) return false;
  value = 0;
  for (unsigned int index = 0; index < bytes.size(); ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return true;
}

std::string stable_hash(const std::string_view value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

bool write_header(const std::filesystem::path& path, const std::string& identity, std::string& error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not create recovery journal";
    return false;
  }
  output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  write_u64(output, identity.size());
  output.write(identity.data(), static_cast<std::streamsize>(identity.size()));
  if (!output) {
    error = "could not write recovery journal header";
    return false;
  }
  return true;
}

}  // namespace

std::string recovery_identity(const std::filesystem::path& document_path) {
  if (document_path.empty()) return "[untitled]";
  std::error_code error;
  const auto normalized = std::filesystem::weakly_canonical(document_path, error);
  return (error ? std::filesystem::absolute(document_path, error) : normalized).string();
}

std::string document_state_key(const std::string_view document_identity) {
  return stable_hash(document_identity);
}

RecoveryJournal::RecoveryJournal(std::filesystem::path state_directory, std::string document_identity)
    : path_(std::move(state_directory) / "recovery" /
            (document_state_key(document_identity) + ".journal")),
      identity_(std::move(document_identity)) {}

const std::filesystem::path& RecoveryJournal::path() const noexcept { return path_; }

std::optional<std::string> RecoveryJournal::latest_snapshot(std::string& error) const {
  error.clear();
  std::ifstream input(path_, std::ios::binary);
  if (!input) return std::nullopt;

  std::string actual_magic(magic.size(), '\0');
  input.read(actual_magic.data(), static_cast<std::streamsize>(actual_magic.size()));
  std::uint64_t identity_size = 0;
  if (!input || actual_magic != magic || !read_u64(input, identity_size) ||
      identity_size > maximum_snapshot_bytes) {
    error = "recovery journal has an invalid header";
    return std::nullopt;
  }
  std::string identity(static_cast<std::size_t>(identity_size), '\0');
  input.read(identity.data(), static_cast<std::streamsize>(identity.size()));
  if (!input || identity != identity_) {
    error = "recovery journal belongs to a different document";
    return std::nullopt;
  }

  std::optional<std::string> latest;
  while (true) {
    std::uint64_t snapshot_size = 0;
    if (!read_u64(input, snapshot_size)) break;
    if (snapshot_size > maximum_snapshot_bytes) {
      error = "recovery journal contains an invalid snapshot size";
      return latest;
    }
    std::string snapshot(static_cast<std::size_t>(snapshot_size), '\0');
    input.read(snapshot.data(), static_cast<std::streamsize>(snapshot.size()));
    if (!input) break;  // Ignore a partially written final record after a crash.
    latest = std::move(snapshot);
  }
  return latest;
}

bool RecoveryJournal::append_snapshot(const std::string_view text, std::string& error) {
  error.clear();
  std::error_code filesystem_error;
  std::filesystem::create_directories(path_.parent_path(), filesystem_error);
  if (filesystem_error) {
    error = "could not create recovery directory";
    return false;
  }
  if (!std::filesystem::exists(path_, filesystem_error) && !write_header(path_, identity_, error)) {
    return false;
  }
  std::ofstream output(path_, std::ios::binary | std::ios::app);
  if (!output) {
    error = "could not open recovery journal";
    return false;
  }
  write_u64(output, text.size());
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.flush();
  if (!output) {
    error = "could not append recovery snapshot";
    return false;
  }
  return true;
}

bool RecoveryJournal::clear(std::string& error) {
  error.clear();
  std::error_code filesystem_error;
  std::filesystem::remove(path_, filesystem_error);
  if (filesystem_error) {
    error = "could not clear recovery journal";
    return false;
  }
  return true;
}

}  // namespace horcrux
