#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace
{
constexpr wchar_t kCapturePathEnvironment[] = L"OPENOMEGA_ARGV_CAPTURE_PATH";
constexpr std::uint32_t kCaptureMagic = 0x4752414fU;
constexpr std::uint32_t kCaptureVersion = 1U;
static_assert(sizeof(wchar_t) == 2U, "Windows argv capture requires UTF-16 wchar_t");

[[nodiscard]] bool WriteBytes(
    const HANDLE file, const void* const bytes, const DWORD byte_count) noexcept
{
    DWORD written = 0U;
    return WriteFile(file, bytes, byte_count, &written, nullptr) != FALSE &&
           written == byte_count;
}

[[nodiscard]] bool WriteUint32(const HANDLE file, const std::uint32_t value) noexcept
{
    return WriteBytes(file, &value, static_cast<DWORD>(sizeof(value)));
}

[[nodiscard]] std::wstring CapturePath()
{
    const DWORD required = GetEnvironmentVariableW(kCapturePathEnvironment, nullptr, 0U);
    if (required <= 1U)
        return {};

    std::wstring path(required, L'\0');
    const DWORD copied =
        GetEnvironmentVariableW(kCapturePathEnvironment, path.data(), required);
    if (copied == 0U || copied >= required)
        return {};
    path.resize(copied);
    return path;
}
} // namespace

int wmain(const int argument_count, wchar_t* const argument_values[])
{
    if (argument_count < 1 || argument_values == nullptr)
        return 2;

    const std::wstring capture_path = CapturePath();
    if (capture_path.empty())
        return 3;
    const std::wstring temporary_path =
        capture_path + L".tmp-" + std::to_wstring(GetCurrentProcessId());

    const HANDLE output = CreateFileW(temporary_path.c_str(), GENERIC_WRITE, 0U, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        return 4;

    bool succeeded = WriteUint32(output, kCaptureMagic) &&
                     WriteUint32(output, kCaptureVersion) &&
                     WriteUint32(output, static_cast<std::uint32_t>(argument_count));
    for (int index = 0; succeeded && index < argument_count; ++index)
    {
        const std::wstring_view argument(argument_values[index]);
        if (argument.size() > std::numeric_limits<std::uint32_t>::max() ||
            argument.size() > std::numeric_limits<DWORD>::max() / sizeof(wchar_t))
        {
            succeeded = false;
            break;
        }

        const auto code_units = static_cast<std::uint32_t>(argument.size());
        const auto byte_count = static_cast<DWORD>(argument.size() * sizeof(wchar_t));
        succeeded = WriteUint32(output, code_units) &&
                    WriteBytes(output, argument.data(), byte_count);
    }

    if (succeeded)
        succeeded = FlushFileBuffers(output) != FALSE;
    if (CloseHandle(output) == FALSE)
        succeeded = false;
    if (!succeeded)
    {
        DeleteFileW(temporary_path.c_str());
        return 5;
    }

    if (MoveFileExW(temporary_path.c_str(), capture_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        DeleteFileW(temporary_path.c_str());
        return 6;
    }
    return 0;
}
