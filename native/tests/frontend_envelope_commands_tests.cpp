#include "frontend_envelope_commands.h"

#include "omega/retail/fnt_envelope_descriptor.h"
#include "omega/retail/ie_envelope_descriptor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
#else
#include <sys/stat.h>
#endif

namespace {
int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void WriteU16(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32U; shift += 8U)
    bytes[offset + shift / 8U] =
        static_cast<std::byte>((value >> shift) & 0xFFU);
}

struct HogMember {
  std::string name;
  std::vector<std::byte> payload;
};

std::vector<std::byte> MakeHog(const std::vector<HogMember> &members) {
  const std::size_t names_offset = 0x14U + 4U * (members.size() + 1U);
  std::size_t names_end = names_offset;
  std::size_t payload_bytes = 0;
  for (const auto &member : members) {
    names_end += member.name.size() + 1U;
    payload_bytes += member.payload.size();
  }
  const std::size_t data_offset = (names_end + 15U) & ~std::size_t{15U};
  std::vector<std::byte> bytes(data_offset, std::byte{0});
  WriteU32(bytes, 0x00U, 0x4052673DU);
  WriteU32(bytes, 0x04U, static_cast<std::uint32_t>(members.size()));
  WriteU32(bytes, 0x08U, 0x14U);
  WriteU32(bytes, 0x0CU, static_cast<std::uint32_t>(names_offset));
  WriteU32(bytes, 0x10U, static_cast<std::uint32_t>(data_offset));

  std::size_t name_cursor = names_offset;
  std::uint32_t payload_cursor = 0;
  for (std::size_t index = 0; index < members.size(); ++index) {
    WriteU32(bytes, 0x14U + 4U * index, payload_cursor);
    for (const unsigned char character : members[index].name)
      bytes[name_cursor++] = static_cast<std::byte>(character);
    ++name_cursor;
    payload_cursor += static_cast<std::uint32_t>(members[index].payload.size());
  }
  WriteU32(bytes, 0x14U + 4U * members.size(), payload_cursor);
  bytes.reserve(data_offset + payload_bytes);
  for (const auto &member : members)
    bytes.insert(bytes.end(), member.payload.begin(), member.payload.end());
  return bytes;
}

std::vector<std::byte> MakeFnt() {
  std::vector<std::byte> bytes(17U, std::byte{0});
  WriteU16(bytes, 0U, omega::retail::kFntObservedWord0x00);
  bytes[2] = static_cast<std::byte>(omega::retail::kFntObservedByte0x02);
  constexpr std::string_view text = "ABCDEFGHIJKL";
  for (std::size_t index = 0; index < text.size(); ++index)
    bytes[3U + index] =
        static_cast<std::byte>(static_cast<unsigned char>(text[index]));
  bytes[16] = std::byte{0xA5};
  return bytes;
}

std::vector<std::byte> MakeGui() {
  std::vector<std::byte> bytes{
      std::byte{'G'},  std::byte{'U'},  std::byte{'I'}, std::byte{0xA5},
      std::byte{0x34}, std::byte{0x12}, std::byte{0x5A}};
  return bytes;
}

std::vector<std::byte> MakeIe() {
  return std::vector<std::byte>{std::byte{0xA5}, std::byte{0x5A},
                                std::byte{0x78}, std::byte{0x56},
                                std::byte{0xCC}};
}

bool WriteBytes(const std::filesystem::path &path,
                const std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  if (!bytes.empty())
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

bool ReadBytes(const std::filesystem::path &path,
               std::vector<std::byte> &bytes) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return false;
  const std::streampos end = input.tellg();
  if (end < 0)
    return false;
  const auto size = static_cast<std::uint64_t>(end);
  if (size > std::numeric_limits<std::size_t>::max())
    return false;
  bytes.assign(static_cast<std::size_t>(size), std::byte{0});
  input.seekg(0, std::ios::beg);
  if (!bytes.empty())
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return input.good();
}

class TempTree final {
public:
  explicit TempTree(const std::string_view label) {
    static std::atomic<std::uint64_t> next{0};
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    root_ = std::filesystem::temp_directory_path() /
            ("omega-frontend-envelope-command-" + std::string(label) + "-" +
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

  StreamCapture(const StreamCapture &) = delete;
  StreamCapture &operator=(const StreamCapture &) = delete;

  [[nodiscard]] std::string Finish() {
    if (active_) {
      stream_.rdbuf(original_);
      active_ = false;
    }
    return buffer_.str();
  }

private:
  std::ostream &stream_;
  std::streambuf *original_ = nullptr;
  std::ostringstream buffer_;
  bool active_ = true;
};

struct ToolRun {
  int exit_code = 0;
  std::string standard_output;
  std::string standard_error;
};

ToolRun RunTool(const std::filesystem::path &root) {
  StreamCapture output(std::cout);
  StreamCapture error(std::cerr);
  const int exit_code = omega::tool::FrontendEnvelopeCoverageVerifyTree(root);
  return ToolRun{
      .exit_code = exit_code,
      .standard_output = output.Finish(),
      .standard_error = error.Finish(),
  };
}

ToolRun
RunToolWithHooks(const std::filesystem::path &root,
                 const omega::tool::FrontendEnvelopeCommandTestHooks hooks) {
  StreamCapture output(std::cout);
  StreamCapture error(std::cerr);
  const int exit_code =
      omega::tool::FrontendEnvelopeCoverageVerifyTreeForTesting(root, hooks);
  return ToolRun{
      .exit_code = exit_code,
      .standard_output = output.Finish(),
      .standard_error = error.Finish(),
  };
}

struct ExpectedFamily {
  std::uint64_t candidates = 0;
  std::uint64_t accepted = 0;
  std::array<std::uint64_t, 7> rejected{};
};

void AppendFamily(std::ostringstream &stream, const std::string_view name,
                  const ExpectedFamily &family) {
  stream << '"' << name << "\":{\"candidates\":" << family.candidates
         << ",\"accepted\":" << family.accepted
         << ",\"rejected_truncated\":" << family.rejected[0]
         << ",\"rejected_malformed\":" << family.rejected[1]
         << ",\"rejected_overflow\":" << family.rejected[2]
         << ",\"rejected_limit_exceeded\":" << family.rejected[3]
         << ",\"rejected_unsupported_variant\":" << family.rejected[4]
         << ",\"rejected_invalid_reference\":" << family.rejected[5]
         << ",\"rejected_duplicate_reference\":" << family.rejected[6] << '}';
}

std::string BuildReport(const ExpectedFamily &fnt = {},
                        const ExpectedFamily &gui = {},
                        const ExpectedFamily &ie = {}) {
  std::ostringstream stream;
  stream << "{\"schema_version\":1,";
  AppendFamily(stream, "fnt", fnt);
  stream << ',';
  AppendFamily(stream, "gui", gui);
  stream << ',';
  AppendFamily(stream, "ie", ie);
  stream << "}\n";
  return stream.str();
}

std::vector<std::byte> MakeAcceptedFrontendHog() {
  return MakeHog({
      HogMember{.name = "VALID.FNT", .payload = MakeFnt()},
      HogMember{.name = "VALID.GUI", .payload = MakeGui()},
      HogMember{.name = "VALID.IE", .payload = MakeIe()},
  });
}

std::uint8_t HexNibble(const char value) {
  if (value >= '0' && value <= '9')
    return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<std::uint8_t>(value - 'a' + 10);
  return static_cast<std::uint8_t>(value - 'A' + 10);
}

omega::tool::FrontendEnvelopeSha256Digest
DigestFromHex(const std::string_view hex) {
  omega::tool::FrontendEnvelopeSha256Digest digest{};
  for (std::size_t index = 0; index < digest.size(); ++index) {
    digest[index] = static_cast<std::byte>((HexNibble(hex[index * 2U]) << 4U) |
                                           HexNibble(hex[index * 2U + 1U]));
  }
  return digest;
}

void CheckSha256KnownAnswers() {
  const std::string_view empty;
  const std::string_view abc = "abc";
  const std::string_view padding_boundary =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  const auto bytes = [](const std::string_view text) {
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
  };

  Check(omega::tool::FrontendEnvelopeSha256ForTesting(bytes(empty)) ==
            DigestFromHex("e3b0c44298fc1c149afbf4c8996fb924"
                          "27ae41e4649b934ca495991b7852b855"),
        "SHA-256 matches the empty-input known answer");
  Check(omega::tool::FrontendEnvelopeSha256ForTesting(bytes(abc)) ==
            DigestFromHex("ba7816bf8f01cfea414140de5dae2223"
                          "b00361a396177a9cb410ff61f20015ad"),
        "SHA-256 matches the abc known answer");
  Check(
      omega::tool::FrontendEnvelopeSha256ForTesting(bytes(padding_boundary)) ==
          DigestFromHex("248d6a61d20638b8e5c026930c3e6039"
                        "a33ce45964ff2167f6ecedd419db06c1"),
      "SHA-256 matches the padding-boundary known answer");
}

void CheckAuthenticatedChunkBoundaries() {
  TempTree tree("authenticated-chunk-boundary");
  constexpr std::size_t padding_size = 65'432U;
  const auto hog = MakeHog({
      HogMember{.name = "PADDING.BIN",
                .payload =
                    std::vector<std::byte>(padding_size, std::byte{0x42})},
      HogMember{.name = "VALID.FNT", .payload = MakeFnt()},
      HogMember{.name = "VALID.GUI", .payload = MakeGui()},
      HogMember{.name = "VALID.IE", .payload = MakeIe()},
  });
  Check(hog.size() > 64U * 1024U && tree.ready() &&
            tree.Add("FRONTEND.HOG", hog),
        "multi-chunk HOG with a boundary-spanning candidate is written");
  if (!tree.ready())
    return;

  const ExpectedFamily accepted{.candidates = 1, .accepted = 1};
  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code == 0 &&
            run.standard_output == BuildReport(accepted, accepted, accepted) &&
            run.standard_error.empty(),
        "authenticated reads copy exact overlaps across the final short chunk");
}

ExpectedFamily OneAcceptedFamily() {
  return ExpectedFamily{.candidates = 1, .accepted = 1};
}

void CheckNestedCoverageAndTypedRejections() {
  TempTree tree("typed");
  auto malformed_fnt = MakeFnt();
  malformed_fnt[3] = std::byte{0};
  auto wrong_gui = MakeGui();
  wrong_gui[0] = std::byte{'X'};
  std::vector<std::byte> oversized_ie(
      static_cast<std::size_t>(omega::retail::kIeMaximumInputBytes + 1U),
      std::byte{0});
  auto nested = MakeHog({
      HogMember{.name = "SHORT.FnT", .payload = {std::byte{3}}},
      HogMember{.name = "VALID.gUi", .payload = MakeGui()},
      HogMember{.name = "WRONG.GUI", .payload = std::move(wrong_gui)},
      HogMember{.name = "VALID.iE", .payload = MakeIe()},
  });
  nested.resize(nested.size() + 16U, std::byte{0});
  const auto top = MakeHog({
      HogMember{.name = "VALID.FNT", .payload = MakeFnt()},
      HogMember{.name = "MALFORMED.fnt", .payload = std::move(malformed_fnt)},
      HogMember{.name = "TOO_BIG.IE", .payload = std::move(oversized_ie)},
      HogMember{.name = "NESTED.HoG", .payload = nested},
      HogMember{.name = "IGNORED.BIN", .payload = {std::byte{0x42}}},
  });
  Check(tree.ready() && tree.Add("archives/FRONTEND.HOG", top) &&
            tree.Add("LOOSE.GUI", MakeGui()),
        "generated nested HOG and ignored loose asset are written");
  if (!tree.ready())
    return;

  ExpectedFamily fnt;
  fnt.candidates = 3;
  fnt.accepted = 1;
  fnt.rejected[0] = 1;
  fnt.rejected[1] = 1;
  ExpectedFamily gui;
  gui.candidates = 2;
  gui.accepted = 1;
  gui.rejected[4] = 1;
  ExpectedFamily ie;
  ie.candidates = 2;
  ie.accepted = 1;
  ie.rejected[3] = 1;

  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code == 2 &&
            run.standard_output == BuildReport(fnt, gui, ie) &&
            run.standard_error ==
                "frontend-envelope-coverage: descriptor_rejections\n",
        "nested HOG coverage emits only exact per-family typed counters");
}

void CheckDeterminismAndPrivacy() {
  constexpr std::string_view first_marker = "PRIVATE_FIRST_IDENTITY";
  constexpr std::string_view second_marker = "PRIVATE_SECOND_IDENTITY";
  TempTree first(first_marker);
  TempTree second(second_marker);
  const auto first_hog = MakeHog({
      HogMember{.name = std::string(first_marker) + ".FNT",
                .payload = MakeFnt()},
      HogMember{.name = std::string(first_marker) + ".GUI",
                .payload = MakeGui()},
      HogMember{.name = std::string(first_marker) + ".IE", .payload = MakeIe()},
  });
  const auto second_hog = MakeHog({
      HogMember{.name = std::string(second_marker) + ".FNT",
                .payload = MakeFnt()},
      HogMember{.name = std::string(second_marker) + ".GUI",
                .payload = MakeGui()},
      HogMember{.name = std::string(second_marker) + ".IE",
                .payload = MakeIe()},
  });
  Check(first.ready() && second.ready() &&
            first.Add(std::string(first_marker) + ".HOG", first_hog) &&
            second.Add(std::string(second_marker) + ".HOG", second_hog),
        "privacy-equivalent generated HOG trees are written");
  if (!first.ready() || !second.ready())
    return;

  const ToolRun first_run = RunTool(first.root());
  const ToolRun second_run = RunTool(second.root());
  ExpectedFamily accepted;
  accepted.candidates = 1;
  accepted.accepted = 1;
  const std::string expected = BuildReport(accepted, accepted, accepted);
  Check(first_run.exit_code == 0 && second_run.exit_code == 0 &&
            first_run.standard_output == expected &&
            second_run.standard_output == expected &&
            first_run.standard_error.empty() &&
            second_run.standard_error.empty(),
        "identity-distinct trees produce one deterministic accepted report");

  const std::string combined =
      first_run.standard_output + first_run.standard_error +
      second_run.standard_output + second_run.standard_error;
  Check(combined.find(first_marker) == std::string::npos &&
            combined.find(second_marker) == std::string::npos,
        "reports and diagnostics contain no root, archive, or member identity");
}

void CheckAtomicInfrastructureFailures() {
  TempTree empty("empty");
  Check(empty.ready(), "empty generated root is created");
  if (!empty.ready())
    return;

  const ToolRun missing = RunTool(empty.root() / "missing");
  Check(missing.exit_code == 1 && missing.standard_output == BuildReport() &&
            missing.standard_error ==
                "frontend-envelope-coverage: discovery_invalid_root\n",
        "invalid root emits an atomic zero report and fixed diagnostic");

  const ToolRun no_candidates = RunTool(empty.root());
  Check(no_candidates.exit_code == 2 &&
            no_candidates.standard_output == BuildReport() &&
            no_candidates.standard_error ==
                "frontend-envelope-coverage: missing_family_candidates\n",
        "empty coverage fails without inventing family observations");

  constexpr std::string_view private_marker = "PRIVATE_BROKEN_ARCHIVE";
  Check(empty.Add(std::string(private_marker) + ".HOG",
                  std::vector<std::byte>{std::byte{0x42}}),
        "malformed generated HOG is written");
  const ToolRun malformed = RunTool(empty.root());
  Check(malformed.exit_code == 1 &&
            malformed.standard_output == BuildReport() &&
            malformed.standard_error ==
                "frontend-envelope-coverage: hog_open\n" &&
            (malformed.standard_output + malformed.standard_error)
                    .find(private_marker) == std::string::npos,
        "HOG failure discards partial observations and redacts identity");
}

void CheckNestedDepthLimit() {
  TempTree tree("depth");
  std::vector<std::byte> nested = MakeHog({
      HogMember{.name = "VALID.FNT", .payload = MakeFnt()},
      HogMember{.name = "VALID.GUI", .payload = MakeGui()},
      HogMember{.name = "VALID.IE", .payload = MakeIe()},
  });
  for (std::size_t depth = 0; depth < 33U; ++depth) {
    nested = MakeHog({
        HogMember{.name = "NEXT.HOG", .payload = std::move(nested)},
    });
  }
  Check(tree.ready() && tree.Add("TOO_DEEP.HOG", nested),
        "over-depth generated nested HOG chain is written");
  if (!tree.ready())
    return;

  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code == 1 && run.standard_output == BuildReport() &&
            run.standard_error ==
                "frontend-envelope-coverage: scan_limit_exceeded\n",
        "nested-HOG depth ceiling fails atomically with fixed output");
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
  Check(tree.ready() &&
            tree.Add("target/FRONTEND.HOG", MakeAcceptedFrontendHog()),
        "unsafe-link target HOG is written");
  if (!tree.ready())
    return;

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
      Check(explicit_skip, "unsafe-link fixture fails only for a recognized "
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

  const ToolRun run = RunTool(tree.root());
  Check(run.exit_code == 1 && run.standard_output == BuildReport() &&
            run.standard_error ==
                "frontend-envelope-coverage: discovery_unsafe_entry\n",
        "full-tree discovery rejects links with atomic path-free output");
}

struct DeleteBeforeOpenContext {
  std::filesystem::path target;
  bool invoked = false;
  bool removed = false;
};

void DeleteBeforeOpenHook(
    const omega::tool::FrontendEnvelopeCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<DeleteBeforeOpenContext *>(opaque);
  if (event !=
          omega::tool::FrontendEnvelopeCommandTestEvent::BeforeHogFileOpen ||
      path != context.target || context.invoked)
    return;
  context.invoked = true;
  std::error_code error;
  context.removed = std::filesystem::remove(path, error) && !error;
}

struct FileReplacementContext {
  std::filesystem::path target;
  std::filesystem::path backup;
  std::filesystem::path replacement;
  bool invoked = false;
  bool replaced = false;
};

void FileReplacementHook(
    const omega::tool::FrontendEnvelopeCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<FileReplacementContext *>(opaque);
  if (event !=
          omega::tool::FrontendEnvelopeCommandTestEvent::BeforeHogFileOpen ||
      path != context.target || context.invoked)
    return;
  context.invoked = true;
  std::error_code error;
  std::filesystem::rename(context.target, context.backup, error);
  if (error)
    return;
  std::filesystem::rename(context.replacement, context.target, error);
  context.replaced = !error;
}

struct RawChunkMutationContext {
  std::filesystem::path target;
  std::size_t mutate_on_read = 0;
  std::size_t chunk_zero_reads = 0;
  bool mutated = false;
};

void MutateAuthenticatedScratch(const std::filesystem::path &path,
                                const std::uint64_t chunk_offset,
                                const std::span<std::byte> chunk,
                                void *opaque) {
  auto &context = *static_cast<RawChunkMutationContext *>(opaque);
  if (path != context.target || chunk_offset != 0U)
    return;
  ++context.chunk_zero_reads;
  if (context.chunk_zero_reads != context.mutate_on_read ||
      chunk.size() <= 0x24U)
    return;
  chunk[0x24U] = std::byte{'v'};
  context.mutated = true;
}

void CheckTransientChunkMutationIsRejected(const std::size_t mutate_on_read,
                                           const std::string_view label) {
  TempTree tree(label);
  const auto original = MakeAcceptedFrontendHog();
  const std::filesystem::path target = tree.root() / "FRONTEND.HOG";
  Check(tree.ready() && tree.Add("FRONTEND.HOG", original),
        "transient chunk-mutation fixture is written");
  if (!tree.ready())
    return;

  std::error_code error;
  const auto original_time = std::filesystem::last_write_time(target, error);
  const auto original_size = std::filesystem::file_size(target, error);
  Check(!error, "transient chunk-mutation metadata is captured");
  if (error)
    return;

  RawChunkMutationContext context{
      .target = target,
      .mutate_on_read = mutate_on_read,
  };
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .after_raw_hog_chunk_read = MutateAuthenticatedScratch,
                       .context = &context,
                   });

  std::vector<std::byte> after;
  error.clear();
  const auto after_time = std::filesystem::last_write_time(target, error);
  const auto after_size = std::filesystem::file_size(target, error);
  const bool read_back = ReadBytes(target, after);
  Check(context.mutated && context.chunk_zero_reads == mutate_on_read &&
            run.exit_code == 1 && run.standard_output == BuildReport() &&
            run.standard_error == "frontend-envelope-coverage: member_read\n",
        "transient authenticated-chunk mutation fails atomically");
  Check(!error && read_back && after == original &&
            after_size == original_size && after_time == original_time,
        "transient chunk mutation leaves file bytes and metadata unchanged");
}

struct RawChunkCountContext {
  std::filesystem::path target;
  std::size_t reads = 0;
};

void CountAuthenticatedChunkReads(const std::filesystem::path &path,
                                  const std::uint64_t,
                                  const std::span<std::byte>, void *opaque) {
  auto &context = *static_cast<RawChunkCountContext *>(opaque);
  if (path == context.target)
    ++context.reads;
}

void CheckPaddedNestedAuthenticationAllowance() {
  TempTree tree("padded-nested-authentication-allowance");
  auto nested = MakeAcceptedFrontendHog();
  nested.resize(nested.size() + 2U * 64U * 1024U, std::byte{0});
  const auto outer = MakeHog({
      HogMember{.name = "NESTED.HOG", .payload = std::move(nested)},
  });
  const std::filesystem::path target = tree.root() / "FRONTEND.HOG";
  Check(tree.ready() && tree.Add("FRONTEND.HOG", outer),
        "multi-chunk padded nested-HOG fixture is written");
  if (!tree.ready())
    return;

  RawChunkCountContext context{.target = target};
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .after_raw_hog_chunk_read = CountAuthenticatedChunkReads,
                       .context = &context,
                   });
  const ExpectedFamily accepted{.candidates = 1, .accepted = 1};
  const std::size_t physical_chunks =
      (outer.size() + 64U * 1024U - 1U) / (64U * 1024U);
  Check(run.exit_code == 0 &&
            run.standard_output == BuildReport(accepted, accepted, accepted) &&
            run.standard_error.empty(),
        "chunk-rounded nested allowance accepts padded nested HOG coverage");
  Check(context.reads > physical_chunks,
        "padded nested scan authenticates a bounded post-padding backtrack");
}

void CheckAuthenticatedChunkCacheBoundsTinyMemberWork() {
  TempTree tree("authenticated-chunk-cache");
  constexpr std::size_t member_count = 8'192U;
  std::vector<HogMember> members;
  members.reserve(member_count);
  for (std::size_t index = 0; index < member_count; ++index) {
    members.push_back(
        HogMember{.name = "TINY.FNT", .payload = {std::byte{0x03}}});
  }
  members.push_back(HogMember{.name = "VALID.GUI", .payload = MakeGui()});
  members.push_back(HogMember{.name = "VALID.IE", .payload = MakeIe()});
  const auto hog = MakeHog(members);
  const std::filesystem::path target = tree.root() / "FRONTEND.HOG";
  Check(tree.ready() && tree.Add("FRONTEND.HOG", hog),
        "many-tiny-member cache fixture is written");
  if (!tree.ready())
    return;

  RawChunkCountContext context{.target = target};
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .after_raw_hog_chunk_read = CountAuthenticatedChunkReads,
                       .context = &context,
                   });
  ExpectedFamily fnt{.candidates = member_count};
  fnt.rejected[0] = member_count;
  const ExpectedFamily accepted{.candidates = 1, .accepted = 1};
  const std::size_t physical_chunks =
      (hog.size() + 64U * 1024U - 1U) / (64U * 1024U);
  const bool report_matches =
      run.exit_code == 2 &&
      run.standard_output == BuildReport(fnt, accepted, accepted) &&
      run.standard_error ==
          "frontend-envelope-coverage: descriptor_rejections\n";
  if (!report_matches) {
    std::cerr << "many-tiny-member run: exit=" << run.exit_code
              << ", stdout=" << run.standard_output
              << ", stderr=" << run.standard_error;
  }
  Check(report_matches,
        "many-tiny-member scan preserves the bounded aggregate report");
  if (context.reads != physical_chunks) {
    std::cerr << "authenticated chunk reads: expected " << physical_chunks
              << ", observed " << context.reads << '\n';
  }
  Check(
      context.reads == physical_chunks,
      "verified chunk cache makes tiny-member work proportional to HOG bytes");
}

void CheckAuthenticatedReadBudgetIsEnforced() {
  TempTree tree("authenticated-read-budget");
  Check(tree.ready() && tree.Add("FRONTEND.HOG", MakeAcceptedFrontendHog()),
        "authenticated-read budget fixture is written");
  if (!tree.ready())
    return;

  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .maximum_authenticated_hog_read_bytes = 1U,
                   });
  Check(run.exit_code == 1 && run.standard_output == BuildReport() &&
            run.standard_error ==
                "frontend-envelope-coverage: scan_limit_exceeded\n",
        "physical authentication work stops at the per-HOG byte budget");
}

void CheckDiscoveryDigestBudgetIsEnforced() {
  TempTree tree("discovery-digest-budget");
  Check(tree.ready() && tree.Add("FRONTEND.HOG", MakeAcceptedFrontendHog()),
        "discovery-digest budget fixture is written");
  if (!tree.ready())
    return;

  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .maximum_discovery_chunk_digests = 0U,
                   });
  Check(run.exit_code == 1 && run.standard_output == BuildReport() &&
            run.standard_error ==
                "frontend-envelope-coverage: scan_limit_exceeded\n",
        "retained discovery digests stop at the global count budget");
}

struct SameSizeMutationContext {
  std::filesystem::path target;
  std::vector<std::byte> replacement;
  bool invoked = false;
  bool write_succeeded = false;
};

void SameSizeMutationHook(
    const omega::tool::FrontendEnvelopeCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<SameSizeMutationContext *>(opaque);
  if (event != omega::tool::FrontendEnvelopeCommandTestEvent::HogFileOpened ||
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
    const omega::tool::FrontendEnvelopeCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<DirectoryReplacementContext *>(opaque);
  if (event != omega::tool::FrontendEnvelopeCommandTestEvent::
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

#ifndef _WIN32
struct FifoReplacementContext {
  std::filesystem::path target;
  std::filesystem::path backup;
  bool invoked = false;
  bool replaced = false;
};

void FifoReplacementHook(
    const omega::tool::FrontendEnvelopeCommandTestEvent event,
    const std::filesystem::path &path, void *opaque) {
  auto &context = *static_cast<FifoReplacementContext *>(opaque);
  if (event !=
          omega::tool::FrontendEnvelopeCommandTestEvent::BeforeHogFileOpen ||
      path != context.target || context.invoked)
    return;
  context.invoked = true;
  std::error_code error;
  std::filesystem::rename(context.target, context.backup, error);
  if (error)
    return;
  context.replaced = ::mkfifo(context.target.c_str(), S_IRUSR | S_IWUSR) == 0;
}
#endif

void CheckHogDeletionBeforeStableOpen() {
  TempTree tree("delete-before-open");
  const std::filesystem::path target = tree.root() / "FRONTEND.HOG";
  Check(tree.ready() && tree.Add("FRONTEND.HOG", MakeAcceptedFrontendHog()),
        "HOG deletion fixture is written");
  if (!tree.ready())
    return;

  DeleteBeforeOpenContext context{.target = target};
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .callback = DeleteBeforeOpenHook,
                       .context = &context,
                   });
  Check(context.invoked && context.removed && run.exit_code == 1 &&
            run.standard_output == BuildReport() &&
            run.standard_error == "frontend-envelope-coverage: member_read\n",
        "candidate deletion before stable open fails atomically");
}

void CheckSameSizeTimeFileReplacementIsRejected() {
  TempTree tree("identity-replacement");
  const auto bytes = MakeAcceptedFrontendHog();
  const std::filesystem::path target = tree.root() / "FRONTEND.HOG";
  const std::filesystem::path replacement = tree.root() / "replacement.bin";
  Check(tree.ready() && tree.Add("FRONTEND.HOG", bytes) &&
            tree.Add("replacement.bin", bytes),
        "same-size file replacement fixture is written");
  if (!tree.ready())
    return;

  std::error_code error;
  const auto timestamp = std::filesystem::last_write_time(target, error);
  if (!error)
    std::filesystem::last_write_time(replacement, timestamp, error);
  Check(!error, "replacement timestamp matches the discovered candidate");
  if (error)
    return;

  FileReplacementContext context{
      .target = target,
      .backup = tree.root() / "original.bin",
      .replacement = replacement,
  };
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .callback = FileReplacementHook,
                       .context = &context,
                   });
  Check(context.invoked && context.replaced && run.exit_code == 1 &&
            run.standard_output == BuildReport() &&
            run.standard_error == "frontend-envelope-coverage: member_read\n",
        "same-byte, same-size, same-time replacement is rejected by identity");
}

void CheckSameSizeMutationIsStableOrRejected() {
  TempTree tree("same-size-mutation");
  const auto bytes = MakeAcceptedFrontendHog();
  auto replacement = bytes;
  // Preserve a valid archive and the case-insensitive classification result so
  // only the content-stability check can distinguish this same-size rewrite.
  replacement[0x24U] = std::byte{'v'};
  const std::filesystem::path target = tree.root() / "FRONTEND.HOG";
  Check(tree.ready() && tree.Add("FRONTEND.HOG", bytes),
        "same-size mutation fixture is written");
  if (!tree.ready())
    return;

  SameSizeMutationContext context{
      .target = target,
      .replacement = std::move(replacement),
  };
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .callback = SameSizeMutationHook,
                       .context = &context,
                   });
  if (context.write_succeeded) {
    Check(context.invoked && run.exit_code == 1 &&
              run.standard_output == BuildReport() &&
              run.standard_error == "frontend-envelope-coverage: member_read\n",
          "persistent same-size mutation is rejected before observations "
          "publish");
  } else {
    const ExpectedFamily accepted = OneAcceptedFamily();
    Check(context.invoked && run.exit_code == 0 &&
              run.standard_output ==
                  BuildReport(accepted, accepted, accepted) &&
              run.standard_error.empty(),
          "open Windows HOG handle denies mutation until stable reads finish");
  }
}

void CheckDirectoryReplacementIsStableOrRejected() {
  TempTree tree("directory-replacement");
  Check(tree.ready() &&
            tree.Add("guarded/FRONTEND.HOG", MakeAcceptedFrontendHog()),
        "directory replacement fixture is written");
  if (!tree.ready())
    return;

  DirectoryReplacementContext context{
      .target = tree.root() / "guarded",
      .backup = tree.root() / "guarded-original",
  };
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .callback = DirectoryReplacementHook,
                       .context = &context,
                   });
  if (context.rename_succeeded) {
    Check(context.invoked && run.exit_code == 1 &&
              run.standard_output == BuildReport() &&
              run.standard_error == "frontend-envelope-coverage: scan_io\n",
          "replaced POSIX directory identity is rejected after iterator open");
  } else {
    const ExpectedFamily accepted = OneAcceptedFamily();
    Check(context.invoked && run.exit_code == 0 &&
              run.standard_output ==
                  BuildReport(accepted, accepted, accepted) &&
              run.standard_error.empty(),
          "open Windows directory guard prevents traversal replacement");
  }
}

#ifndef _WIN32
void CheckFifoReplacementCannotBlockStableOpen() {
  TempTree tree("fifo-replacement");
  const std::filesystem::path target = tree.root() / "FRONTEND.HOG";
  Check(tree.ready() && tree.Add("FRONTEND.HOG", MakeAcceptedFrontendHog()),
        "FIFO replacement fixture is written");
  if (!tree.ready())
    return;

  FifoReplacementContext context{
      .target = target,
      .backup = tree.root() / "original.bin",
  };
  const ToolRun run = RunToolWithHooks(
      tree.root(), omega::tool::FrontendEnvelopeCommandTestHooks{
                       .callback = FifoReplacementHook,
                       .context = &context,
                   });
  Check(context.invoked && context.replaced && run.exit_code == 1 &&
            run.standard_output == BuildReport() &&
            run.standard_error ==
                "frontend-envelope-coverage: discovery_unsafe_entry\n",
        "nonblocking stable open rejects a substituted FIFO atomically");
}
#endif
} // namespace

int main() {
  CheckSha256KnownAnswers();
  CheckNestedCoverageAndTypedRejections();
  CheckAuthenticatedChunkBoundaries();
  CheckDeterminismAndPrivacy();
  CheckAtomicInfrastructureFailures();
  CheckNestedDepthLimit();
  CheckUnsafeLinksAreRejected();
  CheckHogDeletionBeforeStableOpen();
  CheckSameSizeTimeFileReplacementIsRejected();
  CheckTransientChunkMutationIsRejected(1U, "first-chunk-read-mutation");
  CheckPaddedNestedAuthenticationAllowance();
  CheckAuthenticatedChunkCacheBoundsTinyMemberWork();
  CheckAuthenticatedReadBudgetIsEnforced();
  CheckDiscoveryDigestBudgetIsEnforced();
  CheckSameSizeMutationIsStableOrRejected();
  CheckDirectoryReplacementIsStableOrRejected();
#ifndef _WIN32
  CheckFifoReplacementCannotBlockStableOpen();
#endif

  if (failures != 0) {
    std::cerr << failures << " frontend envelope command test(s) failed\n";
    return 1;
  }
  std::cout << "frontend envelope command tests passed\n";
  return 0;
}
