#pragma once

#include "omega/profiles/profile_catalog.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omega::persistence {
class SaveDatabase;
}

namespace omega::profiles {
inline constexpr std::uint32_t kCharacterMetadataSchemaVersion = 1U;
inline constexpr std::size_t kCharacterDisplayNameMaxBytes = 64U;
inline constexpr std::uint64_t kCharacterTimestampMaxUnixMilliseconds =
    253'402'300'799'999ULL;

// Project-owned ceiling on the number of character markers admitted by one
// profile-scoped enumeration. This is an application containment bound, not a
// retail character-slot count or PS2 storage limit. character_catalog.cpp
// verifies that it covers the storage layer's hard record ceiling, so List()
// admits every database-legal character population. Callers with smaller
// materialization budgets use ListBounded explicitly.
inline constexpr std::size_t kCharacterCatalogMaxCharacters = 4'096U;

// A native OpenOmega character identifier. Text form is exactly 32 lowercase
// hexadecimal bytes. This project-owned identity does not model a retail save,
// network account, memory-card slot, or emulator object.
class CharacterId final {
public:
  [[nodiscard]] static constexpr CharacterId
  FromBytes(std::array<std::uint8_t, 16U> bytes) noexcept {
    return CharacterId(bytes);
  }

  [[nodiscard]] static std::optional<CharacterId>
  Parse(std::string_view text) noexcept;

  [[nodiscard]] std::string ToString() const;

  [[nodiscard]] constexpr const std::array<std::uint8_t, 16U> &
  bytes() const noexcept {
    return bytes_;
  }

  auto operator<=>(const CharacterId &) const = default;

private:
  explicit constexpr CharacterId(
      const std::array<std::uint8_t, 16U> bytes) noexcept
      : bytes_(bytes) {}

  std::array<std::uint8_t, 16U> bytes_;
};

// Timestamps are supplied by the application boundary. The catalog reads no
// clock and invents no appearance, loadout, archetype, progression, or retail
// character fields.
struct CharacterMetadata {
  std::string display_name;
  std::uint64_t created_unix_milliseconds = 0U;
  std::uint64_t modified_unix_milliseconds = 0U;
};

struct CharacterSummary {
  CharacterId id;
  CharacterMetadata metadata;
  // Exact SaveDatabase generation that last wrote this marker. This is an
  // optimistic-concurrency token, not a character-local counter.
  std::uint64_t metadata_revision = 0U;
};

enum class CharacterCatalogErrorCode : std::uint8_t {
  InvalidMetadata = 0,
  AlreadyExists = 1,
  NotFound = 2,
  RevisionConflict = 3,
  CorruptMetadata = 4,
  UnsupportedMetadata = 5,
  StorageFailure = 6,
  ResourceExhausted = 7,
};

[[nodiscard]] std::string_view
CharacterCatalogErrorCodeName(CharacterCatalogErrorCode code) noexcept;

struct CharacterCatalogError {
  CharacterCatalogErrorCode code = CharacterCatalogErrorCode::StorageFailure;
  std::string message;
};

// Non-hot-reloadable typed facade over one externally owned SaveDatabase.
// NativePersistence owns the database and this catalog; CharacterCatalog only
// borrows the database and retains no active-profile or active-character state.
// The owner must destroy the catalog before destroying the database.
//
// Like SaveDatabase, this facade is externally serialized. All methods belong
// on one persistence/game thread and must not be invoked concurrently. Every
// operation takes an explicit ProfileId and validates the corresponding parent
// profile marker before reading or mutating character state. No orphan marker
// is created through this API.
class CharacterCatalog final {
public:
  // [persistence/game thread, startup] Borrows a live database. The caller
  // retains unique ownership and keeps it alive for the catalog lifetime.
  explicit CharacterCatalog(persistence::SaveDatabase &database) noexcept;

  CharacterCatalog(const CharacterCatalog &) = delete;
  CharacterCatalog &operator=(const CharacterCatalog &) = delete;
  CharacterCatalog(CharacterCatalog &&) = delete;
  CharacterCatalog &operator=(CharacterCatalog &&) = delete;

  // [owning persistence/game thread] Creates exactly one marker at
  // profiles/<profile-id>/characters/<character-id>/metadata. The parent
  // profile must exist and be valid. No implicit profile or character exists.
  [[nodiscard]] std::expected<CharacterSummary, CharacterCatalogError>
  Create(ProfileId profile_id, CharacterId character_id,
         CharacterMetadata metadata);

  // [owning persistence/game thread] Returns empty when the parent exists but
  // the requested character marker is absent. A missing parent is NotFound.
  [[nodiscard]]
  std::expected<std::optional<CharacterSummary>, CharacterCatalogError>
  Read(ProfileId profile_id, CharacterId character_id) const;

  // [owning persistence/game thread] Returns the selected profile's summaries
  // sorted by CharacterId. Equivalent to
  // ListBounded(profile_id, kCharacterCatalogMaxCharacters).
  [[nodiscard]]
  std::expected<std::vector<CharacterSummary>, CharacterCatalogError>
  List(ProfileId profile_id) const;

  // [owning persistence/game thread] Returns the selected profile's summaries
  // sorted by CharacterId, admitting at most max_characters direct markers.
  // Every direct <character-id>/metadata-shaped key spends budget before its ID
  // or payload is parsed. Malformed admitted markers fail closed; child records
  // and records owned by other profiles are ignored. Budgets above the
  // project-owned ceiling are rejected with ResourceExhausted.
  //
  // SaveDatabase::List still materializes prefix metadata under its own hard
  // record bound; this limit bounds only what this catalog admits from it.
  [[nodiscard]]
  std::expected<std::vector<CharacterSummary>, CharacterCatalogError>
  ListBounded(ProfileId profile_id, std::size_t max_characters) const;

  // [owning persistence/game thread] Optimistically replaces typed metadata.
  // The parent and character must exist. Creation time is immutable and
  // modification time cannot move backwards.
  [[nodiscard]] std::expected<CharacterSummary, CharacterCatalogError>
  Update(ProfileId profile_id, CharacterId character_id,
         CharacterMetadata metadata, std::uint64_t expected_metadata_revision);

private:
  persistence::SaveDatabase *database_;
};
} // namespace omega::profiles
