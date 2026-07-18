#include "pop_post_terrain_commands.h"

#include "omega/asset/pop_terrain_index.h"
#include "omega/retail/pop_post_terrain_hypothesis_descriptor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
constexpr std::array<std::string_view, 19> kLiteralOrder{
    "GOB:", "SND:", "ACL:", "INL:", "NPC:", "WPN:", "PLR:",
    "SKY:", "PNT:", "DIR:", "ENV:", "NOD:", "GEN:", "GRP:",
    "BOX:", "FIR:", "CAM:", "INV:", "BUG:",
};

struct CandidateSpec {
  std::size_t marker_ordinal = 0;
  std::uint32_t word = 0;
  std::uint32_t stride = 0;
};

constexpr std::array<CandidateSpec, 5> kCandidateSpecs{{
    {3, 2, 36},
    {8, 0, 88},
    {9, 1, 44},
    {10, 3, 76},
    {17, 0, 84},
}};

struct PopFixture {
  std::vector<std::byte> bytes;
  std::array<std::size_t, kLiteralOrder.size()> marker_offsets{};
};

void AppendU32(std::vector<std::byte> &bytes, const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void WriteU32(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes[offset + shift / 8U] =
        static_cast<std::byte>((value >> shift) & 0xFFU);
}

void AppendText(std::vector<std::byte> &bytes, const std::string_view value) {
  for (const char character : value)
    bytes.push_back(static_cast<std::byte>(character));
}

void AppendTerrainRecord(std::vector<std::byte> &bytes,
                         const std::uint32_t ordinal,
                         const std::string_view name) {
  AppendU32(bytes, ordinal + 1U);
  AppendU32(bytes, ordinal * 3U + 7U);
  AppendText(bytes, name);
  bytes.push_back(std::byte{0});
  while (bytes.size() % 4U != 0)
    bytes.push_back(std::byte{0x5A});
}

[[nodiscard]] PopFixture MakePop(const std::vector<std::string> &terrain_names,
                                 const std::size_t trailing_bytes = 0) {
  PopFixture fixture;
  AppendU32(fixture.bytes, 70);
  AppendText(fixture.bytes, "TER:");
  AppendU32(fixture.bytes, static_cast<std::uint32_t>(terrain_names.size()));
  for (std::size_t index = 0; index < terrain_names.size(); ++index) {
    AppendTerrainRecord(fixture.bytes, static_cast<std::uint32_t>(index),
                        terrain_names[index]);
  }

  for (std::size_t ordinal = 0; ordinal < kLiteralOrder.size(); ++ordinal) {
    fixture.marker_offsets[ordinal] = fixture.bytes.size();
    AppendText(fixture.bytes, kLiteralOrder[ordinal]);
    for (std::size_t index = 0; index < kCandidateSpecs.size(); ++index) {
      const CandidateSpec spec = kCandidateSpecs[index];
      if (spec.marker_ordinal != ordinal)
        continue;
      AppendU32(fixture.bytes, spec.word);
      fixture.bytes.insert(fixture.bytes.end(),
                           static_cast<std::size_t>(spec.word) * spec.stride,
                           static_cast<std::byte>(0x90U + index));
      break;
    }
  }
  fixture.bytes.insert(fixture.bytes.end(), trailing_bytes, std::byte{0xCC});
  return fixture;
}

bool WriteBytes(const std::filesystem::path &path,
                const std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  return output.good();
}

class TempTree final {
public:
  explicit TempTree(const std::string_view label) {
    static std::atomic<std::uint64_t> next{0};
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    root_ = std::filesystem::temp_directory_path() /
            ("omega-pop-hypothesis-command-" + std::string(label) + "-" +
             std::to_string(stamp) + "-" + std::to_string(next.fetch_add(1)));
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    ready_ = !error;
  }

  ~TempTree() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  TempTree(const TempTree &) = delete;
  TempTree &operator=(const TempTree &) = delete;

  [[nodiscard]] bool ready() const noexcept { return ready_; }
  [[nodiscard]] const std::filesystem::path &root() const noexcept {
    return root_;
  }

  [[nodiscard]] bool Add(const std::filesystem::path &relative,
                         const std::span<const std::byte> bytes) const {
    const std::filesystem::path path = root_ / relative;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return !error && WriteBytes(path, bytes);
  }

private:
  std::filesystem::path root_;
  bool ready_ = false;
};

class StreamCapture final {
public:
  explicit StreamCapture(std::ostream &stream)
      : stream_(stream), original_(stream.rdbuf(buffer_.rdbuf())) {}

  ~StreamCapture() {
    if (active_)
      stream_.rdbuf(original_);
  }

  [[nodiscard]] std::string Release() {
    stream_.rdbuf(original_);
    active_ = false;
    return buffer_.str();
  }

private:
  std::ostream &stream_;
  std::ostringstream buffer_;
  std::streambuf *original_ = nullptr;
  bool active_ = true;
};

struct ToolRun {
  int exit_code = 0;
  std::string standard_output;
  std::string standard_error;
};

[[nodiscard]] ToolRun RunTool(const std::filesystem::path &root) {
  StreamCapture output(std::cout);
  StreamCapture error(std::cerr);
  const int exit_code = omega::tool::PopPostTerrainHypothesesVerifyTree(root);
  return ToolRun{
      .exit_code = exit_code,
      .standard_output = output.Release(),
      .standard_error = error.Release(),
  };
}

[[nodiscard]] ToolRun
RunToolWithHooks(const std::filesystem::path &root,
                 const omega::tool::PopPostTerrainCommandTestHooks hooks) {
  StreamCapture output(std::cout);
  StreamCapture error(std::cerr);
  const int exit_code =
      omega::tool::PopPostTerrainHypothesesVerifyTreeForTesting(root, hooks);
  return ToolRun{
      .exit_code = exit_code,
      .standard_output = output.Release(),
      .standard_error = error.Release(),
  };
}

enum class ErrorIndex : std::size_t {
  DiscoveryInvalidRoot,
  DiscoveryUnsafeEntry,
  DiscoveryLimitExceeded,
  DiscoveryIo,
  NoPopCandidates,
  PopRead,
  DescriptorTruncated,
  DescriptorMalformed,
  DescriptorOverflow,
  DescriptorLimitExceeded,
  DescriptorUnsupportedVariant,
  DescriptorInvalidReference,
  DescriptorDuplicateReference,
  UsageMeasurement,
  AggregateOverflow,
  Count,
};

constexpr std::array<std::string_view,
                     static_cast<std::size_t>(ErrorIndex::Count)>
    kErrorNames{
        "discovery_invalid_root",
        "discovery_unsafe_entry",
        "discovery_limit_exceeded",
        "discovery_io",
        "no_pop_candidates",
        "pop_read",
        "descriptor_truncated",
        "descriptor_malformed",
        "descriptor_overflow",
        "descriptor_limit_exceeded",
        "descriptor_unsupported_variant",
        "descriptor_invalid_reference",
        "descriptor_duplicate_reference",
        "usage_measurement",
        "aggregate_overflow",
    };

struct LogicalUsage {
  std::uint64_t input_bytes = 0;
  std::uint64_t items = 0;
  std::uint64_t logical_output_bytes = 0;
  std::uint64_t string_bytes = 0;
  std::uint64_t peak_scratch_bytes = 0;
};

struct ExpectedReport {
  std::uint64_t pop_candidates_discovered = 0;
  std::uint64_t descriptors_accepted = 0;
  std::uint64_t descriptors_rejected = 0;
  LogicalUsage maxima;
  std::array<std::uint64_t, static_cast<std::size_t>(ErrorIndex::Count)>
      errors{};
};

[[nodiscard]] std::string BuildReport(const ExpectedReport &report) {
  std::uint64_t error_total = 0;
  for (const std::uint64_t value : report.errors)
    error_total += value;

  std::ostringstream output;
  output << "{\"schema_version\":1,"
            "\"scope\":\"native aggregate POP post-terrain hypothesis "
            "verification; "
            "independent logical field maxima over accepted descriptors only; "
            "literal order and "
            "arithmetic extents remain hypotheses, not decoded sections, "
            "counts, records, "
            "payloads, placement, visibility, rendering, or gameplay; no "
            "paths, names, hashes, "
            "literal spellings, candidate offsets, observed words, strides, "
            "opaque-region sizes, "
            "per-file rows, identities, or bindings\",\"totals\":{"
            "\"pop_candidates_discovered\":"
         << report.pop_candidates_discovered
         << ",\"descriptors_accepted\":" << report.descriptors_accepted
         << ",\"descriptors_rejected\":" << report.descriptors_rejected
         << ",\"errors\":" << error_total
         << "},\"maxima\":{\"input_bytes\":" << report.maxima.input_bytes
         << ",\"items\":" << report.maxima.items
         << ",\"logical_output_bytes\":" << report.maxima.logical_output_bytes
         << ",\"string_bytes\":" << report.maxima.string_bytes
         << ",\"peak_scratch_bytes\":" << report.maxima.peak_scratch_bytes
         << "},\"error_categories\":{";
  for (std::size_t index = 0; index < kErrorNames.size(); ++index) {
    if (index != 0)
      output << ',';
    output << '\"' << kErrorNames[index] << "\":" << report.errors[index];
  }
  output << "}}\n";
  return output.str();
}

[[nodiscard]] std::string BuildErrors(const ExpectedReport &report) {
  std::ostringstream output;
  for (std::size_t index = 0; index < report.errors.size(); ++index) {
    if (report.errors[index] != 0)
      output << "pop-post-terrain-hypotheses: " << kErrorNames[index] << '\n';
  }
  return output.str();
}

[[nodiscard]] std::optional<LogicalUsage>
MeasureUsage(const std::span<const std::byte> bytes) {
  if (!omega::retail::InspectPopPostTerrainHypotheses(bytes))
    return std::nullopt;
  auto terrain = omega::asset::PopTerrainIndex::Parse(bytes);
  if (!terrain)
    return std::nullopt;

  LogicalUsage usage{
      .input_bytes = bytes.size(),
      .items = 1,
      .logical_output_bytes =
          sizeof(omega::retail::PopPostTerrainHypothesisDescriptor),
  };
  std::uint64_t name_bytes = 0;
  for (const auto &record : terrain->records()) {
    name_bytes += record.name.size();
    usage.string_bytes =
        std::max<std::uint64_t>(usage.string_bytes, record.name.size());
  }
  usage.peak_scratch_bytes =
      terrain->records().size() *
          (sizeof(omega::asset::PopTerrainRecord) + 2U * sizeof(void *)) +
      name_bytes;
  return usage;
}

void Observe(LogicalUsage &maxima, const LogicalUsage &usage) {
  maxima.input_bytes = std::max(maxima.input_bytes, usage.input_bytes);
  maxima.items = std::max(maxima.items, usage.items);
  maxima.logical_output_bytes =
      std::max(maxima.logical_output_bytes, usage.logical_output_bytes);
  maxima.string_bytes = std::max(maxima.string_bytes, usage.string_bytes);
  maxima.peak_scratch_bytes =
      std::max(maxima.peak_scratch_bytes, usage.peak_scratch_bytes);
}

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::string PrivacyComparable(const std::string_view value) {
  std::string comparable;
  comparable.reserve(value.size());
  for (const unsigned char character : value) {
    if (character == static_cast<unsigned char>('\\')) {
      comparable.push_back('/');
      continue;
    }
    comparable.push_back(
        static_cast<char>(character >= static_cast<unsigned char>('A') &&
                                  character <= static_cast<unsigned char>('Z')
                              ? character + ('a' - 'A')
                              : character));
  }
  return comparable;
}

void CheckIndependentMaxima() {
  TempTree tree("independent-maxima");
  auto input = MakePop({"Q"}, 8192);
  auto longest = MakePop({std::string(200, 'L')});
  std::vector<std::string> many_names;
  for (std::size_t index = 0; index < 24; ++index)
    many_names.push_back("N" + std::to_string(index));
  auto scratch = MakePop(many_names);

  Check(tree.ready() && tree.Add("z/INPUT.pop", input.bytes) &&
            tree.Add("a/String.POP", longest.bytes) &&
            tree.Add("m/scratch.PoP", scratch.bytes) &&
            tree.Add("ignored.bin", std::span<const std::byte>{}),
        "mixed-case synthetic POP tree is written");
  if (!tree.ready())
    return;

  const auto input_usage = MeasureUsage(input.bytes);
  const auto string_usage = MeasureUsage(longest.bytes);
  const auto scratch_usage = MeasureUsage(scratch.bytes);
  Check(input_usage && string_usage && scratch_usage,
        "independent-maxima fixtures satisfy the public descriptor");
  if (!input_usage || !string_usage || !scratch_usage)
    return;

  Check(
      input_usage->input_bytes > string_usage->input_bytes &&
          input_usage->input_bytes > scratch_usage->input_bytes &&
          string_usage->string_bytes > input_usage->string_bytes &&
          string_usage->string_bytes > scratch_usage->string_bytes &&
          scratch_usage->peak_scratch_bytes > input_usage->peak_scratch_bytes &&
          scratch_usage->peak_scratch_bytes > string_usage->peak_scratch_bytes,
      "input, string, and scratch maxima have different candidate witnesses");

  ExpectedReport expected;
  expected.pop_candidates_discovered = 3;
  expected.descriptors_accepted = 3;
  Observe(expected.maxima, *input_usage);
  Observe(expected.maxima, *string_usage);
  Observe(expected.maxima, *scratch_usage);

  const ToolRun run = RunTool(tree.root());
  Check(
      run.exit_code == 0 && run.standard_output == BuildReport(expected) &&
          run.standard_error.empty(),
      "accepted tree emits exact fixed schema with independent logical maxima");
}

void CheckEmptyAndInvalidRoots() {
  TempTree empty("empty");
  Check(empty.ready(), "empty synthetic root is created");
  ExpectedReport empty_expected;
  empty_expected.errors[static_cast<std::size_t>(ErrorIndex::NoPopCandidates)] =
      1;
  const ToolRun empty_run = RunTool(empty.root());
  Check(
      empty_run.exit_code != 0 &&
          empty_run.standard_output == BuildReport(empty_expected) &&
          empty_run.standard_error == BuildErrors(empty_expected),
      "empty POP candidate set fails with one fixed category and zero maxima");

  const std::vector<std::byte> marker{std::byte{0x42}};
  Check(empty.Add("not-a-root.bin", marker),
        "regular-file invalid root is written");
  ExpectedReport invalid_expected;
  invalid_expected
      .errors[static_cast<std::size_t>(ErrorIndex::DiscoveryInvalidRoot)] = 1;
  const ToolRun invalid_run = RunTool(empty.root() / "not-a-root.bin");
  Check(invalid_run.exit_code != 0 &&
            invalid_run.standard_output == BuildReport(invalid_expected) &&
            invalid_run.standard_error == BuildErrors(invalid_expected),
        "non-directory root fails discovery without publishing partial totals");
}

void CheckTypedRejections() {
  TempTree tree("typed-rejections");
  auto malformed = MakePop({"MALFORMED"});
  malformed.bytes[0] = std::byte{69};
  auto unsupported = MakePop({"UNSUPPORTED"});
  WriteU32(unsupported.bytes, unsupported.marker_offsets[3] + 4U, 1);
  const std::vector<std::byte> truncated(8, std::byte{0});
  Check(tree.ready() && tree.Add("one/TRUNCATED.POP", truncated) &&
            tree.Add("two/MALFORMED.POP", malformed.bytes) &&
            tree.Add("three/UNSUPPORTED.POP", unsupported.bytes),
        "typed rejection tree is written");

  ExpectedReport expected;
  expected.pop_candidates_discovered = 3;
  expected.descriptors_rejected = 3;
  expected.errors[static_cast<std::size_t>(ErrorIndex::DescriptorTruncated)] =
      1;
  expected.errors[static_cast<std::size_t>(ErrorIndex::DescriptorMalformed)] =
      1;
  expected.errors[static_cast<std::size_t>(
      ErrorIndex::DescriptorUnsupportedVariant)] = 1;
  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code != 0 && run.standard_output == BuildReport(expected) &&
            run.standard_error == BuildErrors(expected),
        "typed descriptor failures retain zero accepted-only maxima and fixed "
        "diagnostics");
}

void CheckAtomicityAndPrivacy() {
  constexpr std::string_view root_marker = "PRIVATE_ROOT_IDENTITY";
  constexpr std::string_view valid_file_marker = "PRIVATE_VALID_FILE.POP";
  constexpr std::string_view invalid_file_marker = "PRIVATE_INVALID_FILE.POP";
  constexpr std::string_view terrain_marker = "PRIVATE_TERRAIN_NAME";
  constexpr std::string_view opaque_marker = "PRIVATE_OPAQUE_CONTENT";

  TempTree tree(root_marker);
  auto valid = MakePop({std::string(terrain_marker)});
  AppendText(valid.bytes, opaque_marker);
  auto invalid = MakePop({"REJECTED_NAME"}, 16384);
  WriteU32(invalid.bytes, invalid.marker_offsets[3] + 4U, 1);
  Check(tree.ready() && tree.Add(valid_file_marker, valid.bytes) &&
            tree.Add(invalid_file_marker, invalid.bytes),
        "private-marker atomicity tree is written");

  const auto valid_usage = MeasureUsage(valid.bytes);
  Check(valid_usage.has_value(),
        "accepted private-marker fixture is measurable");
  if (!valid_usage)
    return;

  ExpectedReport expected;
  expected.pop_candidates_discovered = 2;
  expected.descriptors_accepted = 1;
  expected.descriptors_rejected = 1;
  expected.maxima = *valid_usage;
  expected.errors[static_cast<std::size_t>(
      ErrorIndex::DescriptorUnsupportedVariant)] = 1;
  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code != 0 && run.standard_output == BuildReport(expected) &&
            run.standard_error == BuildErrors(expected) &&
            expected.maxima.input_bytes < invalid.bytes.size(),
        "rejected larger input cannot influence accepted-only maxima");

  const std::array<std::string, 7> forbidden{
      tree.root().string(),
      tree.root().generic_string(),
      std::string(root_marker),
      std::string(valid_file_marker),
      std::string(invalid_file_marker),
      std::string(terrain_marker),
      std::string(opaque_marker),
  };
  const std::string comparable_output = PrivacyComparable(run.standard_output);
  const std::string comparable_error = PrivacyComparable(run.standard_error);
  for (const auto &marker : forbidden) {
    const std::string comparable_marker = PrivacyComparable(marker);
    Check(
        comparable_output.find(comparable_marker) == std::string::npos &&
            comparable_error.find(comparable_marker) == std::string::npos,
        "aggregate output and diagnostics disclose no private identity marker");
  }
  for (const std::string_view literal : kLiteralOrder) {
    Check(run.standard_output.find(literal) == std::string::npos &&
              run.standard_error.find(literal) == std::string::npos,
          "aggregate output and diagnostics disclose no literal spelling");
  }
}

#ifdef _WIN32
bool TryCreateUnprivilegedDirectorySymlink(const std::filesystem::path &target,
                                           const std::filesystem::path &link,
                                           std::error_code &error) noexcept {
  DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
  if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags) != FALSE) {
    error.clear();
    return true;
  }
  error =
      std::error_code(static_cast<int>(GetLastError()), std::system_category());
  return false;
}

bool IsExplicitReparseCapabilitySkip(const std::error_code &error) noexcept {
  switch (static_cast<DWORD>(error.value())) {
  case ERROR_ACCESS_DENIED:
  case ERROR_INVALID_FUNCTION:
  case ERROR_NOT_SUPPORTED:
  case ERROR_INVALID_PARAMETER:
  case ERROR_CALL_NOT_IMPLEMENTED:
  case ERROR_PRIVILEGE_NOT_HELD:
    return true;
  default:
    return false;
  }
}
#endif

void CheckUnsafeLinksAreRejected() {
  TempTree tree("unsafe-link");
  auto pop = MakePop({"SAFE"});
  Check(tree.ready() && tree.Add("target/DATA.POP", pop.bytes),
        "unsafe-link target tree is written");
  const std::filesystem::path target = tree.root() / "target";
  const std::filesystem::path link = tree.root() / "linked-target";
  std::error_code error;
  std::filesystem::create_directory_symlink(target, link, error);
#ifdef _WIN32
  if (error) {
    std::error_code cleanup_error;
    std::filesystem::remove(link, cleanup_error);
    if (!TryCreateUnprivilegedDirectorySymlink(target, link, error)) {
      const bool explicit_skip = IsExplicitReparseCapabilitySkip(error);
      Check(explicit_skip, "unsafe-link fixture fails only for recognized "
                           "Windows reparse capability");
      if (explicit_skip)
        std::cout
            << "SKIP: Windows unprivileged reparse fixture is unavailable\n";
      return;
    }
  }
#else
  if (error) {
    Check(false, "unsafe-link fixture is created on this platform");
    return;
  }
#endif

  ExpectedReport expected;
  expected.errors[static_cast<std::size_t>(ErrorIndex::DiscoveryUnsafeEntry)] =
      1;
  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code != 0 && run.standard_output == BuildReport(expected) &&
            run.standard_error == BuildErrors(expected),
        "full-tree discovery rejects links before publishing candidate totals");
}

struct DeleteBeforeOpenContext {
  std::filesystem::path target;
  bool invoked = false;
  bool removed = false;
};

void DeleteBeforeOpenHook(
    const omega::tool::PopPostTerrainCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<DeleteBeforeOpenContext *>(opaque);
  if (event != omega::tool::PopPostTerrainCommandTestEvent::BeforePopFileOpen ||
      path != context.target || context.invoked)
    return;
  context.invoked = true;
  std::error_code error;
  context.removed = std::filesystem::remove(path, error) && !error;
}

struct SameSizeMutationContext {
  std::filesystem::path target;
  std::vector<std::byte> replacement;
  bool invoked = false;
  bool write_succeeded = false;
};

void SameSizeMutationHook(
    const omega::tool::PopPostTerrainCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<SameSizeMutationContext *>(opaque);
  if (event != omega::tool::PopPostTerrainCommandTestEvent::PopFileOpened ||
      path != context.target || context.invoked)
    return;
  context.invoked = true;
  context.write_succeeded = WriteBytes(path, context.replacement);
}

struct DirectoryReplacementContext {
  std::filesystem::path target;
  std::filesystem::path backup;
  bool invoked = false;
  bool rename_succeeded = false;
};

void DirectoryReplacementHook(
    const omega::tool::PopPostTerrainCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<DirectoryReplacementContext *>(opaque);
  if (event != omega::tool::PopPostTerrainCommandTestEvent::
                   DirectoryIteratorOpened ||
      path != context.target || context.invoked)
    return;
  context.invoked = true;
  std::error_code error;
  std::filesystem::rename(context.target, context.backup, error);
  context.rename_succeeded = !error;
  if (context.rename_succeeded) {
    error.clear();
    static_cast<void>(std::filesystem::create_directory(context.target, error));
  }
}

void CheckPopReadAfterCandidateDeletion() {
  TempTree tree("pop-read-delete");
  auto pop = MakePop({"DELETE_ME"});
  const std::filesystem::path target = tree.root() / "DATA.POP";
  Check(tree.ready() && tree.Add("DATA.POP", pop.bytes),
        "POP-read deletion fixture is written");

  DeleteBeforeOpenContext context{.target = target};
  const ToolRun run =
      RunToolWithHooks(tree.root(), omega::tool::PopPostTerrainCommandTestHooks{
                                        .callback = DeleteBeforeOpenHook,
                                        .context = &context,
                                    });
  ExpectedReport expected;
  expected.pop_candidates_discovered = 1;
  expected.errors[static_cast<std::size_t>(ErrorIndex::PopRead)] = 1;
  Check(context.invoked && context.removed && run.exit_code != 0 &&
            run.standard_output == BuildReport(expected) &&
            run.standard_error == BuildErrors(expected),
        "candidate deletion before stable open is reported only as POP-read "
        "failure");
}

void CheckSameSizeMutationIsStableOrRejected() {
  TempTree tree("same-size-mutation");
  auto pop = MakePop({"ORIGINAL"});
  const std::filesystem::path target = tree.root() / "DATA.POP";
  Check(tree.ready() && tree.Add("DATA.POP", pop.bytes),
        "same-size mutation fixture is written");
  const auto usage = MeasureUsage(pop.bytes);
  Check(usage.has_value(), "same-size mutation source fixture is accepted");
  if (!usage)
    return;

  SameSizeMutationContext context{
      .target = target,
      .replacement = std::vector<std::byte>(pop.bytes.size(), std::byte{0xA5}),
  };
  const ToolRun run =
      RunToolWithHooks(tree.root(), omega::tool::PopPostTerrainCommandTestHooks{
                                        .callback = SameSizeMutationHook,
                                        .context = &context,
                                    });
  ExpectedReport expected;
  expected.pop_candidates_discovered = 1;
  if (context.write_succeeded) {
    expected.errors[static_cast<std::size_t>(ErrorIndex::PopRead)] = 1;
    Check(context.invoked && run.exit_code != 0 &&
              run.standard_output == BuildReport(expected) &&
              run.standard_error == BuildErrors(expected),
          "same-size mutation of an open POSIX file invalidates the stable "
          "snapshot");
  } else {
    expected.descriptors_accepted = 1;
    expected.maxima = *usage;
    Check(context.invoked && run.exit_code == 0 &&
              run.standard_output == BuildReport(expected) &&
              run.standard_error.empty(),
          "open Windows POP handle denies mutation while the stable read "
          "completes");
  }
}

void CheckDirectoryReplacementIsStableOrRejected() {
  TempTree tree("directory-replacement");
  auto pop = MakePop({"GUARDED"});
  Check(tree.ready() && tree.Add("guarded/DATA.POP", pop.bytes),
        "directory replacement fixture is written");
  const auto usage = MeasureUsage(pop.bytes);
  Check(usage.has_value(), "directory replacement source fixture is accepted");
  if (!usage)
    return;

  DirectoryReplacementContext context{
      .target = tree.root() / "guarded",
      .backup = tree.root() / "guarded-original",
  };
  const ToolRun run =
      RunToolWithHooks(tree.root(), omega::tool::PopPostTerrainCommandTestHooks{
                                        .callback = DirectoryReplacementHook,
                                        .context = &context,
                                    });
  ExpectedReport expected;
  if (context.rename_succeeded) {
    expected.errors[static_cast<std::size_t>(ErrorIndex::DiscoveryIo)] = 1;
    Check(context.invoked && run.exit_code != 0 &&
              run.standard_output == BuildReport(expected) &&
              run.standard_error == BuildErrors(expected),
          "replaced POSIX directory identity is rejected immediately after "
          "iterator open");
  } else {
    expected.pop_candidates_discovered = 1;
    expected.descriptors_accepted = 1;
    expected.maxima = *usage;
    Check(
        context.invoked && run.exit_code == 0 &&
            run.standard_output == BuildReport(expected) &&
            run.standard_error.empty(),
        "open Windows directory guard prevents replacement through traversal");
  }
}

void CheckDiscoveryDepthLimit() {
  TempTree tree("depth-limit");
  std::filesystem::path current = tree.root();
  std::error_code error;
  for (std::size_t depth = 0; depth < 33 && !error; ++depth) {
    current /= "d";
    static_cast<void>(std::filesystem::create_directory(current, error));
  }
  Check(tree.ready() && !error, "depth-limit directory fixture is written");

  ExpectedReport expected;
  expected
      .errors[static_cast<std::size_t>(ErrorIndex::DiscoveryLimitExceeded)] = 1;
  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code != 0 && run.standard_output == BuildReport(expected) &&
            run.standard_error == BuildErrors(expected),
        "tree depth beyond the bounded traversal limit fails before aggregate "
        "publication");
}
} // namespace

int main() {
  CheckIndependentMaxima();
  CheckEmptyAndInvalidRoots();
  CheckTypedRejections();
  CheckAtomicityAndPrivacy();
  CheckUnsafeLinksAreRejected();
  CheckPopReadAfterCandidateDeletion();
  CheckSameSizeMutationIsStableOrRejected();
  CheckDirectoryReplacementIsStableOrRejected();
  CheckDiscoveryDepthLimit();
  if (failures == 0)
    std::cout << "pop_post_terrain_commands_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
