#pragma once

// The debugger entry hook is a Windows/MSVC Debug-only developer aid.  In every
// other build this header contributes only the no-op macro at the end of the
// file: no platform headers, environment access, atomics, or calls survive.
#if defined(OPENOMEGA_ENABLE_SUBSYSTEM_ENTRY_BREAK) && \
    OPENOMEGA_ENABLE_SUBSYSTEM_ENTRY_BREAK && defined(_WIN32) && \
    defined(_MSC_VER) && defined(_DEBUG)

#ifndef NOMINMAX
#define NOMINMAX
#define OPENOMEGA_SUBSYSTEM_ENTRY_BREAK_RESTORE_NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#define OPENOMEGA_SUBSYSTEM_ENTRY_BREAK_RESTORE_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <debugapi.h>
#include <intrin.h>
#include <processenv.h>
#if defined(OPENOMEGA_SUBSYSTEM_ENTRY_BREAK_RESTORE_LEAN_AND_MEAN)
#undef WIN32_LEAN_AND_MEAN
#undef OPENOMEGA_SUBSYSTEM_ENTRY_BREAK_RESTORE_LEAN_AND_MEAN
#endif
#if defined(OPENOMEGA_SUBSYSTEM_ENTRY_BREAK_RESTORE_NOMINMAX)
#undef NOMINMAX
#undef OPENOMEGA_SUBSYSTEM_ENTRY_BREAK_RESTORE_NOMINMAX
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <string_view>

namespace omega::debug::detail
{
inline constexpr wchar_t kSubsystemEntryBreakEnvironment[] =
    L"OPENOMEGA_DEBUG_BREAK_SUBSYSTEM";
inline constexpr std::size_t kMaximumSubsystemNameBytes = 63U;

struct RequestedSubsystem final
{
    std::array<wchar_t, kMaximumSubsystemNameBytes + 1U> value{};
    std::size_t size = 0U;
    bool valid_ascii = false;
};

// External-linkage inline functions have one shared function-local static
// object across all translation units.  The process therefore reads the
// environment exactly once, at the first genuine instrumented entry.
[[nodiscard]] inline const RequestedSubsystem& RequestedSubsystemFromEnvironment() noexcept
{
    static const RequestedSubsystem request = []() noexcept
    {
        RequestedSubsystem result{};
        constexpr DWORD capacity =
            static_cast<DWORD>(kMaximumSubsystemNameBytes + 1U);
        const DWORD size = ::GetEnvironmentVariableW(
            kSubsystemEntryBreakEnvironment, result.value.data(), capacity);

        // A zero result means absent or empty.  A result at least as large as
        // the capacity means the complete value did not fit.  Both fail closed.
        if (size == 0U || size >= capacity)
            return result;

        for (DWORD index = 0U; index < size; ++index)
        {
            const wchar_t character = result.value[index];
            if (character < L'!' || character > L'~')
                return result;
        }

        result.size = static_cast<std::size_t>(size);
        result.valid_ascii = true;
        return result;
    }();
    return request;
}

[[nodiscard]] inline bool MatchesRequestedSubsystem(
    const std::string_view subsystem) noexcept
{
    const RequestedSubsystem& request = RequestedSubsystemFromEnvironment();
    if (!request.valid_ascii || request.size != subsystem.size())
        return false;

    for (std::size_t index = 0U; index < subsystem.size(); ++index)
    {
        const unsigned char expected = static_cast<unsigned char>(subsystem[index]);
        if (expected < static_cast<unsigned char>('!') ||
            expected > static_cast<unsigned char>('~') ||
            request.value[index] != static_cast<wchar_t>(expected))
        {
            return false;
        }
    }
    return true;
}

// This inline variable is the single ODR-shared one-shot for the process, not
// one flag per library or translation unit.
inline std::atomic_flag g_subsystem_entry_break_consumed = ATOMIC_FLAG_INIT;
} // namespace omega::debug::detail

namespace omega::debug
{
inline void BreakAtSubsystemEntryIfRequested(
    const std::string_view subsystem) noexcept
{
    if (!detail::MatchesRequestedSubsystem(subsystem))
        return;
    if (::IsDebuggerPresent() == FALSE)
        return;
    if (detail::g_subsystem_entry_break_consumed.test_and_set(
            std::memory_order_relaxed))
    {
        return;
    }
    __debugbreak();
}
} // namespace omega::debug

#define OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY(subsystem_literal) \
    ::omega::debug::BreakAtSubsystemEntryIfRequested(subsystem_literal)

#else

#define OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY(subsystem_literal) ((void)0)

#endif
