#include "omega/profiles/character_catalog.h"

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
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using namespace std::string_view_literals;

using omega::persistence::SaveDatabase;
using omega::persistence::SaveDatabaseConfig;
using omega::persistence::SaveMutation;
using omega::profiles::CharacterCatalog;
using omega::profiles::CharacterCatalogError;
using omega::profiles::CharacterCatalogErrorCode;
using omega::profiles::CharacterId;
using omega::profiles::CharacterMetadata;
using omega::profiles::ProfileCatalog;
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
void CheckErrorCode(const std::expected<T, CharacterCatalogError> &result,
                    const CharacterCatalogErrorCode expected,
                    const std::string_view message) {
  if (!result && result.error().code == expected)
    return;
  std::cerr << "FAILED: " << message << "\n  expected: "
            << omega::profiles::CharacterCatalogErrorCodeName(expected)
            << "\n  actual:   "
            << (result ? "<success>"
                       : omega::profiles::CharacterCatalogErrorCodeName(
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

[[nodiscard]] ProfileId Profile(const std::string_view text) {
  const auto parsed = ProfileId::Parse(text);
  Check(parsed.has_value(), "the test profile identifier parses");
  if (parsed)
    return *parsed;
  return ProfileId::FromBytes({});
}

[[nodiscard]] CharacterId Character(const std::string_view text) {
  const auto parsed = CharacterId::Parse(text);
  Check(parsed.has_value(), "the test character identifier parses");
  if (parsed)
    return *parsed;
  return CharacterId::FromBytes({});
}

[[nodiscard]] std::string ProfileMetadataKey(const ProfileId profile_id) {
  return "profiles/" + profile_id.ToString() + "/metadata";
}

[[nodiscard]] std::string CharacterPrefix(const ProfileId profile_id) {
  return "profiles/" + profile_id.ToString() + "/characters/";
}

[[nodiscard]] std::string CharacterMetadataKey(const ProfileId profile_id,
                                               const CharacterId character_id) {
  return CharacterPrefix(profile_id) + character_id.ToString() + "/metadata";
}

class TempDirectory final {
public:
  explicit TempDirectory(const std::string_view label) {
    static std::atomic<std::uint64_t> next{0U};
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("openomega-character-catalog-tests-" + std::string(label) + "-" +
             std::to_string(tick) + "-" + std::to_string(next.fetch_add(1U)));
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    Check(!error, "the synthetic character-catalog test directory is created");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    Check(!error, "the synthetic character-catalog test directory is removed");
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

[[nodiscard]] bool
CreateParent(SaveDatabase &database, const ProfileId profile_id,
             const std::string_view display_name = "Synthetic Profile") {
  ProfileCatalog profiles(database);
  const ProfileMetadata metadata{
      .display_name = std::string(display_name),
      .created_unix_milliseconds = 100U,
      .modified_unix_milliseconds = 100U,
  };
  return profiles.Create(profile_id, metadata).has_value();
}

[[nodiscard]] CharacterMetadata
Metadata(std::string display_name, const std::uint64_t created = 1'000U,
         const std::uint64_t modified = 1'000U) {
  return {
      .display_name = std::move(display_name),
      .created_unix_milliseconds = created,
      .modified_unix_milliseconds = modified,
  };
}

void CheckIdentifiersAndErrorNames() {
  constexpr std::string_view canonical = "00112233445566778899aabbccddeeff";
  const auto parsed = CharacterId::Parse(canonical);
  Check(parsed && parsed->ToString() == canonical,
        "a canonical lowercase character identifier round-trips");
  Check(!CharacterId::Parse("00112233445566778899AABBCCDDEEFF"),
        "uppercase hexadecimal character identifiers are rejected");
  Check(!CharacterId::Parse("00112233445566778899aabbccddeef"),
        "short character identifiers are rejected");
  Check(!CharacterId::Parse("00112233445566778899aabbccddeefg"),
        "non-hexadecimal character identifiers are rejected");

  constexpr std::array expected{
      "invalid-metadata"sv,  "already-exists"sv,     "not-found"sv,
      "revision-conflict"sv, "corrupt-metadata"sv,   "unsupported-metadata"sv,
      "storage-failure"sv,   "resource-exhausted"sv,
  };
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    Check(omega::profiles::CharacterCatalogErrorCodeName(
              static_cast<CharacterCatalogErrorCode>(index)) == expected[index],
          "every character catalog error has a fixed name");
  }
  Check(omega::profiles::CharacterCatalogErrorCodeName(
            static_cast<CharacterCatalogErrorCode>(255U)) == "storage-failure",
        "an invalid character catalog error maps to the fixed fallback");
}

void CheckParentOwnershipLifecycleAndIsolation() {
  TempDirectory tree("lifecycle");
  auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
  Check(opened.has_value(), "the character lifecycle database opens");
  if (!opened)
    return;

  SaveDatabase database = std::move(*opened);
  CharacterCatalog catalog(database);
  const ProfileId first_profile = Profile("00000000000000000000000000000001");
  const ProfileId second_profile = Profile("00000000000000000000000000000002");
  const CharacterId first_character =
      Character("00000000000000000000000000000001");
  const CharacterId second_character =
      Character("ffffffffffffffffffffffffffffffff");
  const CharacterId absent_character =
      Character("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  const CharacterMetadata initial = Metadata("Initial", 1'000U, 1'250U);

  CheckErrorCode(catalog.List(first_profile),
                 CharacterCatalogErrorCode::NotFound,
                 "listing requires an existing parent profile");
  CheckErrorCode(catalog.Read(first_profile, first_character),
                 CharacterCatalogErrorCode::NotFound,
                 "reading requires an existing parent profile");
  CheckErrorCode(catalog.Create(first_profile, first_character, initial),
                 CharacterCatalogErrorCode::NotFound,
                 "creating cannot produce an orphan character marker");
  CheckErrorCode(catalog.Update(first_profile, first_character, initial, 1U),
                 CharacterCatalogErrorCode::NotFound,
                 "updating requires an existing parent profile");
  Check(database.generation() == 0U && database.record_count() == 0U,
        "missing-parent operations never mutate storage");

  Check(CreateParent(database, first_profile, "First Profile"),
        "the first synthetic parent profile commits");
  Check(CreateParent(database, second_profile, "Second Profile"),
        "the second synthetic parent profile commits");
  const auto empty = catalog.List(first_profile);
  Check(empty && empty->empty() && database.generation() == 2U,
        "a valid empty character namespace has no implicit character");

  const auto created_second = catalog.Create(
      first_profile, second_character, Metadata("First High", 2'000U, 2'000U));
  Check(created_second && created_second->id == second_character &&
            created_second->metadata_revision == 3U,
        "a character create returns the committed database revision");
  const auto created_first =
      catalog.Create(first_profile, first_character, initial);
  Check(created_first && created_first->metadata_revision == 4U,
        "a second character create returns its committed revision");
  const auto created_same_id_other_profile = catalog.Create(
      second_profile, first_character, Metadata("Second Low", 3'000U, 3'000U));
  Check(created_same_id_other_profile &&
            created_same_id_other_profile->metadata_revision == 5U,
        "the same character ID is independently owned by another profile");

  CheckErrorCode(catalog.Create(first_profile, first_character, initial),
                 CharacterCatalogErrorCode::AlreadyExists,
                 "a duplicate character in one profile is rejected");
  Check(database.generation() == 5U,
        "a duplicate character does not publish a generation");

  const auto first_list = catalog.List(first_profile);
  Check(first_list && first_list->size() == 2U &&
            (*first_list)[0].id == first_character &&
            (*first_list)[1].id == second_character &&
            (*first_list)[0].metadata.display_name == "Initial",
        "one profile's character summaries are identifier-sorted");
  const auto second_list = catalog.List(second_profile);
  Check(second_list && second_list->size() == 1U &&
            (*second_list)[0].id == first_character &&
            (*second_list)[0].metadata.display_name == "Second Low",
        "listing cannot observe another profile's character markers");
  const auto absent_in_second = catalog.Read(second_profile, second_character);
  Check(absent_in_second && !*absent_in_second,
        "a character owned by one profile is absent from another");

  const auto raw_marker =
      database.Read(CharacterMetadataKey(first_profile, first_character));
  constexpr std::array expected_magic{'O', 'O', 'C', 'H', 'A', 'R', 'M', 'D'};
  bool magic_matches =
      raw_marker && *raw_marker && (**raw_marker).value.size() == 39U;
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
                omega::profiles::kCharacterMetadataSchemaVersion &&
            magic_matches,
        "character metadata uses the fixed project-owned binary envelope");

  CharacterMetadata renamed = initial;
  renamed.display_name = "Renamed";
  renamed.modified_unix_milliseconds = 1'500U;
  CheckErrorCode(catalog.Update(first_profile, first_character, renamed, 3U),
                 CharacterCatalogErrorCode::RevisionConflict,
                 "an unobserved character revision cannot be updated");
  CheckErrorCode(catalog.Update(first_profile, absent_character, renamed, 1U),
                 CharacterCatalogErrorCode::NotFound,
                 "updating an absent character reports not-found");

  CharacterMetadata changed_creation = renamed;
  changed_creation.created_unix_milliseconds = 999U;
  CheckErrorCode(
      catalog.Update(first_profile, first_character, changed_creation, 4U),
      CharacterCatalogErrorCode::InvalidMetadata,
      "an update cannot change character creation time");
  CharacterMetadata backwards = renamed;
  backwards.modified_unix_milliseconds = 1'100U;
  CheckErrorCode(catalog.Update(first_profile, first_character, backwards, 4U),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "an update cannot move character modification time backwards");

  const auto updated =
      catalog.Update(first_profile, first_character, renamed, 4U);
  Check(updated && updated->metadata.display_name == "Renamed" &&
            updated->metadata_revision == 6U && database.generation() == 6U,
        "an exact-revision update publishes a new generation token");
  const auto reread = catalog.Read(first_profile, first_character);
  Check(reread && *reread && (**reread).metadata.display_name == "Renamed" &&
            (**reread).metadata_revision == 6U,
        "updated character metadata is readable through the typed facade");

  std::array child_record{
      SaveMutation::Put(CharacterPrefix(first_profile) +
                            first_character.ToString() + "/state",
                        1U, Bytes("opaque-child")),
  };
  Check(database.Commit(child_record).has_value(),
        "a synthetic non-marker character child record commits");
  const auto filtered = catalog.List(first_profile);
  Check(filtered && filtered->size() == 2U,
        "listing ignores non-marker character child records");
}

void CheckMetadataValidationAndReopen() {
  TempDirectory validation_tree("validation");
  auto opened = SaveDatabase::Open(Config(validation_tree.path() / "database"));
  Check(opened.has_value(), "the character validation database opens");
  if (!opened)
    return;

  SaveDatabase database = std::move(*opened);
  const ProfileId profile_id = Profile("11111111111111111111111111111111");
  const CharacterId character_id =
      Character("22222222222222222222222222222222");
  Check(CreateParent(database, profile_id),
        "the validation parent profile commits");
  CharacterCatalog catalog(database);

  CharacterMetadata metadata = Metadata("");
  CheckErrorCode(catalog.Create(profile_id, character_id, metadata),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "an empty character display name is rejected");
  metadata.display_name.assign(
      omega::profiles::kCharacterDisplayNameMaxBytes + 1U, 'x');
  CheckErrorCode(catalog.Create(profile_id, character_id, metadata),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "an over-budget character display name is rejected");
  metadata.display_name = std::string(1U, static_cast<char>(0xc0));
  CheckErrorCode(catalog.Create(profile_id, character_id, metadata),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "malformed UTF-8 is rejected");
  metadata.display_name = "bad\nname";
  CheckErrorCode(catalog.Create(profile_id, character_id, metadata),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "ASCII control characters are rejected");
  metadata.display_name =
      std::string{static_cast<char>(0xc2), static_cast<char>(0x80)};
  CheckErrorCode(catalog.Create(profile_id, character_id, metadata),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "Unicode C1 control characters are rejected");
  metadata = Metadata("Valid", 101U, 100U);
  CheckErrorCode(catalog.Create(profile_id, character_id, metadata),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "a modification time before creation is rejected");
  metadata = Metadata("Valid");
  metadata.created_unix_milliseconds =
      omega::profiles::kCharacterTimestampMaxUnixMilliseconds + 1U;
  metadata.modified_unix_milliseconds = metadata.created_unix_milliseconds;
  CheckErrorCode(catalog.Create(profile_id, character_id, metadata),
                 CharacterCatalogErrorCode::InvalidMetadata,
                 "an out-of-range UTC timestamp is rejected");
  Check(database.generation() == 1U && database.record_count() == 1U,
        "invalid character metadata never mutates the database");

  const CharacterMetadata valid{
      .display_name = std::string("Jos") + "\xc3\xa9",
      .created_unix_milliseconds = 5'000U,
      .modified_unix_milliseconds = 5'500U,
  };
  Check(catalog.Create(profile_id, character_id, valid).has_value(),
        "valid multibyte UTF-8 character metadata commits");

  const auto live = catalog.Read(profile_id, character_id);
  Check(live && *live &&
            (**live).metadata.display_name == std::string("Jos") + "\xc3\xa9" &&
            (**live).metadata_revision == 2U,
        "valid typed character metadata reads before reopen");

  // Reopen is exercised in a separate scope so the borrowed catalog cannot
  // outlive the database it references.
  {
    TempDirectory reopen_tree("reopen");
    const auto reopen_root = reopen_tree.path() / "database";
    const ProfileId reopen_profile =
        Profile("33333333333333333333333333333333");
    const CharacterId reopen_character =
        Character("44444444444444444444444444444444");
    {
      auto source = SaveDatabase::Open(Config(reopen_root));
      Check(source.has_value(), "the typed character source database opens");
      if (!source)
        return;
      SaveDatabase source_database = std::move(*source);
      Check(CreateParent(source_database, reopen_profile),
            "the typed character source parent commits");
      CharacterCatalog source_catalog(source_database);
      Check(
          source_catalog
              .Create(reopen_profile, reopen_character,
                      Metadata(std::string("Zo") + "\xc3\xab", 7'000U, 7'500U))
              .has_value(),
          "the typed character source marker commits");
    }

    auto reopened = SaveDatabase::Open(Config(reopen_root));
    Check(reopened.has_value(), "the typed character database reopens");
    if (!reopened)
      return;
    SaveDatabase reopened_database = std::move(*reopened);
    CharacterCatalog reopened_catalog(reopened_database);
    const auto read = reopened_catalog.Read(reopen_profile, reopen_character);
    Check(read && *read &&
              (**read).metadata.display_name ==
                  std::string("Zo") + "\xc3\xab" &&
              (**read).metadata.created_unix_milliseconds == 7'000U &&
              (**read).metadata.modified_unix_milliseconds == 7'500U &&
              (**read).metadata_revision == 2U,
          "typed character metadata survives database close and reopen");
  }
}

void CheckBoundedListingAndMalformedMarkers() {
  {
    TempDirectory tree("bounded");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the bounded character database opens");
    if (!opened)
      return;
    SaveDatabase database = std::move(*opened);
    const ProfileId profile_id = Profile("55555555555555555555555555555555");
    Check(CreateParent(database, profile_id),
          "the bounded character parent commits");
    CharacterCatalog catalog(database);
    const auto empty = catalog.ListBounded(profile_id, 0U);
    Check(empty && empty->empty(),
          "a zero character budget admits an empty valid namespace");

    constexpr std::array ids{
        "00000000000000000000000000000001"sv,
        "00000000000000000000000000000002"sv,
        "00000000000000000000000000000003"sv,
    };
    for (std::size_t index = ids.size(); index > 0U; --index) {
      Check(catalog
                .Create(profile_id, Character(ids[index - 1U]),
                        Metadata("Bounded " + std::to_string(index)))
                .has_value(),
            "each bounded-listing character commits");
    }

    const auto exact = catalog.ListBounded(profile_id, ids.size());
    Check(exact && exact->size() == ids.size() &&
              (*exact)[0].id == Character(ids[0]) &&
              (*exact)[1].id == Character(ids[1]) &&
              (*exact)[2].id == Character(ids[2]),
          "an exact character budget returns every marker in sorted order");
    const auto surplus = catalog.ListBounded(profile_id, ids.size() + 1U);
    Check(surplus && surplus->size() == ids.size(),
          "a surplus character budget admits the full population");
    CheckErrorCode(catalog.ListBounded(profile_id, ids.size() - 1U),
                   CharacterCatalogErrorCode::ResourceExhausted,
                   "a character budget below the population fails closed");
    CheckErrorCode(catalog.ListBounded(profile_id, 0U),
                   CharacterCatalogErrorCode::ResourceExhausted,
                   "a zero budget fails closed once a character marker exists");
    CheckErrorCode(
        catalog.ListBounded(
            profile_id, omega::profiles::kCharacterCatalogMaxCharacters + 1U),
        CharacterCatalogErrorCode::ResourceExhausted,
        "a budget above the project-owned character ceiling is rejected");

    const auto delegated = catalog.List(profile_id);
    const auto ceiling = catalog.ListBounded(
        profile_id, omega::profiles::kCharacterCatalogMaxCharacters);
    Check(delegated && ceiling && delegated->size() == ids.size() &&
              ceiling->size() == ids.size() &&
              (*delegated)[0].id == (*ceiling)[0].id,
          "List delegates to the project-owned character ceiling");

    std::array child_record{
        SaveMutation::Put(CharacterPrefix(profile_id) +
                              Character(ids[0]).ToString() + "/state",
                          1U, Bytes("opaque-child")),
    };
    Check(database.Commit(child_record).has_value(),
          "a bounded-listing non-marker child commits");
    const auto filtered = catalog.ListBounded(profile_id, ids.size());
    Check(filtered && filtered->size() == ids.size(),
          "non-marker character children spend no enumeration budget");
  }

  {
    TempDirectory tree("malformed-marker");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the malformed character database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId profile_id = Profile("66666666666666666666666666666666");
      Check(CreateParent(database, profile_id),
            "the malformed-marker parent commits");
      std::array mutation{
          SaveMutation::Put(CharacterPrefix(profile_id) +
                                "not-a-character-id/metadata",
                            1U, Bytes("synthetic")),
      };
      Check(database.Commit(mutation).has_value(),
            "a noncanonical character marker commits through raw storage");
      CharacterCatalog catalog(database);
      CheckErrorCode(catalog.ListBounded(profile_id, 0U),
                     CharacterCatalogErrorCode::ResourceExhausted,
                     "a malformed direct marker spends budget before parsing");
      CheckErrorCode(catalog.ListBounded(profile_id, 1U),
                     CharacterCatalogErrorCode::CorruptMetadata,
                     "an admitted malformed direct marker fails closed");
    }
  }

  {
    TempDirectory tree("degenerate-marker");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the degenerate character database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId profile_id = Profile("77777777777777777777777777777777");
      Check(CreateParent(database, profile_id),
            "the degenerate-marker parent commits");
      std::array mutation{
          SaveMutation::Put(CharacterPrefix(profile_id) + "metadata", 1U,
                            Bytes("synthetic")),
      };
      Check(database.Commit(mutation).has_value(),
            "an identifier-free character marker commits through raw storage");
      CharacterCatalog catalog(database);
      CheckErrorCode(catalog.ListBounded(profile_id, 0U),
                     CharacterCatalogErrorCode::ResourceExhausted,
                     "an identifier-free marker spends enumeration budget");
      CheckErrorCode(catalog.ListBounded(profile_id, 1U),
                     CharacterCatalogErrorCode::CorruptMetadata,
                     "an identifier-free marker fails closed after admission");
    }
  }
}

void CheckCorruptionVersionAndProfileIsolation() {
  {
    TempDirectory tree("future-character-schema");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the future character schema database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId profile_id = Profile("88888888888888888888888888888888");
      const CharacterId character_id =
          Character("11111111111111111111111111111111");
      Check(CreateParent(database, profile_id),
            "the future-schema parent commits");
      std::array mutation{
          SaveMutation::Put(CharacterMetadataKey(profile_id, character_id), 2U,
                            Bytes("future")),
      };
      Check(database.Commit(mutation).has_value(),
            "a future character schema commits through raw storage");
      CharacterCatalog catalog(database);
      CheckErrorCode(catalog.Read(profile_id, character_id),
                     CharacterCatalogErrorCode::UnsupportedMetadata,
                     "a future character database schema fails closed");
    }
  }

  {
    TempDirectory tree("corrupt-character-payload");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the corrupt character database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId profile_id = Profile("99999999999999999999999999999999");
      const CharacterId character_id =
          Character("22222222222222222222222222222222");
      Check(CreateParent(database, profile_id),
            "the corrupt-character parent commits");
      std::array mutation{
          SaveMutation::Put(CharacterMetadataKey(profile_id, character_id),
                            omega::profiles::kCharacterMetadataSchemaVersion,
                            Bytes("truncated")),
      };
      Check(database.Commit(mutation).has_value(),
            "a corrupt character payload commits through raw storage");
      CharacterCatalog catalog(database);
      CheckErrorCode(catalog.Read(profile_id, character_id),
                     CharacterCatalogErrorCode::CorruptMetadata,
                     "a malformed character payload fails closed");
    }
  }

  {
    TempDirectory tree("cross-profile-malformed");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the cross-profile database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId clean_profile =
          Profile("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      const ProfileId corrupt_profile =
          Profile("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
      Check(CreateParent(database, clean_profile, "Clean"),
            "the clean parent profile commits");
      Check(CreateParent(database, corrupt_profile, "Corrupt"),
            "the corrupt parent profile commits");
      std::array mutation{
          SaveMutation::Put(CharacterPrefix(corrupt_profile) +
                                "not-a-character-id/metadata",
                            1U, Bytes("synthetic")),
      };
      Check(database.Commit(mutation).has_value(),
            "the other-profile malformed marker commits");
      CharacterCatalog catalog(database);
      const auto clean = catalog.List(clean_profile);
      Check(clean && clean->empty(),
            "one profile never scans another profile's malformed markers");
      CheckErrorCode(catalog.List(corrupt_profile),
                     CharacterCatalogErrorCode::CorruptMetadata,
                     "the owning profile fails closed on its malformed marker");
    }
  }

  {
    TempDirectory tree("corrupt-parent");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the corrupt-parent database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId profile_id = Profile("cccccccccccccccccccccccccccccccc");
      const CharacterId character_id =
          Character("33333333333333333333333333333333");
      std::array mutation{
          SaveMutation::Put(ProfileMetadataKey(profile_id),
                            omega::profiles::kProfileMetadataSchemaVersion,
                            Bytes("truncated")),
      };
      Check(database.Commit(mutation).has_value(),
            "a corrupt parent profile commits through raw storage");
      CharacterCatalog catalog(database);
      CheckErrorCode(catalog.Read(profile_id, character_id),
                     CharacterCatalogErrorCode::CorruptMetadata,
                     "parent profile corruption propagates through the typed "
                     "character boundary");
      CheckErrorCode(catalog.List(profile_id),
                     CharacterCatalogErrorCode::CorruptMetadata,
                     "listing also validates corrupt parent metadata");
    }
  }

  {
    TempDirectory tree("future-parent");
    auto opened = SaveDatabase::Open(Config(tree.path() / "database"));
    Check(opened.has_value(), "the future-parent database opens");
    if (opened) {
      SaveDatabase database = std::move(*opened);
      const ProfileId profile_id = Profile("dddddddddddddddddddddddddddddddd");
      const CharacterId character_id =
          Character("44444444444444444444444444444444");
      std::array mutation{
          SaveMutation::Put(ProfileMetadataKey(profile_id), 2U,
                            Bytes("future")),
      };
      Check(database.Commit(mutation).has_value(),
            "a future parent profile schema commits through raw storage");
      CharacterCatalog catalog(database);
      CheckErrorCode(
          catalog.Create(profile_id, character_id, Metadata("Never Written")),
          CharacterCatalogErrorCode::UnsupportedMetadata,
          "an unsupported parent profile prevents character create");
      Check(database.generation() == 1U && database.record_count() == 1U,
            "parent validation failure never creates a character marker");
    }
  }
}

void CheckStorageAndResourceMappings() {
  TempDirectory tree("storage-limit");
  SaveDatabaseConfig config = Config(tree.path() / "database");
  config.limits.max_records = 1U;
  auto opened = SaveDatabase::Open(std::move(config));
  Check(opened.has_value(), "the storage-limited character database opens");
  if (!opened)
    return;

  SaveDatabase database = std::move(*opened);
  const ProfileId profile_id = Profile("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
  const CharacterId character_id =
      Character("55555555555555555555555555555555");
  Check(CreateParent(database, profile_id),
        "the storage-limited parent consumes the sole record");
  CharacterCatalog catalog(database);
  CheckErrorCode(
      catalog.Create(profile_id, character_id, Metadata("Over Record Limit")),
      CharacterCatalogErrorCode::StorageFailure,
      "a SaveDatabase record limit maps to storage-failure");
  Check(database.generation() == 1U && database.record_count() == 1U,
        "a storage failure leaves the parent-only database unchanged");
  CheckErrorCode(
      catalog.ListBounded(profile_id,
                          omega::profiles::kCharacterCatalogMaxCharacters + 1U),
      CharacterCatalogErrorCode::ResourceExhausted,
      "a catalog materialization limit maps to resource-exhausted");
}
} // namespace

int main() {
  CheckIdentifiersAndErrorNames();
  CheckParentOwnershipLifecycleAndIsolation();
  CheckMetadataValidationAndReopen();
  CheckBoundedListingAndMalformedMarkers();
  CheckCorruptionVersionAndProfileIsolation();
  CheckStorageAndResourceMappings();
  return failures == 0 ? 0 : 1;
}
