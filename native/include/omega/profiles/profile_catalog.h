#pragma once

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
inline constexpr std::uint32_t kProfileMetadataSchemaVersion = 1U;
inline constexpr std::size_t kProfileDisplayNameMaxBytes = 64U;
inline constexpr std::uint64_t kProfileTimestampMaxUnixMilliseconds =
    253'402'300'799'999ULL;

// Project-owned ceiling on how many profile markers one enumeration will
// admit. This is an invented application limit chosen for this reimplementation
// — it does not model a memory-card capacity, a save-slot count, or any retail
// quantity. It deliberately covers the storage layer's own record ceiling: a
// SaveDatabase can never hold more records than SaveDatabase::kHardMaxRecords,
// so no database-legal profiles/ namespace can present more direct markers than
// this admits, and the default enumeration can never reject a catalog the
// database itself accepts. profile_catalog.cpp static_asserts that
// relationship. Callers that want a tighter budget ask for one explicitly
// through ListBounded.
inline constexpr std::size_t kProfileCatalogMaxProfiles = 4'096U;

// A native OpenOmega profile identifier. Text form is exactly 32 lowercase
// hexadecimal bytes. The identifier is application-owned and does not model a
// PS2 memory-card directory, game save slot, or emulator identity.
class ProfileId final {
public:
  [[nodiscard]] static constexpr ProfileId
  FromBytes(std::array<std::uint8_t, 16U> bytes) noexcept {
    return ProfileId(bytes);
  }

  [[nodiscard]] static std::optional<ProfileId>
  Parse(std::string_view text) noexcept;

  [[nodiscard]] std::string ToString() const;

  [[nodiscard]] constexpr const std::array<std::uint8_t, 16U> &
  bytes() const noexcept {
    return bytes_;
  }

  auto operator<=>(const ProfileId &) const = default;

private:
  explicit constexpr ProfileId(
      const std::array<std::uint8_t, 16U> bytes) noexcept
      : bytes_(bytes) {}

  std::array<std::uint8_t, 16U> bytes_;
};

// Timestamps are supplied by the application boundary. The catalog performs
// no wall-clock reads, making profile mutations deterministic and testable.
struct ProfileMetadata {
  std::string display_name;
  std::uint64_t created_unix_milliseconds = 0U;
  std::uint64_t modified_unix_milliseconds = 0U;
};

struct ProfileSummary {
  ProfileId id;
  ProfileMetadata metadata;
  // Exact SaveDatabase generation that last wrote this marker. This is an
  // optimistic-concurrency token, not a profile-local incrementing counter.
  std::uint64_t metadata_revision = 0U;
};

enum class ProfileCatalogErrorCode : std::uint8_t {
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
ProfileCatalogErrorCodeName(ProfileCatalogErrorCode code) noexcept;

struct ProfileCatalogError {
  ProfileCatalogErrorCode code = ProfileCatalogErrorCode::StorageFailure;
  std::string message;
};

// Non-hot-reloadable typed facade over one externally owned SaveDatabase.
// OmegaApp owns the database and catalog; ProfileCatalog only borrows the
// database, never creates a default profile, and never selects an active one.
// The owner must destroy the catalog before destroying the database.
//
// Like SaveDatabase, this facade is externally serialized. All methods belong
// on one persistence/game thread and must not be invoked concurrently.
class ProfileCatalog final {
public:
  // [persistence/game thread, startup] Borrows a live database. The caller
  // retains unique ownership and must keep it alive for the catalog lifetime.
  explicit ProfileCatalog(persistence::SaveDatabase &database) noexcept;

  ProfileCatalog(const ProfileCatalog &) = delete;
  ProfileCatalog &operator=(const ProfileCatalog &) = delete;
  ProfileCatalog(ProfileCatalog &&) = delete;
  ProfileCatalog &operator=(ProfileCatalog &&) = delete;

  // [owning persistence/game thread] Creates exactly one marker record at
  // profiles/<32-lower-hex-id>/metadata. It never creates an implicit profile.
  [[nodiscard]] std::expected<ProfileSummary, ProfileCatalogError>
  Create(ProfileId id, ProfileMetadata metadata);

  // [owning persistence/game thread] Returns empty when the marker is absent.
  [[nodiscard]]
  std::expected<std::optional<ProfileSummary>, ProfileCatalogError>
  Read(ProfileId id) const;

  // [owning persistence/game thread] Returns summaries sorted by ProfileId.
  // Non-marker records beneath profiles/ are deliberately ignored. Exactly
  // equivalent to ListBounded(kProfileCatalogMaxProfiles). Because that ceiling
  // covers SaveDatabase::kHardMaxRecords, this enumeration admits every catalog
  // the database can legally hold and never fails closed on its own budget, so
  // it remains the unbounded whole-namespace listing it has always been.
  [[nodiscard]] std::expected<std::vector<ProfileSummary>, ProfileCatalogError>
  List() const;

  // [owning persistence/game thread] Returns summaries sorted by ProfileId,
  // admitting at most max_profiles profile markers. This is the opt-in tighter
  // budget: a caller that can only afford a handful of profiles asks for that
  // many and gets a typed refusal instead of a partial answer. Every record
  // beneath profiles/ whose key is shaped like a direct <id>/metadata marker
  // spends one unit of the budget *before* its identifier is parsed or its
  // payload read, so a namespace holding more markers than the requested budget
  // fails closed with ResourceExhausted rather than silently truncating.
  // Non-marker records beneath profiles/ are deliberately ignored and cost no
  // budget. A max_profiles above kProfileCatalogMaxProfiles is itself rejected
  // with ResourceExhausted; since that ceiling covers the storage layer's
  // record cap, only budgets no database could ever need are refused this way.
  //
  // This bounds what the catalog materializes, not the storage layer beneath
  // it. SaveDatabase::List still materializes prefix record metadata for the
  // whole profiles/ namespace — bounded only by its own
  // SaveDatabase::kHardMaxRecords cap — before this budget is applied, so this
  // is not a lower-memory enumeration path.
  [[nodiscard]] std::expected<std::vector<ProfileSummary>, ProfileCatalogError>
  ListBounded(std::size_t max_profiles) const;

  // [owning persistence/game thread] Optimistically replaces typed metadata.
  // Creation time is immutable and modification time cannot move backwards.
  [[nodiscard]] std::expected<ProfileSummary, ProfileCatalogError>
  Update(ProfileId id, ProfileMetadata metadata,
         std::uint64_t expected_metadata_revision);

private:
  persistence::SaveDatabase *database_;
};
} // namespace omega::profiles
