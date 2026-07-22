#include "omega/profiles/profile_catalog.h"

#include "omega/debug/subsystem_entry_break.h"
#include "omega/persistence/save_database.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::profiles {
// The project-owned catalog ceiling must cover the storage layer's own record
// ceiling, so a profiles/ namespace can never be legal to the database yet
// unrepresentable to the default enumeration. Every record a database holds is
// checked against its configured max_records, which is itself validated at or
// below kHardMaxRecords, so this relationship is what keeps List() — that is,
// ListBounded(kProfileCatalogMaxProfiles) — from newly failing closed on a
// catalog written before the bound existed.
static_assert(kProfileCatalogMaxProfiles >=
                  persistence::SaveDatabase::kHardMaxRecords,
              "the project-owned profile ceiling must cover "
              "SaveDatabase::kHardMaxRecords so the default enumeration admits "
              "every database-legal profile catalog");

namespace {
using persistence::SaveDatabaseError;
using persistence::SaveDatabaseErrorCode;
using persistence::SaveMutation;
using persistence::SaveRecord;
using persistence::SaveWriteCondition;

constexpr std::string_view kProfileKeyPrefix = "profiles/";
constexpr std::string_view kProfileMetadataKeySuffix = "/metadata";
constexpr std::size_t kEncodedMetadataHeaderBytes = 32U;
constexpr std::array<std::byte, 8U> kEncodedMetadataMagic{
    std::byte{0x4f}, std::byte{0x4f}, std::byte{0x50}, std::byte{0x52},
    std::byte{0x4f}, std::byte{0x46}, std::byte{0x4d}, std::byte{0x44},
};

[[nodiscard]] ProfileCatalogError MakeError(const ProfileCatalogErrorCode code,
                                            std::string message) {
  return {.code = code, .message = std::move(message)};
}

[[nodiscard]] ProfileCatalogError
MakeStorageError(const std::string_view operation,
                 const SaveDatabaseError &error) {
  std::string message(operation);
  message += ": ";
  message += error.message;
  return MakeError(ProfileCatalogErrorCode::StorageFailure, std::move(message));
}

[[nodiscard]] ProfileCatalogError OutOfMemoryError() {
  return MakeError(ProfileCatalogErrorCode::ResourceExhausted,
                   "profile catalog allocation failed");
}

[[nodiscard]] constexpr std::optional<std::uint8_t>
LowerHexValue(const char value) noexcept {
  if (value >= '0' && value <= '9')
    return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<std::uint8_t>(10 + value - 'a');
  return std::nullopt;
}

[[nodiscard]] bool IsUtf8Continuation(const std::uint8_t value) noexcept {
  return value >= 0x80U && value <= 0xbfU;
}

[[nodiscard]] bool IsValidDisplayName(const std::string_view name) noexcept {
  if (name.empty() || name.size() > kProfileDisplayNameMaxBytes)
    return false;

  const auto *bytes = reinterpret_cast<const unsigned char *>(name.data());
  std::size_t cursor = 0U;
  while (cursor < name.size()) {
    const std::uint8_t first = bytes[cursor];
    if (first <= 0x7fU) {
      if (first <= 0x1fU || first == 0x7fU)
        return false;
      ++cursor;
      continue;
    }

    if (first >= 0xc2U && first <= 0xdfU) {
      if (cursor + 1U >= name.size() ||
          !IsUtf8Continuation(bytes[cursor + 1U])) {
        return false;
      }
      // Reject the Unicode C1 control block U+0080 through U+009F.
      if (first == 0xc2U && bytes[cursor + 1U] <= 0x9fU)
        return false;
      cursor += 2U;
      continue;
    }

    if (first >= 0xe0U && first <= 0xefU) {
      if (cursor + 2U >= name.size() ||
          !IsUtf8Continuation(bytes[cursor + 1U]) ||
          !IsUtf8Continuation(bytes[cursor + 2U])) {
        return false;
      }
      if ((first == 0xe0U && bytes[cursor + 1U] < 0xa0U) ||
          (first == 0xedU && bytes[cursor + 1U] > 0x9fU)) {
        return false;
      }
      cursor += 3U;
      continue;
    }

    if (first >= 0xf0U && first <= 0xf4U) {
      if (cursor + 3U >= name.size() ||
          !IsUtf8Continuation(bytes[cursor + 1U]) ||
          !IsUtf8Continuation(bytes[cursor + 2U]) ||
          !IsUtf8Continuation(bytes[cursor + 3U])) {
        return false;
      }
      if ((first == 0xf0U && bytes[cursor + 1U] < 0x90U) ||
          (first == 0xf4U && bytes[cursor + 1U] > 0x8fU)) {
        return false;
      }
      cursor += 4U;
      continue;
    }

    return false;
  }
  return true;
}

[[nodiscard]] std::expected<void, ProfileCatalogError>
ValidateMetadata(const ProfileMetadata &metadata) {
  if (!IsValidDisplayName(metadata.display_name)) {
    return std::unexpected(MakeError(
        ProfileCatalogErrorCode::InvalidMetadata,
        "profile display name must be valid control-free UTF-8 between 1 and " +
            std::to_string(kProfileDisplayNameMaxBytes) + " bytes"));
  }
  if (metadata.created_unix_milliseconds >
          kProfileTimestampMaxUnixMilliseconds ||
      metadata.modified_unix_milliseconds >
          kProfileTimestampMaxUnixMilliseconds) {
    return std::unexpected(MakeError(
        ProfileCatalogErrorCode::InvalidMetadata,
        "profile timestamp exceeds the supported UTC millisecond range"));
  }
  if (metadata.modified_unix_milliseconds <
      metadata.created_unix_milliseconds) {
    return std::unexpected(
        MakeError(ProfileCatalogErrorCode::InvalidMetadata,
                  "profile modification time precedes its creation time"));
  }
  return {};
}

void AppendU16(std::vector<std::byte> &bytes, const std::uint16_t value) {
  bytes.push_back(static_cast<std::byte>(value & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void AppendU32(std::vector<std::byte> &bytes, const std::uint32_t value) {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

void AppendU64(std::vector<std::byte> &bytes, const std::uint64_t value) {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

[[nodiscard]] std::uint16_t LoadU16(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint8_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(
           std::to_integer<std::uint8_t>(bytes[offset + 1U]))
       << 8U));
}

[[nodiscard]] std::uint32_t LoadU32(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
  std::uint32_t value = 0U;
  for (std::uint32_t index = 0U; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::uint64_t LoadU64(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
  std::uint64_t value = 0U;
  for (std::uint32_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::vector<std::byte>
EncodeMetadata(const ProfileMetadata &metadata) {
  std::vector<std::byte> bytes;
  bytes.reserve(kEncodedMetadataHeaderBytes + metadata.display_name.size());
  bytes.insert(bytes.end(), kEncodedMetadataMagic.begin(),
               kEncodedMetadataMagic.end());
  AppendU16(bytes, 1U);
  AppendU16(bytes, 0U);
  AppendU32(bytes, static_cast<std::uint32_t>(metadata.display_name.size()));
  AppendU64(bytes, metadata.created_unix_milliseconds);
  AppendU64(bytes, metadata.modified_unix_milliseconds);
  for (const char value : metadata.display_name) {
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return bytes;
}

[[nodiscard]] std::expected<ProfileMetadata, ProfileCatalogError>
DecodeMetadata(const SaveRecord &record) {
  if (record.schema_version != kProfileMetadataSchemaVersion) {
    return std::unexpected(MakeError(
        ProfileCatalogErrorCode::UnsupportedMetadata,
        "profile marker uses an unsupported database schema version"));
  }
  if (record.value.size() < kEncodedMetadataHeaderBytes) {
    return std::unexpected(MakeError(ProfileCatalogErrorCode::CorruptMetadata,
                                     "profile metadata header is truncated"));
  }
  const std::span<const std::byte> bytes(record.value);
  if (!std::equal(kEncodedMetadataMagic.begin(), kEncodedMetadataMagic.end(),
                  bytes.begin())) {
    return std::unexpected(MakeError(ProfileCatalogErrorCode::CorruptMetadata,
                                     "profile metadata magic is invalid"));
  }
  const std::uint16_t format_version = LoadU16(bytes, 8U);
  if (format_version != 1U) {
    return std::unexpected(
        MakeError(ProfileCatalogErrorCode::UnsupportedMetadata,
                  "profile metadata payload version is unsupported"));
  }
  if (LoadU16(bytes, 10U) != 0U) {
    return std::unexpected(MakeError(ProfileCatalogErrorCode::CorruptMetadata,
                                     "profile metadata flags are nonzero"));
  }

  const std::uint32_t name_bytes = LoadU32(bytes, 12U);
  if (name_bytes == 0U || name_bytes > kProfileDisplayNameMaxBytes ||
      name_bytes > std::numeric_limits<std::size_t>::max() -
                       kEncodedMetadataHeaderBytes ||
      record.value.size() != kEncodedMetadataHeaderBytes + name_bytes) {
    return std::unexpected(
        MakeError(ProfileCatalogErrorCode::CorruptMetadata,
                  "profile metadata display-name extent is invalid"));
  }

  ProfileMetadata metadata;
  metadata.created_unix_milliseconds = LoadU64(bytes, 16U);
  metadata.modified_unix_milliseconds = LoadU64(bytes, 24U);
  metadata.display_name.reserve(name_bytes);
  for (std::size_t index = kEncodedMetadataHeaderBytes;
       index < record.value.size(); ++index) {
    metadata.display_name.push_back(
        static_cast<char>(std::to_integer<unsigned char>(bytes[index])));
  }
  const auto validation = ValidateMetadata(metadata);
  if (!validation) {
    return std::unexpected(MakeError(
        ProfileCatalogErrorCode::CorruptMetadata,
        "profile metadata payload violates the typed metadata contract"));
  }
  return metadata;
}

[[nodiscard]] std::string MetadataKey(const ProfileId id) {
  std::string key(kProfileKeyPrefix);
  key += id.ToString();
  key += kProfileMetadataKeySuffix;
  return key;
}

// Shape test only: true when the key occupies a direct profile marker slot,
// whether or not the identifier it carries is canonical. A malformed direct
// marker is still a marker, so it must spend enumeration budget before it is
// parsed; that ordering is what makes an over-populated namespace report
// ResourceExhausted rather than a corruption verdict it cannot afford to reach.
[[nodiscard]] bool IsDirectMarkerKey(const std::string_view key) noexcept {
  if (!key.starts_with(kProfileKeyPrefix) ||
      !key.ends_with(kProfileMetadataKeySuffix)) {
    return false;
  }
  if (key.size() <
      kProfileKeyPrefix.size() + kProfileMetadataKeySuffix.size()) {
    // The prefix and suffix overlap, leaving no identifier at all. This is a
    // degenerate direct marker, not a nested record.
    return true;
  }

  const std::size_t middle_bytes =
      key.size() - kProfileKeyPrefix.size() - kProfileMetadataKeySuffix.size();
  return key.substr(kProfileKeyPrefix.size(), middle_bytes).find('/') ==
         std::string_view::npos;
}

// Precondition: IsDirectMarkerKey(key). Fails closed on any identifier the
// canonical grammar rejects.
[[nodiscard]] std::expected<ProfileId, ProfileCatalogError>
ParseDirectMarkerId(const std::string_view key) {
  if (key.size() <
      kProfileKeyPrefix.size() + kProfileMetadataKeySuffix.size()) {
    return std::unexpected(MakeError(
        ProfileCatalogErrorCode::CorruptMetadata,
        "profile metadata marker has an incomplete profile identifier"));
  }

  const std::size_t middle_bytes =
      key.size() - kProfileKeyPrefix.size() - kProfileMetadataKeySuffix.size();
  const std::optional<ProfileId> id =
      ProfileId::Parse(key.substr(kProfileKeyPrefix.size(), middle_bytes));
  if (!id) {
    return std::unexpected(MakeError(
        ProfileCatalogErrorCode::CorruptMetadata,
        "profile metadata marker has a noncanonical profile identifier"));
  }
  return *id;
}

[[nodiscard]] std::expected<ProfileSummary, ProfileCatalogError>
DecodeSummary(const ProfileId id, const SaveRecord &record) {
  auto metadata = DecodeMetadata(record);
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));
  return ProfileSummary{
      .id = id,
      .metadata = std::move(*metadata),
      .metadata_revision = record.revision,
  };
}
} // namespace

std::optional<ProfileId>
ProfileId::Parse(const std::string_view text) noexcept {
  OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_profiles");
  if (text.size() != 32U)
    return std::nullopt;
  std::array<std::uint8_t, 16U> bytes{};
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    const auto high = LowerHexValue(text[index * 2U]);
    const auto low = LowerHexValue(text[index * 2U + 1U]);
    if (!high || !low)
      return std::nullopt;
    bytes[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
  }
  return ProfileId(bytes);
}

std::string ProfileId::ToString() const {
  constexpr std::string_view alphabet = "0123456789abcdef";
  std::string result(32U, '0');
  for (std::size_t index = 0U; index < bytes_.size(); ++index) {
    result[index * 2U] = alphabet[bytes_[index] >> 4U];
    result[index * 2U + 1U] = alphabet[bytes_[index] & 0x0fU];
  }
  return result;
}

std::string_view
ProfileCatalogErrorCodeName(const ProfileCatalogErrorCode code) noexcept {
  switch (code) {
  case ProfileCatalogErrorCode::InvalidMetadata:
    return "invalid-metadata";
  case ProfileCatalogErrorCode::AlreadyExists:
    return "already-exists";
  case ProfileCatalogErrorCode::NotFound:
    return "not-found";
  case ProfileCatalogErrorCode::RevisionConflict:
    return "revision-conflict";
  case ProfileCatalogErrorCode::CorruptMetadata:
    return "corrupt-metadata";
  case ProfileCatalogErrorCode::UnsupportedMetadata:
    return "unsupported-metadata";
  case ProfileCatalogErrorCode::StorageFailure:
    return "storage-failure";
  case ProfileCatalogErrorCode::ResourceExhausted:
    return "resource-exhausted";
  }
  return "storage-failure";
}

ProfileCatalog::ProfileCatalog(persistence::SaveDatabase &database) noexcept
    : database_(&database) {}

std::expected<ProfileSummary, ProfileCatalogError>
ProfileCatalog::Create(const ProfileId id, ProfileMetadata metadata) {
  try {
    const auto validation = ValidateMetadata(metadata);
    if (!validation)
      return std::unexpected(validation.error());

    std::array mutation{
        SaveMutation::Put(MetadataKey(id), kProfileMetadataSchemaVersion,
                          EncodeMetadata(metadata),
                          SaveWriteCondition::MustBeAbsent()),
    };
    const auto committed = database_->Commit(mutation);
    if (!committed) {
      if (committed.error().code == SaveDatabaseErrorCode::PreconditionFailed) {
        return std::unexpected(MakeError(ProfileCatalogErrorCode::AlreadyExists,
                                         "profile already exists"));
      }
      return std::unexpected(
          MakeStorageError("profile create failed", committed.error()));
    }
    return ProfileSummary{
        .id = id,
        .metadata = std::move(metadata),
        .metadata_revision = *committed,
    };
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}

std::expected<std::optional<ProfileSummary>, ProfileCatalogError>
ProfileCatalog::Read(const ProfileId id) const {
  try {
    auto record = database_->Read(MetadataKey(id));
    if (!record) {
      return std::unexpected(
          MakeStorageError("profile read failed", record.error()));
    }
    if (!*record)
      return std::optional<ProfileSummary>{};

    auto summary = DecodeSummary(id, **record);
    if (!summary)
      return std::unexpected(std::move(summary.error()));
    return std::optional<ProfileSummary>{std::move(*summary)};
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}

std::expected<std::vector<ProfileSummary>, ProfileCatalogError>
ProfileCatalog::List() const {
  return ListBounded(kProfileCatalogMaxProfiles);
}

std::expected<std::vector<ProfileSummary>, ProfileCatalogError>
ProfileCatalog::ListBounded(const std::size_t max_profiles) const {
  try {
    if (max_profiles > kProfileCatalogMaxProfiles) {
      return std::unexpected(
          MakeError(ProfileCatalogErrorCode::ResourceExhausted,
                    "requested profile enumeration budget exceeds the "
                    "project-owned catalog ceiling"));
    }

    // SaveDatabase::List still materializes prefix metadata for the whole
    // profiles/ namespace under its own hard record cap; max_profiles bounds
    // only what this catalog admits from that listing.
    const auto records = database_->List(kProfileKeyPrefix);
    if (!records) {
      return std::unexpected(
          MakeStorageError("profile list failed", records.error()));
    }

    std::vector<ProfileSummary> summaries;
    summaries.reserve(std::min(records->size(), max_profiles));
    std::size_t marker_candidates = 0U;
    for (const auto &record_info : *records) {
      if (!IsDirectMarkerKey(record_info.key))
        continue;

      ++marker_candidates;
      if (marker_candidates > max_profiles) {
        return std::unexpected(
            MakeError(ProfileCatalogErrorCode::ResourceExhausted,
                      "profile namespace holds more markers than the "
                      "enumeration budget admits"));
      }

      auto marker_id = ParseDirectMarkerId(record_info.key);
      if (!marker_id)
        return std::unexpected(std::move(marker_id.error()));

      auto record = database_->Read(record_info.key);
      if (!record) {
        return std::unexpected(
            MakeStorageError("profile marker read failed", record.error()));
      }
      if (!*record) {
        return std::unexpected(MakeError(
            ProfileCatalogErrorCode::CorruptMetadata,
            "listed profile marker disappeared during serialized access"));
      }
      auto summary = DecodeSummary(*marker_id, **record);
      if (!summary)
        return std::unexpected(std::move(summary.error()));
      summaries.push_back(std::move(*summary));
    }

    std::sort(summaries.begin(), summaries.end(),
              [](const ProfileSummary &left, const ProfileSummary &right) {
                return left.id < right.id;
              });
    return summaries;
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}

std::expected<ProfileSummary, ProfileCatalogError>
ProfileCatalog::Update(const ProfileId id, ProfileMetadata metadata,
                       const std::uint64_t expected_metadata_revision) {
  try {
    const auto validation = ValidateMetadata(metadata);
    if (!validation)
      return std::unexpected(validation.error());

    auto current = Read(id);
    if (!current)
      return std::unexpected(std::move(current.error()));
    if (!*current) {
      return std::unexpected(MakeError(ProfileCatalogErrorCode::NotFound,
                                       "profile does not exist"));
    }
    if (expected_metadata_revision == 0U ||
        (**current).metadata_revision != expected_metadata_revision) {
      return std::unexpected(MakeError(
          ProfileCatalogErrorCode::RevisionConflict,
          "profile metadata revision does not match the observed revision"));
    }
    if (metadata.created_unix_milliseconds !=
        (**current).metadata.created_unix_milliseconds) {
      return std::unexpected(MakeError(ProfileCatalogErrorCode::InvalidMetadata,
                                       "profile creation time is immutable"));
    }
    if (metadata.modified_unix_milliseconds <
        (**current).metadata.modified_unix_milliseconds) {
      return std::unexpected(
          MakeError(ProfileCatalogErrorCode::InvalidMetadata,
                    "profile modification time cannot move backwards"));
    }

    std::array mutation{
        SaveMutation::Put(
            MetadataKey(id), kProfileMetadataSchemaVersion,
            EncodeMetadata(metadata),
            SaveWriteCondition::ExactRevision(expected_metadata_revision)),
    };
    const auto committed = database_->Commit(mutation);
    if (!committed) {
      if (committed.error().code == SaveDatabaseErrorCode::PreconditionFailed) {
        return std::unexpected(
            MakeError(ProfileCatalogErrorCode::RevisionConflict,
                      "profile metadata changed before the update committed"));
      }
      return std::unexpected(
          MakeStorageError("profile update failed", committed.error()));
    }
    return ProfileSummary{
        .id = id,
        .metadata = std::move(metadata),
        .metadata_revision = *committed,
    };
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}
} // namespace omega::profiles
