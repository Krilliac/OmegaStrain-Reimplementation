#include "omega/profiles/character_catalog.h"

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
static_assert(kCharacterCatalogMaxCharacters >=
                  persistence::SaveDatabase::kHardMaxRecords,
              "the project-owned character ceiling must cover "
              "SaveDatabase::kHardMaxRecords so the default profile-scoped "
              "enumeration admits every database-legal character catalog");

namespace {
using persistence::SaveDatabase;
using persistence::SaveDatabaseError;
using persistence::SaveDatabaseErrorCode;
using persistence::SaveMutation;
using persistence::SaveRecord;
using persistence::SaveWriteCondition;

constexpr std::string_view kProfilesKeyPrefix = "profiles/";
constexpr std::string_view kCharactersKeySegment = "/characters/";
constexpr std::string_view kCharacterMetadataKeySuffix = "/metadata";
constexpr std::size_t kEncodedMetadataHeaderBytes = 32U;
constexpr std::array<std::byte, 8U> kEncodedMetadataMagic{
    std::byte{0x4f}, std::byte{0x4f}, std::byte{0x43}, std::byte{0x48},
    std::byte{0x41}, std::byte{0x52}, std::byte{0x4d}, std::byte{0x44},
};

[[nodiscard]] CharacterCatalogError
MakeError(const CharacterCatalogErrorCode code, std::string message) {
  return {.code = code, .message = std::move(message)};
}

[[nodiscard]] CharacterCatalogError
MakeStorageError(const std::string_view operation,
                 const SaveDatabaseError &error) {
  std::string message(operation);
  message += ": ";
  message += error.message;
  return MakeError(CharacterCatalogErrorCode::StorageFailure,
                   std::move(message));
}

[[nodiscard]] CharacterCatalogError OutOfMemoryError() {
  return MakeError(CharacterCatalogErrorCode::ResourceExhausted,
                   "character catalog allocation failed");
}

[[nodiscard]] CharacterCatalogError
MapParentProfileError(const ProfileCatalogError &error) {
  CharacterCatalogErrorCode code = CharacterCatalogErrorCode::StorageFailure;
  switch (error.code) {
  case ProfileCatalogErrorCode::InvalidMetadata:
    code = CharacterCatalogErrorCode::InvalidMetadata;
    break;
  case ProfileCatalogErrorCode::AlreadyExists:
    code = CharacterCatalogErrorCode::AlreadyExists;
    break;
  case ProfileCatalogErrorCode::NotFound:
    code = CharacterCatalogErrorCode::NotFound;
    break;
  case ProfileCatalogErrorCode::RevisionConflict:
    code = CharacterCatalogErrorCode::RevisionConflict;
    break;
  case ProfileCatalogErrorCode::CorruptMetadata:
    code = CharacterCatalogErrorCode::CorruptMetadata;
    break;
  case ProfileCatalogErrorCode::UnsupportedMetadata:
    code = CharacterCatalogErrorCode::UnsupportedMetadata;
    break;
  case ProfileCatalogErrorCode::StorageFailure:
    code = CharacterCatalogErrorCode::StorageFailure;
    break;
  case ProfileCatalogErrorCode::ResourceExhausted:
    code = CharacterCatalogErrorCode::ResourceExhausted;
    break;
  }

  std::string message("parent profile validation failed: ");
  message += error.message;
  return MakeError(code, std::move(message));
}

[[nodiscard]] std::expected<void, CharacterCatalogError>
RequireParentProfile(SaveDatabase &database, const ProfileId profile_id) {
  ProfileCatalog profiles(database);
  auto parent = profiles.Read(profile_id);
  if (!parent)
    return std::unexpected(MapParentProfileError(parent.error()));
  if (!*parent) {
    return std::unexpected(MakeError(CharacterCatalogErrorCode::NotFound,
                                     "parent profile does not exist"));
  }
  return {};
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
  if (name.empty() || name.size() > kCharacterDisplayNameMaxBytes)
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

[[nodiscard]] std::expected<void, CharacterCatalogError>
ValidateMetadata(const CharacterMetadata &metadata) {
  if (!IsValidDisplayName(metadata.display_name)) {
    return std::unexpected(MakeError(
        CharacterCatalogErrorCode::InvalidMetadata,
        "character display name must be valid control-free UTF-8 between 1 "
        "and " +
            std::to_string(kCharacterDisplayNameMaxBytes) + " bytes"));
  }
  if (metadata.created_unix_milliseconds >
          kCharacterTimestampMaxUnixMilliseconds ||
      metadata.modified_unix_milliseconds >
          kCharacterTimestampMaxUnixMilliseconds) {
    return std::unexpected(MakeError(
        CharacterCatalogErrorCode::InvalidMetadata,
        "character timestamp exceeds the supported UTC millisecond range"));
  }
  if (metadata.modified_unix_milliseconds <
      metadata.created_unix_milliseconds) {
    return std::unexpected(
        MakeError(CharacterCatalogErrorCode::InvalidMetadata,
                  "character modification time precedes its creation time"));
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
EncodeMetadata(const CharacterMetadata &metadata) {
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

[[nodiscard]] std::expected<CharacterMetadata, CharacterCatalogError>
DecodeMetadata(const SaveRecord &record) {
  if (record.schema_version != kCharacterMetadataSchemaVersion) {
    return std::unexpected(MakeError(
        CharacterCatalogErrorCode::UnsupportedMetadata,
        "character marker uses an unsupported database schema version"));
  }
  if (record.value.size() < kEncodedMetadataHeaderBytes) {
    return std::unexpected(MakeError(CharacterCatalogErrorCode::CorruptMetadata,
                                     "character metadata header is truncated"));
  }

  const std::span<const std::byte> bytes(record.value);
  if (!std::equal(kEncodedMetadataMagic.begin(), kEncodedMetadataMagic.end(),
                  bytes.begin())) {
    return std::unexpected(MakeError(CharacterCatalogErrorCode::CorruptMetadata,
                                     "character metadata magic is invalid"));
  }
  if (LoadU16(bytes, 8U) != 1U) {
    return std::unexpected(
        MakeError(CharacterCatalogErrorCode::UnsupportedMetadata,
                  "character metadata payload version is unsupported"));
  }
  if (LoadU16(bytes, 10U) != 0U) {
    return std::unexpected(MakeError(CharacterCatalogErrorCode::CorruptMetadata,
                                     "character metadata flags are nonzero"));
  }

  const std::uint32_t name_bytes = LoadU32(bytes, 12U);
  if (name_bytes == 0U || name_bytes > kCharacterDisplayNameMaxBytes ||
      name_bytes > std::numeric_limits<std::size_t>::max() -
                       kEncodedMetadataHeaderBytes ||
      record.value.size() != kEncodedMetadataHeaderBytes + name_bytes) {
    return std::unexpected(
        MakeError(CharacterCatalogErrorCode::CorruptMetadata,
                  "character metadata display-name extent is invalid"));
  }

  CharacterMetadata metadata;
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
        CharacterCatalogErrorCode::CorruptMetadata,
        "character metadata payload violates the typed metadata contract"));
  }
  return metadata;
}

[[nodiscard]] std::string CharactersKeyPrefix(const ProfileId profile_id) {
  std::string key(kProfilesKeyPrefix);
  key += profile_id.ToString();
  key += kCharactersKeySegment;
  return key;
}

[[nodiscard]] std::string MetadataKey(const ProfileId profile_id,
                                      const CharacterId character_id) {
  std::string key = CharactersKeyPrefix(profile_id);
  key += character_id.ToString();
  key += kCharacterMetadataKeySuffix;
  return key;
}

// Shape test only. A malformed direct marker still consumes enumeration budget
// before its identifier is parsed. Nested character child records do not.
[[nodiscard]] bool IsDirectMarkerKey(const std::string_view prefix,
                                     const std::string_view key) noexcept {
  if (!key.starts_with(prefix) || !key.ends_with(kCharacterMetadataKeySuffix)) {
    return false;
  }
  if (key.size() < prefix.size() + kCharacterMetadataKeySuffix.size())
    return true;

  const std::size_t middle_bytes =
      key.size() - prefix.size() - kCharacterMetadataKeySuffix.size();
  return key.substr(prefix.size(), middle_bytes).find('/') ==
         std::string_view::npos;
}

// Precondition: IsDirectMarkerKey(prefix, key).
[[nodiscard]] std::expected<CharacterId, CharacterCatalogError>
ParseDirectMarkerId(const std::string_view prefix, const std::string_view key) {
  if (key.size() < prefix.size() + kCharacterMetadataKeySuffix.size()) {
    return std::unexpected(MakeError(
        CharacterCatalogErrorCode::CorruptMetadata,
        "character metadata marker has an incomplete character identifier"));
  }

  const std::size_t middle_bytes =
      key.size() - prefix.size() - kCharacterMetadataKeySuffix.size();
  const std::optional<CharacterId> id =
      CharacterId::Parse(key.substr(prefix.size(), middle_bytes));
  if (!id) {
    return std::unexpected(MakeError(
        CharacterCatalogErrorCode::CorruptMetadata,
        "character metadata marker has a noncanonical character identifier"));
  }
  return *id;
}

[[nodiscard]] std::expected<CharacterSummary, CharacterCatalogError>
DecodeSummary(const CharacterId id, const SaveRecord &record) {
  auto metadata = DecodeMetadata(record);
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));
  return CharacterSummary{
      .id = id,
      .metadata = std::move(*metadata),
      .metadata_revision = record.revision,
  };
}

// Parent validation is deliberately separate so Update can avoid validating it
// twice while still using the same marker read contract as the public Read.
[[nodiscard]]
std::expected<std::optional<CharacterSummary>, CharacterCatalogError>
ReadMarker(SaveDatabase &database, const ProfileId profile_id,
           const CharacterId character_id) {
  auto record = database.Read(MetadataKey(profile_id, character_id));
  if (!record) {
    return std::unexpected(
        MakeStorageError("character read failed", record.error()));
  }
  if (!*record)
    return std::optional<CharacterSummary>{};

  auto summary = DecodeSummary(character_id, **record);
  if (!summary)
    return std::unexpected(std::move(summary.error()));
  return std::optional<CharacterSummary>{std::move(*summary)};
}
} // namespace

std::optional<CharacterId>
CharacterId::Parse(const std::string_view text) noexcept {
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
  return CharacterId(bytes);
}

std::string CharacterId::ToString() const {
  constexpr std::string_view alphabet = "0123456789abcdef";
  std::string result(32U, '0');
  for (std::size_t index = 0U; index < bytes_.size(); ++index) {
    result[index * 2U] = alphabet[bytes_[index] >> 4U];
    result[index * 2U + 1U] = alphabet[bytes_[index] & 0x0fU];
  }
  return result;
}

std::string_view
CharacterCatalogErrorCodeName(const CharacterCatalogErrorCode code) noexcept {
  switch (code) {
  case CharacterCatalogErrorCode::InvalidMetadata:
    return "invalid-metadata";
  case CharacterCatalogErrorCode::AlreadyExists:
    return "already-exists";
  case CharacterCatalogErrorCode::NotFound:
    return "not-found";
  case CharacterCatalogErrorCode::RevisionConflict:
    return "revision-conflict";
  case CharacterCatalogErrorCode::CorruptMetadata:
    return "corrupt-metadata";
  case CharacterCatalogErrorCode::UnsupportedMetadata:
    return "unsupported-metadata";
  case CharacterCatalogErrorCode::StorageFailure:
    return "storage-failure";
  case CharacterCatalogErrorCode::ResourceExhausted:
    return "resource-exhausted";
  }
  return "storage-failure";
}

CharacterCatalog::CharacterCatalog(persistence::SaveDatabase &database) noexcept
    : database_(&database) {}

std::expected<CharacterSummary, CharacterCatalogError>
CharacterCatalog::Create(const ProfileId profile_id,
                         const CharacterId character_id,
                         CharacterMetadata metadata) {
  try {
    const auto parent = RequireParentProfile(*database_, profile_id);
    if (!parent)
      return std::unexpected(parent.error());

    const auto validation = ValidateMetadata(metadata);
    if (!validation)
      return std::unexpected(validation.error());

    std::array mutation{
        SaveMutation::Put(MetadataKey(profile_id, character_id),
                          kCharacterMetadataSchemaVersion,
                          EncodeMetadata(metadata),
                          SaveWriteCondition::MustBeAbsent()),
    };
    const auto committed = database_->Commit(mutation);
    if (!committed) {
      if (committed.error().code == SaveDatabaseErrorCode::PreconditionFailed) {
        return std::unexpected(
            MakeError(CharacterCatalogErrorCode::AlreadyExists,
                      "character already exists in the parent profile"));
      }
      return std::unexpected(
          MakeStorageError("character create failed", committed.error()));
    }
    return CharacterSummary{
        .id = character_id,
        .metadata = std::move(metadata),
        .metadata_revision = *committed,
    };
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}

std::expected<std::optional<CharacterSummary>, CharacterCatalogError>
CharacterCatalog::Read(const ProfileId profile_id,
                       const CharacterId character_id) const {
  try {
    const auto parent = RequireParentProfile(*database_, profile_id);
    if (!parent)
      return std::unexpected(parent.error());
    return ReadMarker(*database_, profile_id, character_id);
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}

std::expected<std::vector<CharacterSummary>, CharacterCatalogError>
CharacterCatalog::List(const ProfileId profile_id) const {
  return ListBounded(profile_id, kCharacterCatalogMaxCharacters);
}

std::expected<std::vector<CharacterSummary>, CharacterCatalogError>
CharacterCatalog::ListBounded(const ProfileId profile_id,
                              const std::size_t max_characters) const {
  try {
    const auto parent = RequireParentProfile(*database_, profile_id);
    if (!parent)
      return std::unexpected(parent.error());

    if (max_characters > kCharacterCatalogMaxCharacters) {
      return std::unexpected(MakeError(
          CharacterCatalogErrorCode::ResourceExhausted,
          "requested character enumeration budget exceeds the project-owned "
          "catalog ceiling"));
    }

    const std::string prefix = CharactersKeyPrefix(profile_id);
    const auto records = database_->List(prefix);
    if (!records) {
      return std::unexpected(
          MakeStorageError("character list failed", records.error()));
    }

    std::vector<CharacterSummary> summaries;
    summaries.reserve(std::min(records->size(), max_characters));
    std::size_t marker_candidates = 0U;
    for (const auto &record_info : *records) {
      if (!IsDirectMarkerKey(prefix, record_info.key))
        continue;

      ++marker_candidates;
      if (marker_candidates > max_characters) {
        return std::unexpected(
            MakeError(CharacterCatalogErrorCode::ResourceExhausted,
                      "profile character namespace holds more markers than the "
                      "enumeration budget admits"));
      }

      auto marker_id = ParseDirectMarkerId(prefix, record_info.key);
      if (!marker_id)
        return std::unexpected(std::move(marker_id.error()));

      auto record = database_->Read(record_info.key);
      if (!record) {
        return std::unexpected(
            MakeStorageError("character marker read failed", record.error()));
      }
      if (!*record) {
        return std::unexpected(MakeError(
            CharacterCatalogErrorCode::CorruptMetadata,
            "listed character marker disappeared during serialized access"));
      }
      auto summary = DecodeSummary(*marker_id, **record);
      if (!summary)
        return std::unexpected(std::move(summary.error()));
      summaries.push_back(std::move(*summary));
    }

    std::sort(summaries.begin(), summaries.end(),
              [](const CharacterSummary &left, const CharacterSummary &right) {
                return left.id < right.id;
              });
    return summaries;
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}

std::expected<CharacterSummary, CharacterCatalogError>
CharacterCatalog::Update(const ProfileId profile_id,
                         const CharacterId character_id,
                         CharacterMetadata metadata,
                         const std::uint64_t expected_metadata_revision) {
  try {
    const auto parent = RequireParentProfile(*database_, profile_id);
    if (!parent)
      return std::unexpected(parent.error());

    const auto validation = ValidateMetadata(metadata);
    if (!validation)
      return std::unexpected(validation.error());

    auto current = ReadMarker(*database_, profile_id, character_id);
    if (!current)
      return std::unexpected(std::move(current.error()));
    if (!*current) {
      return std::unexpected(MakeError(CharacterCatalogErrorCode::NotFound,
                                       "character does not exist"));
    }
    if (expected_metadata_revision == 0U ||
        (**current).metadata_revision != expected_metadata_revision) {
      return std::unexpected(MakeError(
          CharacterCatalogErrorCode::RevisionConflict,
          "character metadata revision does not match the observed revision"));
    }
    if (metadata.created_unix_milliseconds !=
        (**current).metadata.created_unix_milliseconds) {
      return std::unexpected(
          MakeError(CharacterCatalogErrorCode::InvalidMetadata,
                    "character creation time is immutable"));
    }
    if (metadata.modified_unix_milliseconds <
        (**current).metadata.modified_unix_milliseconds) {
      return std::unexpected(
          MakeError(CharacterCatalogErrorCode::InvalidMetadata,
                    "character modification time cannot move backwards"));
    }

    std::array mutation{
        SaveMutation::Put(
            MetadataKey(profile_id, character_id),
            kCharacterMetadataSchemaVersion, EncodeMetadata(metadata),
            SaveWriteCondition::ExactRevision(expected_metadata_revision)),
    };
    const auto committed = database_->Commit(mutation);
    if (!committed) {
      if (committed.error().code == SaveDatabaseErrorCode::PreconditionFailed) {
        return std::unexpected(MakeError(
            CharacterCatalogErrorCode::RevisionConflict,
            "character metadata changed before the update committed"));
      }
      return std::unexpected(
          MakeStorageError("character update failed", committed.error()));
    }
    return CharacterSummary{
        .id = character_id,
        .metadata = std::move(metadata),
        .metadata_revision = *committed,
    };
  } catch (const std::bad_alloc &) {
    return std::unexpected(OutOfMemoryError());
  }
}
} // namespace omega::profiles
