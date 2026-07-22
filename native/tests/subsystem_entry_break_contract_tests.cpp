#include "omega/debug/subsystem_entry_break.h"

#if defined(OPENOMEGA_ENABLE_SUBSYSTEM_ENTRY_BREAK) && \
    OPENOMEGA_ENABLE_SUBSYSTEM_ENTRY_BREAK && defined(_WIN32) && \
    defined(_MSC_VER) && defined(_DEBUG)

#include <string>
#include <string_view>

namespace
{
struct Scenario final
{
    const wchar_t* environment_value = nullptr;
    std::wstring owned_value;
    bool expected_valid_ascii = false;
    bool expected_exact_match = false;
};

[[nodiscard]] bool SelectScenario(
    const std::wstring_view name, Scenario& scenario)
{
    if (name == L"exact")
    {
        scenario.environment_value = L"omega_simulation";
        scenario.expected_valid_ascii = true;
        scenario.expected_exact_match = true;
    }
    else if (name == L"case")
    {
        scenario.environment_value = L"Omega_simulation";
        scenario.expected_valid_ascii = true;
    }
    else if (name == L"suffix")
    {
        scenario.environment_value = L"omega_simulation_extra";
        scenario.expected_valid_ascii = true;
    }
    else if (name == L"whitespace")
    {
        scenario.environment_value = L"omega_simulation ";
    }
    else if (name == L"non_ascii")
    {
        scenario.environment_value = L"omega_simulati\u00f8n";
    }
    else if (name == L"maximum")
    {
        scenario.owned_value.assign(
            omega::debug::detail::kMaximumSubsystemNameBytes, L'x');
        scenario.environment_value = scenario.owned_value.c_str();
        scenario.expected_valid_ascii = true;
    }
    else if (name == L"too_long")
    {
        scenario.owned_value.assign(
            omega::debug::detail::kMaximumSubsystemNameBytes + 1U, L'x');
        scenario.environment_value = scenario.owned_value.c_str();
    }
    else if (name == L"empty")
    {
        scenario.environment_value = L"";
    }
    else if (name != L"absent")
    {
        return false;
    }
    return true;
}
} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    if (argument_count != 2)
        return 1;

    Scenario scenario{};
    if (!SelectScenario(arguments[1], scenario))
        return 2;
    if (::SetEnvironmentVariableW(
            omega::debug::detail::kSubsystemEntryBreakEnvironment,
            scenario.environment_value) == FALSE)
    {
        return 3;
    }

    const omega::debug::detail::RequestedSubsystem& request =
        omega::debug::detail::RequestedSubsystemFromEnvironment();
    if (request.valid_ascii != scenario.expected_valid_ascii)
        return 4;
    if (omega::debug::detail::MatchesRequestedSubsystem("omega_simulation") !=
        scenario.expected_exact_match)
    {
        return 5;
    }

    // The request is intentionally process-stable: changing the environment
    // after the first access must not cause a second read or a different match.
    if (::SetEnvironmentVariableW(
            omega::debug::detail::kSubsystemEntryBreakEnvironment,
            L"omega_runtime") == FALSE)
    {
        return 6;
    }
    if (&request != &omega::debug::detail::RequestedSubsystemFromEnvironment())
        return 7;
    if (omega::debug::detail::MatchesRequestedSubsystem("omega_simulation") !=
        scenario.expected_exact_match)
    {
        return 8;
    }
    if (omega::debug::detail::MatchesRequestedSubsystem("omega_runtime"))
        return 9;

    return 0;
}

#else

int main()
{
    return 0;
}

#endif
