#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/level_ir.h"
#include "omega/asset/texture_storage_ir.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace omega::content
{
class GameDataService;

namespace detail
{
// Opaque process-local identity. Handles observe this token weakly and never retain a store,
// locator, service implementation, or retail byte view.
struct LevelTextureStoreIdentity;
} // namespace detail

struct LevelTextureStoreConfig
{
    // Internal per-operation policy, not a user-facing configuration surface. The native aggregate
    // verifier established independent maxima (they are not one co-occurring operation): Open used
    // input 3,076,944, 1,460 items, output 111,014, depth zero, and scratch 71,467; Load used input
    // 3,139,344, 5,169 items, output 333,232, depth zero, and scratch 65,595. Each byte/item field is
    // rounded separately to the next binary boundary above its larger maximum. Strings retain the
    // common 4 KiB safety cap. One container edge is the smallest nonzero headroom above measured
    // depth zero and preserves bounded nested-source support without inheriting the generic
    // nine-edge semantic-tree default.
    asset::DecodeLimits limits{
        .maximum_input_bytes = 4ULL * 1024ULL * 1024ULL,
        .maximum_output_bytes = 512ULL * 1024ULL,
        .maximum_scratch_bytes = 128ULL * 1024ULL,
        .maximum_items = 1ULL << 13U,
        .maximum_string_bytes = 4096,
        .maximum_nesting_depth = 1,
    };
};

struct LevelTextureOperationUsage
{
    // Cumulative bytes presented to container readers and the semantic decoder. Open counts each
    // resolved texture-source container once; Load counts each ancestor container and the terminal
    // TDX once.
    std::uint64_t input_bytes = 0;
    // Traversed source-ancestor and terminal-directory entries plus unique stored locators for
    // Open; traversed archive entries plus exact measured TDX semantic items for Load.
    std::uint64_t items = 0;
    // Logical owned output only. Open accounts the immutable store/locator values; Load accounts
    // the canonical TextureStorageIR and its owned storage.
    std::uint64_t logical_output_bytes = 0;
    // Container edges only. The terminal TDX leaf is not a nesting edge.
    std::uint32_t archive_depth = 0;
    // Deterministic logical peak of temporary inventory storage. Open preflights the resident
    // canonical-source workspace (outer vector, SourceLocator/string objects, and validated
    // normalized-character upper bounds) before allocating it. For each parsed directory entry it
    // then adds one map value, four link pointers, and the validated source-name bytes. Directories
    // are processed sequentially, but the canonical sources remain resident. Open reports the
    // larger of source-list construction, canonical sources plus resolver scratch, and canonical
    // sources plus the largest terminal-directory workspace; Load reports resolver scratch. It
    // excludes allocator metadata, spare capacity, parser storage, and resident input buffers.
    // Semantic-decoder scratch is enforced independently.
    std::uint64_t peak_scratch_bytes = 0;
};

enum class LevelTextureStoreErrorCode
{
    InvalidConfiguration,
    LimitExceeded,
    InvalidReference,
    DuplicateReference,
    ReadFailed,
    MalformedArchive,
    DecodeFailed,
    ForeignService,
    InvalidHandle,
    // Documented not-found result of the lexical name-candidate policy below. It reports only
    // that the selected policy admitted no candidate; it is not a claim that no retail texture
    // corresponds to the queried name.
    NameNotFound,
};

[[nodiscard]] std::string_view LevelTextureStoreErrorCodeName(
    LevelTextureStoreErrorCode code) noexcept;

struct LevelTextureStoreError
{
    LevelTextureStoreErrorCode code = LevelTextureStoreErrorCode::InvalidConfiguration;
    // Diagnostics identify only the failing stage/category. They never expose a filesystem path,
    // archive/member name, locator, hash, offset, or payload detail.
    std::string message;
    std::optional<asset::DecodeError> decode_error;
};

class LevelTextureStore;

// Weak, store-scoped identity. Default, expired, out-of-range, and cross-store handles are all
// representable and are rejected by LevelTextureStore::Load before source I/O.
class LevelTextureHandle final
{
public:
    LevelTextureHandle() noexcept = default;

    // [any thread; thread-safe] Reports token lifetime only; Load remains the authority for store
    // membership and index validation.
    [[nodiscard]] bool expired() const noexcept { return store_identity_.expired(); }

private:
    friend class LevelTextureStore;

    LevelTextureHandle(
        std::weak_ptr<const detail::LevelTextureStoreIdentity> store_identity,
        std::size_t index) noexcept
        : store_identity_(std::move(store_identity)), index_(index)
    {
    }

    std::weak_ptr<const detail::LevelTextureStoreIdentity> store_identity_;
    std::size_t index_ = 0;
};

struct LoadedLevelTexture
{
    asset::TextureStorageIR storage;
    LevelTextureOperationUsage usage;
};

// PROJECT ENGINEERING POLICY. This selects which purely lexical candidate rule the store applies
// when a caller asks it to resolve a name to a handle. Both alternatives are project choices. This
// is NOT a decoded retail fact: nothing here claims that retail code performs a name lookup, that a
// catalog/material name is a texture name, that a material record binds a texture, that either
// texture-container class has priority, or that a matched member is the texture retail would have
// used. The policy compares strings; it decides nothing about a cell, mesh, draw, placement,
// visibility state, or renderer resource.
//
// PROVENANCE OF THE MEASUREMENT, not of a retail rule. The project measured the extension-eliding
// alternative offline over the extracted disc: 34,267 of 34,267 VUM material-catalog name
// occurrences resolved to exactly one direct .TDX member, with zero unmatched and zero ambiguous
// names, reproducibly and byte-identically across two confirmed passes. The measurement, its
// counts, and its explicit statement that it establishes no retail alias rule live in
// analysis/formats/VUM.md ("Exact-first one-terminal-extension candidate family", including the
// nonclaim paragraph) and analysis/formats/TDX.md. That is offline lexical coherence and nothing
// more; retail name lookup, retail material-record consumption, and retail extension elision all
// remain unobserved.
enum class LevelTextureNameCandidatePolicy : std::uint8_t
{
    // Full normalized equality against the stored terminal member name. No transformation of
    // either side.
    NormalizedExactTerminalMember,
    // Full normalized equality first; only when that admits no candidate, at most one syntactic
    // extension is removed independently from the final component of BOTH the queried name and
    // each stored terminal member name, and one further exact comparison is made. A syntactic
    // extension requires a dot after the first character of the final component and at least one
    // character after that dot, so ".HIDDEN", "NAME." and an extensionless component are left
    // unchanged while "A.B.C" becomes "A.B". Directory components are never removed and a second
    // extension is never stripped. The elision is a named, caller-selected part of this policy --
    // never a hidden fixup applied on the caller's behalf.
    NormalizedExactThenOneTerminalExtensionElision,
};

// Non-hot-reloadable, immutable level-scoped locator store. It weakly binds to one
// GameDataService source identity and owns neither the service nor source bytes. Open inventories
// only direct TDX members of the manifest's explicit texture sources; it does not traverse DATA
// cells or nested containers. It provides no cache, async scheduling, renderer/GPU upload, retail
// material/name binding, display-pixel expansion, palette/channel/swizzle policy, placement,
// visibility, or draw semantics. Name-keyed access exists only as an explicitly caller-selected
// lexical candidate policy (see LevelTextureNameCandidatePolicy); it reads no material record or
// catalog and asserts no retail name rule.
class LevelTextureStore final
{
public:
    // [game thread; no concurrent move/destruction] Requires at least one valid explicit texture
    // source, inventories each source's direct canonical TDX members only, rejects ambiguous
    // normalized directories before extension filtering, exact-deduplicates repeated locators, and
    // assigns handles in canonical lexical order. Source order does not affect handle order.
    [[nodiscard]] static std::expected<LevelTextureStore, LevelTextureStoreError> Open(
        const GameDataService& game_data, const asset::LevelManifestIR& manifest,
        LevelTextureStoreConfig config = {});

    // [game thread, after all concurrent readers have joined]
    ~LevelTextureStore();
    // [game thread, with no concurrent readers] Existing handles retain the moved identity and
    // become valid against the destination store.
    LevelTextureStore(LevelTextureStore&&) noexcept;
    LevelTextureStore& operator=(LevelTextureStore&&) = delete;
    LevelTextureStore(const LevelTextureStore&) = delete;
    LevelTextureStore& operator=(const LevelTextureStore&) = delete;

    // [any thread after Open(); immutable]
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::expected<LevelTextureHandle, LevelTextureStoreError> HandleAt(
        std::size_t index) const noexcept;
    [[nodiscard]] const LevelTextureOperationUsage& open_usage() const noexcept;

    // [any thread after Open(); immutable] Returns the normalized terminal container-member name
    // this handle index was inventoried under. It is a container member name, not a claimed retail
    // texture name, and it binds no material record. The view stays valid until the store that owns
    // it is destroyed. Out-of-range and moved-from stores yield InvalidHandle, matching HandleAt.
    [[nodiscard]] std::expected<std::string_view, LevelTextureStoreError> TerminalMemberNameAt(
        std::size_t index) const noexcept;

    // [any thread after Open(); immutable] PROJECT ENGINEERING POLICY, not a decoded retail
    // binding. Resolves a name to a handle purely lexically under the caller-selected candidate
    // policy, which has no default and must therefore be spelled at every call site. It compares
    // only against the normalized terminal member names reported by TerminalMemberNameAt; it reads
    // no material record, catalog, or manifest, and it claims no retail alias rule -- see
    // LevelTextureNameCandidatePolicy for the measurement this policy was drawn from and for what
    // that measurement explicitly does not establish. No I/O occurs.
    //
    // Returns InvalidReference when the name cannot be normalized, LimitExceeded when it exceeds
    // this store's string limit, NameNotFound when the policy admits no candidate,
    // DuplicateReference when it admits more than one, InvalidConfiguration for an unrecognized
    // policy value, and InvalidHandle for a moved-from store. Diagnostics never repeat the queried
    // name or any member name.
    [[nodiscard]] std::expected<LevelTextureHandle, LevelTextureStoreError>
    FindHandleByNameCandidate(
        std::string_view texture_name, LevelTextureNameCandidatePolicy policy) const;

    // [any worker thread after Open(); reentrant and thread-safe] Validates the service binding,
    // weak store identity, and handle index before source I/O, then returns independently owned
    // canonical storage with exact composed usage under this store's explicit limits.
    [[nodiscard]] std::expected<LoadedLevelTexture, LevelTextureStoreError> Load(
        const GameDataService& game_data, const LevelTextureHandle& handle) const;

    // [any worker thread after Open(); reentrant and thread-safe] Returns the raw,
    // undecoded terminal TDX member bytes for a handle -- the same bytes Load
    // decodes, before storage decode -- for callers that must run a separate,
    // independently validated display-pixel expansion the store deliberately
    // omits. Same handle/service validation and resolve budget as Load.
    [[nodiscard]] std::expected<std::vector<std::byte>, LevelTextureStoreError>
    LoadRawBytes(
        const GameDataService& game_data, const LevelTextureHandle& handle) const;

private:
    struct Impl;
    explicit LevelTextureStore(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::content
