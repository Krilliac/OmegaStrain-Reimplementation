#include "omega/profiles/profile_catalog.h"

#include "omega/persistence/save_database.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using namespace std::string_view_literals;

using omega::persistence::SaveDatabase;
using omega::persistence::SaveDatabaseConfig;
using omega::persistence::SaveDatabaseError;
using omega::persistence::SaveMutation;
using omega::profiles::ProfileCatalog;
using omega::profiles::ProfileCatalogError;
using omega::profiles::ProfileCatalogErrorCode;
using omega::profiles::ProfileId;
using omega::profiles::ProfileMetadata;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <class T>
void CheckErrorCode(const std::expected<T, ProfileCatalogError> &result,
                    const ProfileCatalogErrorCode expected,
                    const std::string_view message) {
  if (!result && result.error().code == expected)
    return;
  std::cerr << "FAILED: " << message << "\n  expected: "
            << omega::profiles::ProfileCatalogErrorCodeName(expected)
            << "\n  actual:   "
            << (result ? "<success>"
                       : omega::profiles::ProfileCatalogErrorCodeName(
                             result.error().code))
            << '\n';
  ++failures;
}

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const char value : text) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

[[nodiscard]] ProfileId Id(const std::string_view text) {
  const auto parsed = ProfileId::Parse(text);
  Check(parsed.has_value(), "the test profile identifier parses");
  if (parsed)
    return *parsed;
  return ProfileId::FromBytes({});
}

[[nodiscard]] std::string MetadataKey(const ProfileId id) {
  return "profiles/" + id.ToString() + "/metadata";
}

class TempDirectory final {
public:
  explicit TempDirectory(const std::string_view label) {
    static std::atomic<std::uint64_t> next{0U};
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("openomega-profile-catalog-tests-" + std::string(label) + "-" +
             std::to_string(tick) + "-" + std::to_string(next.fetch_add(1U)));
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    Check(!error, "the synthetic profile-catalog test directory is created");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    Check(!error, "the synthetic profile-catalog test directory is removed");
  }

  TempDirectory(const TempDirectory &) = delete;
  TempDirectory &operator=(const TempDirectory &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

[[nodiscard]] SaveDatabaseConfig Config(const std::filesystem::path &path) {
  return {.directory = path};
}

void CheckIdentifiersAndErrorNames() {
  constexpr std::string_view canonical = "00112233445566778899aabbccddeeff";
  const auto parsed = ProfileId::Parse(canonical);
  Check(parsed && parsed->ToString() == canonical,
        "a canonical lowercase profile identifier round-trips");
  Check(!ProfileId::Parse("00112233445566778899AABBCCDDEEFF"),
        "uppercase hexadecimal profile identifiers are rejected");
  Check(!ProfileId::Parse("00112233445566778899aabbccddeef"),
        "short profile identifiers are rejected");
  Check(!ProfileId::Parse("00112233445566778899aabbccddeefg"),
        "non-hexadecimal profile identifiers are rejected");

  constexpr std::array expected{
      "invalid-metadata"sv,  "already-exists"sv,     "not-found"sv,
      "revision-conflict"sv, "corrupt-metadata"sv,   "unsupported-metadata"sv,
      "storage-failure"sv,   "resource-exhausted"sv,
  };
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    Check(omega::profiles::ProfileCatalogErrorCodeName(
              static_cast<ProfileCatalogErrorCode>(index)) == expected[index],
          "every profile catalog error has a fixed name");
  }
  Check(omega::profiles::ProfileCatalogErrorCodeName(
            static_cast<ProfileCatalogErrorCode>(255U)) == "storage-failure",
        "an invalid profile catalog error maps to the fixed fallback");
}

void CheckEmptyCreateListReadAndUpdate() {
  TempDirectory tree("lifecycle");
  auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
  Check(opened.has_value(), "the profile lifecycle database opens");
  if (!opened)
    return;

  SaveDatabase database = std::move(*opened);
  ProfileCatalog catalog(database);
  const auto empty = catalog.List();
  Check(empty && empty->empty() && database.generation() == 0U &&
            database.record_count() == 0U,
        "constructing and listing a catalog never creates a default profile");

  const ProfileId first = Id("00000000000000000000000000000001");
  const ProfileId second = Id("ffffffffffffffffffffffffffffffff");
  const auto absent = catalog.Read(first);
  Check(absent && !*absent && database.generation() == 0U,
        "reading an absent profile is side-effect free");

  const ProfileMetadata second_metadata{
      .display_name = "Second",
      .created_unix_milliseconds = 2'000U,
      .modified_unix_milliseconds = 2'000U,
  };
  auto created_second = catalog.Create(second, second_metadata);
  Check(created_second && created_second->id == second &&
            created_second->metadata.display_name == "Second" &&
            created_second->metadata_revision == 1U &&
            database.generation() == 1U,
        "creating a profile writes one typed marker revision");

  const ProfileMetadata first_metadata{
      .display_name = "First",
      .created_unix_milliseconds = 1'000U,
      .modified_unix_milliseconds = 1'250U,
  };
  const auto created_first = catalog.Create(first, first_metadata);
  Check(created_first && created_first->metadata_revision == 2U &&
            database.generation() == 2U,
        "a second explicit profile receives the committed generation token");

  const auto duplicate = catalog.Create(first, first_metadata);
  CheckErrorCode(duplicate, ProfileCatalogErrorCode::AlreadyExists,
                 "creating an existing profile is rejected explicitly");
  Check(database.generation() == 2U,
        "a duplicate profile does not publish a database generation");

  const auto listed = catalog.List();
  Check(listed && listed->size() == 2U && (*listed)[0].id == first &&
            (*listed)[1].id == second &&
            (*listed)[0].metadata.display_name == "First" &&
            (*listed)[1].metadata.display_name == "Second",
        "profile summaries are deterministic and identifier-sorted");

  const auto read_first = catalog.Read(first);
  Check(read_first && *read_first &&
            (**read_first).metadata.created_unix_milliseconds == 1'000U &&
            (**read_first).metadata.modified_unix_milliseconds == 1'250U &&
            (**read_first).metadata_revision == 2U,
        "reading a profile reconstructs its typed metadata and revision");

  const auto raw_marker = database.Read(MetadataKey(first));
  constexpr std::array expected_magic{'O', 'O', 'P', 'R', 'O', 'F', 'M', 'D'};
  bool magic_matches =
      raw_marker && *raw_marker && (**raw_marker).value.size() == 37U;
  if (magic_matches) {
    for (std::size_t index = 0U; index < expected_magic.size(); ++index) {
      magic_matches =
          magic_matches &&
          std::to_integer<unsigned char>((**raw_marker).value[index]) ==
              static_cast<unsigned char>(expected_magic[index]);
    }
  }
  Check(raw_marker && *raw_marker &&
            (**raw_marker).schema_version ==
                omega::profiles::kProfileMetadataSchemaVersion &&
            magic_matches,
        "profile metadata uses the fixed bounded binary envelope");

  ProfileMetadata renamed = first_metadata;
  renamed.display_name = "Renamed";
  renamed.modified_unix_milliseconds = 1'500U;
  CheckErrorCode(catalog.Update(first, renamed, 1U),
                  ProfileCatalogErrorCode::RevisionConflict,
                  "an unobserved metadata revision cannot be updated");
  CheckErrorCode(
      catalog.Update(Id("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), renamed, 1U),
      ProfileCatalogErrorCode::NotFound,
      "updating an absent profile reports not-found");

  ProfileMetadata changed_creation = renamed;
  changed_creation.created_unix_milliseconds = 999U;
  CheckErrorCode(catalog.Update(first, changed_creation, 2U),
                  ProfileCatalogErrorCode::InvalidMetadata,
                  "an update cannot change profile creation time");
  ProfileMetadata backwards = renamed;
  backwards.modified_unix_milliseconds = 1'100U;
  CheckErrorCode(catalog.Update(first, backwards, 2U),
                  ProfileCatalogErrorCode::InvalidMetadata,
                  "an update cannot move modification time backwards");

  const auto updated = catalog.Update(first, renamed, 2U);
  Check(updated && updated->metadata.display_name == "Renamed" &&
            updated->metadata_revision == 3U && database.generation() == 3U,
        "an exact-revision update publishes the new generation token");
  const auto reread = catalog.Read(first);
  Check(reread && *reread && (**reread).metadata.display_name == "Renamed" &&
            (**reread).metadata_revision == 3U,
        "the updated typed metadata is durable in the live database");

  std::array child_record{
      SaveMutation::Put("profiles/00000000000000000000000000000001/"
                        "campaign/metadata",
                        1U, Bytes("opaque-child")),
  };
  Check(database.Commit(child_record).has_value(),
        "the synthetic non-marker child record commits");
  const auto filtered = catalog.List();
  Check(filtered && filtered->size() == 2U,
        "listing ignores non-marker records in the profile namespace");
}

void CheckBoundedListing() {
  {
    TempDirectory tree("bounded-empty");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the bounded-listing empty database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      ProfileCatalog catalog(database);
      const auto empty = catalog.ListBounded(0U);
      Check(empty && empty->empty(),
            "a zero enumeration budget succeeds when no marker exists");
    }
  }

  {
    TempDirectory tree("bounded-population");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the bounded-listing population database opens");
    if (!opened)
      return;
    SaveDatabase database = std::move(*opened);
    ProfileCatalog catalog(database);

    constexpr std::array ids{
        "00000000000000000000000000000001"sv,
        "00000000000000000000000000000002"sv,
        "00000000000000000000000000000003"sv,
    };
    for (std::size_t index = 0U; index < ids.size(); ++index) {
      const ProfileMetadata metadata{
          .display_name = "Bounded " + std::to_string(index),
          .created_unix_milliseconds = 1'000U,
          .modified_unix_milliseconds = 1'000U,
      };
      Check(catalog.Create(Id(ids[index]), metadata).has_value(),
            "each synthetic bounded-listing profile commits");
    }

    const auto exact = catalog.ListBounded(ids.size());
    Check(exact && exact->size() == ids.size() &&
              (*exact)[0].id == Id(ids[0]) && (*exact)[1].id == Id(ids[1]) &&
              (*exact)[2].id == Id(ids[2]),
          "an exact enumeration budget admits every marker in sorted order");
    const auto surplus = catalog.ListBounded(ids.size() + 1U);
    Check(surplus && surplus->size() == ids.size(),
          "a surplus enumeration budget admits every marker");

    CheckErrorCode(catalog.ListBounded(ids.size() - 1U),
                   ProfileCatalogErrorCode::ResourceExhausted,
                   "a budget below the marker population fails closed");
    CheckErrorCode(catalog.ListBounded(0U),
                   ProfileCatalogErrorCode::ResourceExhausted,
                   "a zero budget fails closed once a marker exists");
    CheckErrorCode(
        catalog.ListBounded(omega::profiles::kProfileCatalogMaxProfiles + 1U),
        ProfileCatalogErrorCode::ResourceExhausted,
        "a budget above the project-owned ceiling is itself rejected");

    const auto delegated = catalog.List();
    const auto ceiling =
        catalog.ListBounded(omega::profiles::kProfileCatalogMaxProfiles);
    Check(delegated && ceiling && delegated->size() == ids.size() &&
              ceiling->size() == ids.size() &&
              (*delegated)[0].id == (*ceiling)[0].id &&
              (*delegated)[2].id == (*ceiling)[2].id,
          "List delegates to the project-owned ceiling budget");

    std::array child_record{
        SaveMutation::Put("profiles/00000000000000000000000000000002/"
                          "campaign/metadata",
                          1U, Bytes("opaque-child")),
    };
    Check(database.Commit(child_record).has_value(),
          "the synthetic bounded-listing child record commits");
    const auto filtered = catalog.ListBounded(ids.size());
    Check(filtered && filtered->size() == ids.size(),
          "non-marker records beneath profiles/ spend no enumeration budget");
  }

  {
    TempDirectory tree("bounded-noncanonical-marker");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the bounded noncanonical-marker database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      std::array mutation{
          SaveMutation::Put("profiles/not-a-profile-id/metadata", 1U,
                            Bytes("synthetic")),
      };
      Check(database.Commit(mutation).has_value(),
            "the noncanonical marker commits through raw storage");
      ProfileCatalog catalog(database);
      CheckErrorCode(catalog.ListBounded(0U),
                     ProfileCatalogErrorCode::ResourceExhausted,
                     "a malformed direct marker spends budget before it is "
                     "parsed, so a zero budget reports exhaustion");
      CheckErrorCode(catalog.ListBounded(1U),
                     ProfileCatalogErrorCode::CorruptMetadata,
                     "a malformed direct marker admitted within budget still "
                     "fails closed as corrupt");
    }
  }

  {
    TempDirectory tree("bounded-degenerate-marker");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the bounded degenerate-marker database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      std::array mutation{
          SaveMutation::Put("profiles/metadata", 1U, Bytes("synthetic")),
      };
      Check(database.Commit(mutation).has_value(),
            "the identifier-free marker commits through raw storage");
      ProfileCatalog catalog(database);
      CheckErrorCode(catalog.ListBounded(0U),
                     ProfileCatalogErrorCode::ResourceExhausted,
                     "an identifier-free marker spends budget before its "
                     "extent is inspected");
      CheckErrorCode(catalog.ListBounded(1U),
                     ProfileCatalogErrorCode::CorruptMetadata,
                     "an identifier-free marker admitted within budget fails "
                     "closed as corrupt");
    }
  }
}

void CheckMetadataValidation() {
  TempDirectory tree("validation");
  auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
  Check(opened.has_value(), "the profile validation database opens");
  if (!opened)
    return;
  SaveDatabase database = std::move(*opened);
  ProfileCatalog catalog(database);
  const ProfileId id = Id("11111111111111111111111111111111");

  ProfileMetadata metadata{
      .display_name = "",
      .created_unix_milliseconds = 100U,
      .modified_unix_milliseconds = 100U,
  };
  CheckErrorCode(catalog.Create(id, metadata),
                 ProfileCatalogErrorCode::InvalidMetadata,
                 "an empty display name is rejected");
  metadata.display_name.assign(
      omega::profiles::kProfileDisplayNameMaxBytes + 1U, 'x');
  CheckErrorCode(catalog.Create(id, metadata),
                 ProfileCatalogErrorCode::InvalidMetadata,
                 "an over-budget display name is rejected");
  metadata.display_name = std::string(1U, static_cast<char>(0xc0));
  CheckErrorCode(catalog.Create(id, metadata),
                 ProfileCatalogErrorCode::InvalidMetadata,
                 "malformed UTF-8 is rejected");
  metadata.display_name = "bad\nname";
  CheckErrorCode(catalog.Create(id, metadata),
                 ProfileCatalogErrorCode::InvalidMetadata,
                 "control characters are rejected");
  metadata.display_name = "Valid";
  metadata.created_unix_milliseconds = 101U;
  metadata.modified_unix_milliseconds = 100U;
  CheckErrorCode(catalog.Create(id, metadata),
                 ProfileCatalogErrorCode::InvalidMetadata,
                 "a modification time before creation is rejected");
  metadata.created_unix_milliseconds =
      omega::profiles::kProfileTimestampMaxUnixMilliseconds + 1U;
  metadata.modified_unix_milliseconds = metadata.created_unix_milliseconds;
  CheckErrorCode(catalog.Create(id, metadata),
                 ProfileCatalogErrorCode::InvalidMetadata,
                 "an out-of-range timestamp is rejected");
  Check(database.generation() == 0U && database.record_count() == 0U,
        "invalid typed metadata never mutates the database");
}

void CheckTypedMetadataReopen() {
  TempDirectory tree("reopen");
  const auto database_root = tree.path() / "database";
  const ProfileId id = Id("1234567890abcdef1234567890abcdef");
  {
    auto opened = SaveDatabase::Open(Config(database_root));
    Check(opened.has_value(), "the typed-metadata source database opens");
    if (!opened)
      return;
    SaveDatabase database = std::move(*opened);
    ProfileCatalog catalog(database);
    ProfileMetadata metadata{
        .display_name = std::string("Jos") + "\xc3\xa9",
        .created_unix_milliseconds = 5'000U,
        .modified_unix_milliseconds = 5'500U,
    };
    Check(catalog.Create(id, metadata).has_value(),
          "valid multibyte UTF-8 metadata commits");
  }

  auto reopened = SaveDatabase::Open(Config(database_root));
  Check(reopened.has_value(), "the typed-metadata database reopens");
  if (!reopened)
    return;
  SaveDatabase database = std::move(*reopened);
  ProfileCatalog catalog(database);
  const auto read = catalog.Read(id);
  Check(read && *read &&
            (**read).metadata.display_name == std::string("Jos") + "\xc3\xa9" &&
            (**read).metadata.created_unix_milliseconds == 5'000U &&
            (**read).metadata.modified_unix_milliseconds == 5'500U &&
            (**read).metadata_revision == 1U,
        "typed profile metadata survives database close and reopen");
}

void CheckCorruptionAndVersionHandling() {
  {
    TempDirectory tree("future-schema");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the future-schema database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId id = Id("22222222222222222222222222222222");
      std::array mutation{
          SaveMutation::Put(MetadataKey(id), 2U, Bytes("future")),
      };
      Check(database.Commit(mutation).has_value(),
            "the synthetic future profile schema commits through raw storage");
      ProfileCatalog catalog(database);
      CheckErrorCode(catalog.Read(id),
                     ProfileCatalogErrorCode::UnsupportedMetadata,
                     "a future profile schema fails closed");
    }
  }

  {
    TempDirectory tree("corrupt-payload");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the corrupt-payload database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId id = Id("33333333333333333333333333333333");
      std::array mutation{
          SaveMutation::Put(MetadataKey(id),
                            omega::profiles::kProfileMetadataSchemaVersion,
                            Bytes("truncated")),
      };
      Check(
          database.Commit(mutation).has_value(),
          "the synthetic corrupt profile payload commits through raw storage");
      ProfileCatalog catalog(database);
      CheckErrorCode(catalog.Read(id), ProfileCatalogErrorCode::CorruptMetadata,
                     "a malformed profile payload fails closed");
    }
  }

  {
    TempDirectory tree("corrupt-marker");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the corrupt-marker database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      std::array mutation{
          SaveMutation::Put("profiles/not-a-profile-id/metadata", 1U,
                            Bytes("synthetic")),
      };
      Check(database.Commit(mutation).has_value(),
            "the noncanonical marker commits through raw storage");
      ProfileCatalog catalog(database);
      CheckErrorCode(catalog.List(), ProfileCatalogErrorCode::CorruptMetadata,
                     "a noncanonical profile marker fails closed during list");
    }
  }
}
} // namespace

int main() {
  CheckIdentifiersAndErrorNames();
  CheckEmptyCreateListReadAndUpdate();
  CheckBoundedListing();
  CheckMetadataValidation();
  CheckTypedMetadataReopen();
  CheckCorruptionAndVersionHandling();
  return failures == 0 ? 0 : 1;
}
