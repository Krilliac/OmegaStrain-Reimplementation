#pragma once

#include "omega/runtime/content_startup.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace omega::runtime
{
enum class ContentStartupDiagnosticErrorCode : std::uint8_t
{
    InconsistentRepresentation = 0U,
};

struct ContentStartupDiagnosticView
{
    std::string_view category;
    std::string_view message;
};

// [any thread; reentrant] Borrows a validated startup error for immediate presentation. The
// message view remains valid only while the source message storage is alive and unchanged. The
// adapter allocates nothing and rejects every shape that StartContent cannot publish.
[[nodiscard]] std::expected<ContentStartupDiagnosticView, ContentStartupDiagnosticErrorCode>
DescribeContentStartupError(const ContentStartupError& error) noexcept;
} // namespace omega::runtime
