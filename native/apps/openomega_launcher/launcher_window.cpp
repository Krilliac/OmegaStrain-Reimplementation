#include "launcher_window.h"

#include "launcher_config.h"
#include "omega/content/game_data_service.h"
#include "omega/debug/subsystem_entry_break.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::launcher
{
namespace
{
using Microsoft::WRL::ComPtr;

constexpr wchar_t kWindowClassName[] = L"OpenOmegaNativeLauncherWindow";
constexpr wchar_t kWindowTitle[] = L"OpenOmega - Game Data Setup";
constexpr float kDefaultClientWidth = 980.0F;
constexpr float kDefaultClientHeight = 680.0F;
constexpr float kMinimumWindowWidth = 780.0F;
constexpr float kMinimumWindowHeight = 610.0F;

// Posted to the launcher window (from a thread-pool wait callback) once the
// supervised game process exits. wParam carries the process exit code.
constexpr UINT kGameExitedMessage = WM_APP + 1U;

enum class FocusTarget : std::uint8_t
{
    None,
    ExtractedFolder,
    OwnedIso,
    Gamepad,
    DeveloperDiagnostics,
    Play,
    Quit,
};

enum class SourceKind : std::uint8_t
{
    None,
    ExtractedFolder,
    OwnedIso,
};

enum class StatusTone : std::uint8_t
{
    Neutral,
    Busy,
    Valid,
    Error,
};

constexpr std::array<FocusTarget, 6> kFocusOrder{
    FocusTarget::ExtractedFolder,
    FocusTarget::OwnedIso,
    FocusTarget::Gamepad,
    FocusTarget::DeveloperDiagnostics,
    FocusTarget::Play,
    FocusTarget::Quit,
};

struct LauncherLayout
{
    D2D1_RECT_F source_card{};
    D2D1_RECT_F extracted_folder{};
    D2D1_RECT_F owned_iso{};
    D2D1_RECT_F settings_card{};
    D2D1_RECT_F gamepad{};
    D2D1_RECT_F developer_diagnostics{};
    D2D1_RECT_F play{};
    D2D1_RECT_F quit{};
};

[[nodiscard]] D2D1_COLOR_F Rgba(const std::uint8_t red, const std::uint8_t green,
                                const std::uint8_t blue, const float alpha = 1.0F) noexcept
{
    constexpr float divisor = 255.0F;
    return D2D1::ColorF(static_cast<float>(red) / divisor, static_cast<float>(green) / divisor,
                        static_cast<float>(blue) / divisor, alpha);
}

[[nodiscard]] D2D1_COLOR_F BackgroundColor() noexcept
{
    return Rgba(6U, 13U, 23U);
}

[[nodiscard]] D2D1_COLOR_F HeaderColor() noexcept
{
    return Rgba(9U, 24U, 37U);
}

[[nodiscard]] D2D1_COLOR_F PanelColor() noexcept
{
    return Rgba(13U, 31U, 46U);
}

[[nodiscard]] D2D1_COLOR_F PanelHoverColor() noexcept
{
    return Rgba(18U, 44U, 61U);
}

[[nodiscard]] D2D1_COLOR_F BorderColor() noexcept
{
    return Rgba(41U, 72U, 88U);
}

[[nodiscard]] D2D1_COLOR_F CyanColor() noexcept
{
    return Rgba(75U, 202U, 218U);
}

[[nodiscard]] D2D1_COLOR_F TealColor() noexcept
{
    return Rgba(31U, 133U, 148U);
}

[[nodiscard]] D2D1_COLOR_F AmberColor() noexcept
{
    return Rgba(218U, 166U, 71U);
}

[[nodiscard]] D2D1_COLOR_F GreenColor() noexcept
{
    return Rgba(81U, 193U, 132U);
}

[[nodiscard]] D2D1_COLOR_F ErrorColor() noexcept
{
    return Rgba(226U, 104U, 100U);
}

[[nodiscard]] D2D1_COLOR_F PrimaryTextColor() noexcept
{
    return Rgba(231U, 241U, 244U);
}

[[nodiscard]] D2D1_COLOR_F SecondaryTextColor() noexcept
{
    return Rgba(151U, 175U, 183U);
}

[[nodiscard]] D2D1_COLOR_F MutedTextColor() noexcept
{
    return Rgba(96U, 124U, 134U);
}

[[nodiscard]] bool ContainsPoint(const D2D1_RECT_F& rectangle, const D2D1_POINT_2F point) noexcept
{
    return point.x >= rectangle.left && point.x <= rectangle.right && point.y >= rectangle.top &&
           point.y <= rectangle.bottom;
}

[[nodiscard]] std::wstring SanitizeVisibleText(std::wstring text, const std::size_t maximum_length)
{
    for (wchar_t& character : text)
    {
        if (character == L'\r' || character == L'\n' || character == L'\t' || character < L' ')
        {
            character = L' ';
        }
    }

    if (text.size() > maximum_length)
    {
        text.resize(maximum_length);
        if (maximum_length > 0U)
        {
            text.back() = L'\x2026';
        }
    }
    return text;
}

[[nodiscard]] std::wstring Utf8ToVisibleWide(const std::string_view text)
{
    constexpr std::size_t maximum_input_bytes = 240U;
    const std::size_t bounded_size = std::min(text.size(), maximum_input_bytes);
    if (bounded_size == 0U)
    {
        return {};
    }

    const int input_size = static_cast<int>(bounded_size);
    const int required =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
    if (required > 0)
    {
        std::wstring result(static_cast<std::size_t>(required), L'\0');
        const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                  input_size, result.data(), required);
        if (written == required)
        {
            return SanitizeVisibleText(std::move(result), 132U);
        }
    }

    std::wstring fallback;
    fallback.reserve(bounded_size);
    for (std::size_t index = 0U; index < bounded_size; ++index)
    {
        const unsigned char character = static_cast<unsigned char>(text[index]);
        fallback.push_back(
            character >= 0x20U && character <= 0x7EU ? static_cast<wchar_t>(character) : L'?');
    }
    return SanitizeVisibleText(std::move(fallback), 132U);
}

[[nodiscard]] std::wstring SelectedLeaf(const std::filesystem::path& path)
{
    std::wstring leaf = path.filename().native();
    if (leaf.empty())
    {
        leaf = L"Selected source";
    }
    return SanitizeVisibleText(std::move(leaf), 64U);
}

[[nodiscard]] bool IsIsoPath(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().native();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const wchar_t value) {
                       return value >= L'A' && value <= L'Z'
                                  ? static_cast<wchar_t>(value - L'A' + L'a')
                                  : value;
                   });
    return extension == L".iso";
}

[[nodiscard]] std::optional<std::filesystem::path> ReadLocalAppDataPath()
{
    constexpr wchar_t variable_name[] = L"LOCALAPPDATA";
    DWORD capacity = ::GetEnvironmentVariableW(variable_name, nullptr, 0U);
    if (capacity == 0U)
    {
        return std::nullopt;
    }

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::vector<wchar_t> buffer(static_cast<std::size_t>(capacity), L'\0');
        const DWORD written = ::GetEnvironmentVariableW(variable_name, buffer.data(), capacity);
        if (written == 0U)
        {
            return std::nullopt;
        }
        if (written < capacity)
        {
            std::filesystem::path result(std::wstring_view(buffer.data(), written));
            if (result.empty() || !result.is_absolute())
            {
                return std::nullopt;
            }
            return result;
        }
        capacity = written + 1U;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> DefaultConfigurationPath()
{
    auto local_app_data = ReadLocalAppDataPath();
    if (!local_app_data)
    {
        return std::nullopt;
    }
    return *local_app_data / L"OpenOmega" / L"openomega.cfg";
}

// The launcher redirects the supervised game's stdout+stderr here so a headless
// failure (the child has no console) can be surfaced back in the launcher window
// instead of the process silently vanishing. Best-effort: an absent path just
// disables output capture, never the launch.
[[nodiscard]] std::optional<std::filesystem::path> DefaultRunLogPath()
{
    auto local_app_data = ReadLocalAppDataPath();
    if (!local_app_data)
    {
        return std::nullopt;
    }
    return *local_app_data / L"OpenOmega" / L"last-run.log";
}

// Reads the final meaningful diagnostic line from the run log so the launcher can
// show why the game exited. Returns an empty string when nothing usable is found.
[[nodiscard]] std::wstring ReadRunLogFailureSummary(const std::filesystem::path& log_path)
{
    HANDLE file = ::CreateFileW(log_path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return std::wstring{};
    }

    LARGE_INTEGER size{};
    if (::GetFileSizeEx(file, &size) == FALSE || size.QuadPart <= 0)
    {
        ::CloseHandle(file);
        return std::wstring{};
    }

    constexpr LONGLONG kMaxTail = 4096;
    const LONGLONG tail = size.QuadPart < kMaxTail ? size.QuadPart : kMaxTail;
    LARGE_INTEGER offset{};
    offset.QuadPart = size.QuadPart - tail;
    if (::SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) == FALSE)
    {
        ::CloseHandle(file);
        return std::wstring{};
    }

    std::string bytes(static_cast<std::size_t>(tail), '\0');
    DWORD read = 0U;
    const BOOL ok = ::ReadFile(file, bytes.data(), static_cast<DWORD>(tail), &read, nullptr);
    ::CloseHandle(file);
    if (ok == FALSE || read == 0U)
    {
        return std::wstring{};
    }
    bytes.resize(static_cast<std::size_t>(read));

    // Prefer the last line that names an ERROR or the failing runtime seam; fall
    // back to the last non-empty line.
    std::string chosen;
    std::size_t line_start = 0U;
    for (std::size_t cursor = 0U; cursor <= bytes.size(); ++cursor)
    {
        if (cursor == bytes.size() || bytes[cursor] == '\n' || bytes[cursor] == '\r')
        {
            if (cursor > line_start)
            {
                std::string_view line(bytes.data() + line_start, cursor - line_start);
                const bool interesting = line.find("ERROR") != std::string_view::npos ||
                                         line.find("runtime loop:") != std::string_view::npos ||
                                         line.find("]: ") != std::string_view::npos;
                if (interesting || chosen.empty())
                {
                    chosen.assign(line);
                }
            }
            line_start = cursor + 1U;
        }
    }
    if (chosen.empty())
    {
        return std::wstring{};
    }

    constexpr std::size_t kMaxSummary = 160U;
    if (chosen.size() > kMaxSummary)
    {
        chosen.resize(kMaxSummary);
    }
    // The diagnostic stream is ASCII; widen byte-for-byte and drop anything else.
    std::wstring summary;
    summary.reserve(chosen.size());
    for (const unsigned char byte : chosen)
    {
        summary.push_back(byte < 0x80U ? static_cast<wchar_t>(byte) : L'?');
    }
    return summary;
}

[[nodiscard]] std::expected<std::filesystem::path, DWORD> CurrentExecutablePath()
{
    std::vector<wchar_t> buffer(512U, L'\0');
    constexpr std::size_t maximum_path_characters = 32768U;
    while (buffer.size() <= maximum_path_characters)
    {
        const DWORD capacity = static_cast<DWORD>(buffer.size());
        ::SetLastError(ERROR_SUCCESS);
        const DWORD written = ::GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (written == 0U)
        {
            return std::unexpected(::GetLastError());
        }
        if (written < capacity - 1U ||
            (written < capacity && ::GetLastError() != ERROR_INSUFFICIENT_BUFFER))
        {
            return std::filesystem::path(std::wstring_view(buffer.data(), written));
        }
        buffer.resize(buffer.size() * 2U, L'\0');
    }
    return std::unexpected(ERROR_INSUFFICIENT_BUFFER);
}

class LauncherWindow final
{
  public:
    LauncherWindow(HINSTANCE instance, std::optional<std::filesystem::path> configuration_path)
        : instance_(instance), configuration_path_(std::move(configuration_path))
    {
    }

    LauncherWindow(const LauncherWindow&) = delete;
    LauncherWindow& operator=(const LauncherWindow&) = delete;

    ~LauncherWindow()
    {
        // Defensive cleanup: the normal exit path already unregisters the wait and
        // closes the process handle in OnGameExited, but never leak them if the
        // window is torn down while a supervised game is still tracked.
        if (game_wait_ != nullptr)
        {
            static_cast<void>(::UnregisterWaitEx(game_wait_, INVALID_HANDLE_VALUE));
            game_wait_ = nullptr;
        }
        if (game_process_ != nullptr)
        {
            ::CloseHandle(game_process_);
            game_process_ = nullptr;
        }
    }

    [[nodiscard]] bool CreateAndShow(const int show_command)
    {
        if (!RegisterWindowClass())
        {
            return false;
        }

        const UINT dpi = ::GetDpiForSystem();
        RECT window_rectangle{
            0, 0, ::MulDiv(static_cast<int>(kDefaultClientWidth), static_cast<int>(dpi), 96),
            ::MulDiv(static_cast<int>(kDefaultClientHeight), static_cast<int>(dpi), 96)};
        if (!::AdjustWindowRectExForDpi(&window_rectangle, WS_OVERLAPPEDWINDOW, FALSE,
                                        WS_EX_APPWINDOW, dpi))
        {
            return false;
        }

        const int window_width = window_rectangle.right - window_rectangle.left;
        const int window_height = window_rectangle.bottom - window_rectangle.top;
        RECT work_area{};
        (void)::SystemParametersInfoW(SPI_GETWORKAREA, 0U, &work_area, 0U);
        const LONG x_offset = std::max<LONG>(
            0L, (work_area.right - work_area.left - static_cast<LONG>(window_width)) / 2L);
        const LONG y_offset = std::max<LONG>(
            0L, (work_area.bottom - work_area.top - static_cast<LONG>(window_height)) / 2L);
        const int x = static_cast<int>(work_area.left + x_offset);
        const int y = static_cast<int>(work_area.top + y_offset);

        window_ = ::CreateWindowExW(WS_EX_APPWINDOW, kWindowClassName, kWindowTitle,
                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, window_width,
                                    window_height, nullptr, nullptr, instance_, this);
        if (window_ == nullptr)
        {
            return false;
        }

        ::ShowWindow(window_, show_command == 0 ? SW_SHOWNORMAL : show_command);
        ::UpdateWindow(window_);
        LoadRememberedPreferences();
        ::InvalidateRect(window_, nullptr, FALSE);
        return true;
    }

    [[nodiscard]] int RunMessageLoop() const
    {
        MSG message{};
        while (true)
        {
            const BOOL result = ::GetMessageW(&message, nullptr, 0U, 0U);
            if (result == -1)
            {
                return 1;
            }
            if (result == 0)
            {
                return static_cast<int>(message.wParam);
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }

  private:
    [[nodiscard]] bool RegisterWindowClass() const
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &LauncherWindow::WindowProcedure;
        window_class.hInstance = instance_;
        window_class.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kWindowClassName;
        window_class.hIconSm = window_class.hIcon;
        if (::RegisterClassExW(&window_class) != 0U)
        {
            return true;
        }
        return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    static LRESULT CALLBACK WindowProcedure(HWND window, const UINT message, const WPARAM wparam,
                                            const LPARAM lparam)
    {
        LauncherWindow* self =
            reinterpret_cast<LauncherWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<LauncherWindow*>(creation->lpCreateParams);
            self->window_ = window;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        if (self != nullptr)
        {
            return self->HandleMessage(message, wparam, lparam);
        }
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT HandleMessage(const UINT message, const WPARAM wparam, const LPARAM lparam)
    {
        switch (message)
        {
        case WM_CREATE:
            return SUCCEEDED(CreateDeviceIndependentResources()) ? 0 : -1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            ResizeRenderTarget(LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_DPICHANGED:
            HandleDpiChanged(wparam, lparam);
            return 0;
        case WM_GETMINMAXINFO:
            SetMinimumWindowSize(lparam);
            return 0;
        case WM_MOUSEMOVE:
            HandleMouseMove(lparam);
            return 0;
        case WM_MOUSELEAVE:
            mouse_tracking_ = false;
            SetHoveredTarget(FocusTarget::None);
            return 0;
        case WM_LBUTTONDOWN:
            HandleLeftButtonDown(lparam);
            return 0;
        case WM_LBUTTONUP:
            HandleLeftButtonUp(lparam);
            return 0;
        case WM_KEYDOWN:
            if (HandleKeyDown(wparam))
            {
                return 0;
            }
            break;
        case WM_SETFOCUS:
            ::InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            pressed_target_ = FocusTarget::None;
            if (::GetCapture() == window_)
            {
                ::ReleaseCapture();
            }
            ::InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT && hovered_target_ != FocusTarget::None &&
                IsEnabled(hovered_target_))
            {
                ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_CLOSE:
            ::DestroyWindow(window_);
            return 0;
        case kGameExitedMessage:
            OnGameExited();
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
        {
            const HWND destroyed_window = window_;
            ::SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
            window_ = nullptr;
            return ::DefWindowProcW(destroyed_window, message, wparam, lparam);
        }
        default:
            break;
        }
        return ::DefWindowProcW(window_, message, wparam, lparam);
    }

    [[nodiscard]] HRESULT CreateDeviceIndependentResources()
    {
        HRESULT result = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                             d2d_factory_.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            return result;
        }

        result = ::DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_factory_.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        result = CreateTextFormat(30.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, title_format_);
        if (SUCCEEDED(result))
        {
            result = CreateTextFormat(12.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, kicker_format_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateTextFormat(18.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, heading_format_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateTextFormat(12.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, label_format_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateTextFormat(14.0F, DWRITE_FONT_WEIGHT_NORMAL, body_format_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateTextFormat(14.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, button_format_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateTextFormat(13.0F, DWRITE_FONT_WEIGHT_NORMAL, status_format_);
        }
        return result;
    }

    [[nodiscard]] HRESULT CreateTextFormat(const float size, const DWRITE_FONT_WEIGHT weight,
                                           ComPtr<IDWriteTextFormat>& destination) const
    {
        constexpr std::array<const wchar_t*, 2> families{L"Bahnschrift", L"Segoe UI"};
        HRESULT result = E_FAIL;
        for (const wchar_t* family : families)
        {
            result = write_factory_->CreateTextFormat(
                family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                L"en-US", destination.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result))
            {
                (void)destination->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                return result;
            }
        }
        return result;
    }

    [[nodiscard]] HRESULT EnsureDeviceResources()
    {
        if (render_target_ != nullptr)
        {
            return S_OK;
        }

        RECT client_rectangle{};
        if (!::GetClientRect(window_, &client_rectangle))
        {
            return HRESULT_FROM_WIN32(::GetLastError());
        }
        const UINT width = static_cast<UINT>(std::max<LONG>(0, client_rectangle.right));
        const UINT height = static_cast<UINT>(std::max<LONG>(0, client_rectangle.bottom));

        HRESULT result = d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(width, height)),
            render_target_.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            return result;
        }

        const float dpi = static_cast<float>(::GetDpiForWindow(window_));
        render_target_->SetDpi(dpi, dpi);
        render_target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        result = render_target_->CreateSolidColorBrush(PrimaryTextColor(),
                                                       brush_.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            DiscardDeviceResources();
        }
        return result;
    }

    void DiscardDeviceResources()
    {
        brush_.Reset();
        render_target_.Reset();
    }

    [[nodiscard]] D2D1_SIZE_F ClientSizeInDips() const
    {
        if (render_target_ != nullptr)
        {
            return render_target_->GetSize();
        }
        RECT rectangle{};
        (void)::GetClientRect(window_, &rectangle);
        const float scale = static_cast<float>(::GetDpiForWindow(window_)) / 96.0F;
        return D2D1::SizeF(static_cast<float>(rectangle.right) / scale,
                           static_cast<float>(rectangle.bottom) / scale);
    }

    [[nodiscard]] LauncherLayout CalculateLayout() const
    {
        const D2D1_SIZE_F size = ClientSizeInDips();
        const float left = 44.0F;
        const float right = std::max(left + 690.0F, size.width - 44.0F);
        const float footer_top = std::max(526.0F, size.height - 80.0F);

        LauncherLayout layout{};
        layout.source_card = D2D1::RectF(left, 151.0F, right, 386.0F);
        layout.extracted_folder = D2D1::RectF(left + 22.0F, 218.0F, left + 238.0F, 262.0F);
        layout.owned_iso = D2D1::RectF(left + 250.0F, 218.0F, left + 466.0F, 262.0F);
        layout.settings_card = D2D1::RectF(left, 402.0F, right, 500.0F);
        layout.gamepad = D2D1::RectF(right - 268.0F, 424.0F, right - 20.0F, 480.0F);
        layout.quit = D2D1::RectF(right - 106.0F, footer_top, right, footer_top + 48.0F);
        layout.play = D2D1::RectF(layout.quit.left - 202.0F, footer_top, layout.quit.left - 12.0F,
                                  footer_top + 48.0F);
        layout.developer_diagnostics = D2D1::RectF(layout.play.left - 222.0F, footer_top,
                                                   layout.play.left - 12.0F, footer_top + 48.0F);
        return layout;
    }

    void Paint()
    {
        PAINTSTRUCT paint_structure{};
        (void)::BeginPaint(window_, &paint_structure);
        const HRESULT resources_result = EnsureDeviceResources();
        if (SUCCEEDED(resources_result))
        {
            Render();
        }
        ::EndPaint(window_, &paint_structure);
    }

    void Render()
    {
        const D2D1_SIZE_F size = render_target_->GetSize();
        const LauncherLayout layout = CalculateLayout();
        render_target_->BeginDraw();
        render_target_->Clear(BackgroundColor());

        FillRectangle(D2D1::RectF(0.0F, 0.0F, size.width, 91.0F), HeaderColor());
        FillRectangle(D2D1::RectF(0.0F, 90.0F, size.width, 92.0F), CyanColor());
        FillRectangle(D2D1::RectF(44.0F, 90.0F, 132.0F, 94.0F), AmberColor());

        for (float x = 44.0F; x < size.width; x += 72.0F)
        {
            DrawLine(D2D1::Point2F(x, 94.0F), D2D1::Point2F(x, size.height),
                     Rgba(27U, 52U, 65U, 0.24F), 1.0F);
        }

        DrawText(L"OPENOMEGA // FIELD TERMINAL", title_format_.Get(),
                 D2D1::RectF(44.0F, 23.0F, size.width - 230.0F, 63.0F), PrimaryTextColor());
        DrawText(L"NATIVE PRELAUNCH CONFIGURATION", kicker_format_.Get(),
                 D2D1::RectF(46.0F, 64.0F, size.width - 230.0F, 84.0F), SecondaryTextColor());
        DrawText(L"PRELAUNCH / WIN64", kicker_format_.Get(),
                 D2D1::RectF(size.width - 220.0F, 36.0F, size.width - 44.0F, 60.0F), CyanColor(),
                 DWRITE_TEXT_ALIGNMENT_TRAILING);

        DrawText(L"GAME DATA SETUP", heading_format_.Get(),
                 D2D1::RectF(44.0F, 116.0F, size.width - 44.0F, 144.0F), PrimaryTextColor());

        DrawPanel(layout.source_card);
        FillRectangle(D2D1::RectF(layout.source_card.left, layout.source_card.top,
                                  layout.source_card.left + 3.0F, layout.source_card.bottom),
                      AmberColor());
        DrawText(L"OWNED GAME DATA", label_format_.Get(),
                 D2D1::RectF(layout.source_card.left + 22.0F, 172.0F,
                             layout.source_card.right - 22.0F, 191.0F),
                 CyanColor());
        DrawText(L"Select an extracted NTSC-U data folder or your legally owned "
                 L"ISO image.",
                 body_format_.Get(),
                 D2D1::RectF(layout.source_card.left + 22.0F, 191.0F,
                             layout.source_card.right - 22.0F, 212.0F),
                 SecondaryTextColor());

        DrawButton(FocusTarget::ExtractedFolder, layout.extracted_folder,
                   L"Choose extracted folder", false);
        DrawButton(FocusTarget::OwnedIso, layout.owned_iso, L"Choose owned ISO", false);

        DrawText(L"CURRENT SOURCE", label_format_.Get(),
                 D2D1::RectF(layout.source_card.left + 22.0F, 283.0F,
                             layout.source_card.right - 22.0F, 301.0F),
                 MutedTextColor());
        DrawText(source_summary_, body_format_.Get(),
                 D2D1::RectF(layout.source_card.left + 22.0F, 302.0F,
                             layout.source_card.right - 22.0F, 326.0F),
                 source_kind_ == SourceKind::None ? SecondaryTextColor() : PrimaryTextColor());
        DrawStatus(D2D1::Point2F(layout.source_card.left + 27.0F, 350.0F),
                   D2D1::RectF(layout.source_card.left + 42.0F, 337.0F,
                               layout.source_card.right - 22.0F, 365.0F));

        DrawPanel(layout.settings_card);
        DrawText(L"INPUT", label_format_.Get(),
                 D2D1::RectF(layout.settings_card.left + 22.0F, 420.0F,
                             layout.settings_card.left + 280.0F, 439.0F),
                 CyanColor());
        DrawText(L"KEYBOARD + MOUSE", body_format_.Get(),
                 D2D1::RectF(layout.settings_card.left + 22.0F, 442.0F,
                             layout.settings_card.left + 290.0F, 465.0F),
                 PrimaryTextColor());
        DrawText(L"Always enabled", status_format_.Get(),
                 D2D1::RectF(layout.settings_card.left + 22.0F, 465.0F,
                             layout.settings_card.left + 290.0F, 486.0F),
                 GreenColor());
        DrawGamepadControl(layout.gamepad);

        DrawText(L"Tab / arrows navigate    Enter selects    Esc closes", status_format_.Get(),
                 D2D1::RectF(44.0F, layout.play.top + 14.0F,
                             layout.developer_diagnostics.left - 20.0F, layout.play.bottom),
                 MutedTextColor());
        DrawButton(FocusTarget::DeveloperDiagnostics, layout.developer_diagnostics,
                   L"DEV DIAGNOSTICS", false);
        DrawButton(FocusTarget::Play, layout.play, L"PLAY OPENOMEGA", true);
        DrawButton(FocusTarget::Quit, layout.quit, L"QUIT", false);

        const HRESULT draw_result = render_target_->EndDraw();
        if (draw_result == D2DERR_RECREATE_TARGET)
        {
            DiscardDeviceResources();
        }
    }

    void FillRectangle(const D2D1_RECT_F& rectangle, const D2D1_COLOR_F color)
    {
        brush_->SetColor(color);
        render_target_->FillRectangle(rectangle, brush_.Get());
    }

    void DrawLine(const D2D1_POINT_2F start, const D2D1_POINT_2F end, const D2D1_COLOR_F color,
                  const float width)
    {
        brush_->SetColor(color);
        render_target_->DrawLine(start, end, brush_.Get(), width);
    }

    void DrawPanel(const D2D1_RECT_F& rectangle)
    {
        brush_->SetColor(PanelColor());
        render_target_->FillRoundedRectangle(D2D1::RoundedRect(rectangle, 3.0F, 3.0F),
                                             brush_.Get());
        brush_->SetColor(BorderColor());
        render_target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle, 3.0F, 3.0F), brush_.Get(),
                                             1.0F);
    }

    void DrawText(
        const std::wstring_view text, IDWriteTextFormat* format, const D2D1_RECT_F& rectangle,
        const D2D1_COLOR_F color,
        const DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
        const DWRITE_PARAGRAPH_ALIGNMENT paragraph_alignment = DWRITE_PARAGRAPH_ALIGNMENT_NEAR)
    {
        if (text.empty())
        {
            return;
        }
        (void)format->SetTextAlignment(alignment);
        (void)format->SetParagraphAlignment(paragraph_alignment);
        brush_->SetColor(color);
        const std::size_t bounded_size =
            std::min<std::size_t>(text.size(), std::numeric_limits<UINT32>::max());
        render_target_->DrawTextW(text.data(), static_cast<UINT32>(bounded_size), format, rectangle,
                                  brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP,
                                  DWRITE_MEASURING_MODE_NATURAL);
    }

    void DrawButton(const FocusTarget target, const D2D1_RECT_F& rectangle,
                    const std::wstring_view label, const bool primary)
    {
        const bool enabled = IsEnabled(target);
        const bool hovered = hovered_target_ == target && enabled;
        const bool pressed = pressed_target_ == target && enabled;
        D2D1_COLOR_F fill = primary && enabled ? TealColor() : PanelColor();
        if (hovered)
        {
            fill = primary ? Rgba(37U, 157U, 171U) : PanelHoverColor();
        }
        if (pressed)
        {
            fill = Rgba(20U, 92U, 106U);
        }
        if (!enabled)
        {
            fill = Rgba(14U, 29U, 40U);
        }

        brush_->SetColor(fill);
        render_target_->FillRoundedRectangle(D2D1::RoundedRect(rectangle, 3.0F, 3.0F),
                                             brush_.Get());
        brush_->SetColor(enabled ? BorderColor() : Rgba(31U, 51U, 61U));
        render_target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle, 3.0F, 3.0F), brush_.Get(),
                                             1.0F);

        if (focused_target_ == target && ::GetFocus() == window_)
        {
            const D2D1_RECT_F focus_rectangle =
                D2D1::RectF(rectangle.left + 2.0F, rectangle.top + 2.0F, rectangle.right - 2.0F,
                            rectangle.bottom - 2.0F);
            brush_->SetColor(primary ? AmberColor() : CyanColor());
            render_target_->DrawRoundedRectangle(D2D1::RoundedRect(focus_rectangle, 2.0F, 2.0F),
                                                 brush_.Get(), 1.5F);
        }

        DrawText(label, button_format_.Get(), rectangle,
                 enabled ? PrimaryTextColor() : MutedTextColor(), DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawGamepadControl(const D2D1_RECT_F& rectangle)
    {
        const bool hovered = hovered_target_ == FocusTarget::Gamepad;
        brush_->SetColor(hovered ? PanelHoverColor() : Rgba(10U, 27U, 40U));
        render_target_->FillRoundedRectangle(D2D1::RoundedRect(rectangle, 3.0F, 3.0F),
                                             brush_.Get());
        brush_->SetColor(BorderColor());
        render_target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle, 3.0F, 3.0F), brush_.Get(),
                                             1.0F);

        const D2D1_RECT_F checkbox = D2D1::RectF(rectangle.left + 14.0F, rectangle.top + 18.0F,
                                                 rectangle.left + 32.0F, rectangle.top + 36.0F);
        brush_->SetColor(preferences_.gamepad_enabled ? TealColor() : BackgroundColor());
        render_target_->FillRectangle(checkbox, brush_.Get());
        brush_->SetColor(preferences_.gamepad_enabled ? CyanColor() : SecondaryTextColor());
        render_target_->DrawRectangle(checkbox, brush_.Get(), 1.5F);
        if (preferences_.gamepad_enabled)
        {
            DrawLine(D2D1::Point2F(checkbox.left + 4.0F, checkbox.top + 9.0F),
                     D2D1::Point2F(checkbox.left + 8.0F, checkbox.bottom - 4.0F),
                     PrimaryTextColor(), 2.0F);
            DrawLine(D2D1::Point2F(checkbox.left + 8.0F, checkbox.bottom - 4.0F),
                     D2D1::Point2F(checkbox.right - 3.0F, checkbox.top + 4.0F), PrimaryTextColor(),
                     2.0F);
        }

        DrawText(L"GAMEPAD INPUT", label_format_.Get(),
                 D2D1::RectF(rectangle.left + 43.0F, rectangle.top + 8.0F, rectangle.right - 10.0F,
                             rectangle.top + 27.0F),
                 PrimaryTextColor());
        DrawText(preferences_.gamepad_enabled ? L"Optional / enabled"
                                              : L"Optional / off by default",
                 status_format_.Get(),
                 D2D1::RectF(rectangle.left + 43.0F, rectangle.top + 29.0F, rectangle.right - 10.0F,
                             rectangle.bottom - 4.0F),
                 preferences_.gamepad_enabled ? GreenColor() : SecondaryTextColor());

        if (focused_target_ == FocusTarget::Gamepad && ::GetFocus() == window_)
        {
            const D2D1_RECT_F focus_rectangle =
                D2D1::RectF(rectangle.left + 2.0F, rectangle.top + 2.0F, rectangle.right - 2.0F,
                            rectangle.bottom - 2.0F);
            brush_->SetColor(CyanColor());
            render_target_->DrawRoundedRectangle(D2D1::RoundedRect(focus_rectangle, 2.0F, 2.0F),
                                                 brush_.Get(), 1.5F);
        }
    }

    void DrawStatus(const D2D1_POINT_2F center, const D2D1_RECT_F& text_rectangle)
    {
        D2D1_COLOR_F color = SecondaryTextColor();
        switch (status_tone_)
        {
        case StatusTone::Busy:
            color = AmberColor();
            break;
        case StatusTone::Valid:
            color = GreenColor();
            break;
        case StatusTone::Error:
            color = ErrorColor();
            break;
        case StatusTone::Neutral:
        default:
            break;
        }
        brush_->SetColor(color);
        render_target_->FillEllipse(D2D1::Ellipse(center, 4.0F, 4.0F), brush_.Get());
        DrawText(status_text_, status_format_.Get(), text_rectangle, color,
                 DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void ResizeRenderTarget(const WORD width, const WORD height)
    {
        if (render_target_ != nullptr)
        {
            const HRESULT result = render_target_->Resize(D2D1::SizeU(width, height));
            if (result == D2DERR_RECREATE_TARGET)
            {
                DiscardDeviceResources();
            }
        }
    }

    void HandleDpiChanged(const WPARAM wparam, const LPARAM lparam)
    {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        (void)::SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
        if (render_target_ != nullptr)
        {
            const float dpi = static_cast<float>(LOWORD(wparam));
            render_target_->SetDpi(dpi, dpi);
        }
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    void SetMinimumWindowSize(const LPARAM lparam) const
    {
        auto* constraints = reinterpret_cast<MINMAXINFO*>(lparam);
        const UINT dpi = ::GetDpiForWindow(window_);
        RECT minimum_rectangle{
            0, 0, ::MulDiv(static_cast<int>(kMinimumWindowWidth), static_cast<int>(dpi), 96),
            ::MulDiv(static_cast<int>(kMinimumWindowHeight), static_cast<int>(dpi), 96)};
        if (::AdjustWindowRectExForDpi(&minimum_rectangle, WS_OVERLAPPEDWINDOW, FALSE,
                                       WS_EX_APPWINDOW, dpi))
        {
            constraints->ptMinTrackSize.x = minimum_rectangle.right - minimum_rectangle.left;
            constraints->ptMinTrackSize.y = minimum_rectangle.bottom - minimum_rectangle.top;
        }
    }

    [[nodiscard]] D2D1_POINT_2F MousePointInDips(const LPARAM lparam) const
    {
        const float scale = static_cast<float>(::GetDpiForWindow(window_)) / 96.0F;
        return D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lparam)) / scale,
                             static_cast<float>(GET_Y_LPARAM(lparam)) / scale);
    }

    [[nodiscard]] FocusTarget HitTest(const D2D1_POINT_2F point) const
    {
        const LauncherLayout layout = CalculateLayout();
        if (ContainsPoint(layout.extracted_folder, point))
        {
            return FocusTarget::ExtractedFolder;
        }
        if (ContainsPoint(layout.owned_iso, point))
        {
            return FocusTarget::OwnedIso;
        }
        if (ContainsPoint(layout.gamepad, point))
        {
            return FocusTarget::Gamepad;
        }
        if (ContainsPoint(layout.developer_diagnostics, point))
        {
            return FocusTarget::DeveloperDiagnostics;
        }
        if (ContainsPoint(layout.play, point))
        {
            return FocusTarget::Play;
        }
        if (ContainsPoint(layout.quit, point))
        {
            return FocusTarget::Quit;
        }
        return FocusTarget::None;
    }

    void HandleMouseMove(const LPARAM lparam)
    {
        if (!mouse_tracking_)
        {
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = static_cast<DWORD>(sizeof(tracking));
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = window_;
            mouse_tracking_ = ::TrackMouseEvent(&tracking) != FALSE;
        }
        SetHoveredTarget(HitTest(MousePointInDips(lparam)));
    }

    void HandleLeftButtonDown(const LPARAM lparam)
    {
        const FocusTarget target = HitTest(MousePointInDips(lparam));
        if (target == FocusTarget::None || !IsEnabled(target))
        {
            return;
        }
        ::SetFocus(window_);
        focused_target_ = target;
        pressed_target_ = target;
        ::SetCapture(window_);
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    void HandleLeftButtonUp(const LPARAM lparam)
    {
        const FocusTarget pressed = pressed_target_;
        pressed_target_ = FocusTarget::None;
        if (::GetCapture() == window_)
        {
            ::ReleaseCapture();
        }
        const FocusTarget released = HitTest(MousePointInDips(lparam));
        ::InvalidateRect(window_, nullptr, FALSE);
        if (pressed != FocusTarget::None && pressed == released && IsEnabled(pressed))
        {
            Activate(pressed);
        }
    }

    void SetHoveredTarget(const FocusTarget target)
    {
        if (hovered_target_ == target)
        {
            return;
        }
        hovered_target_ = target;
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    [[nodiscard]] bool HandleKeyDown(const WPARAM key)
    {
        switch (key)
        {
        case VK_TAB:
            MoveFocus((::GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1);
            return true;
        case VK_LEFT:
        case VK_UP:
            MoveFocus(-1);
            return true;
        case VK_RIGHT:
        case VK_DOWN:
            MoveFocus(1);
            return true;
        case VK_RETURN:
        case VK_SPACE:
            if (IsEnabled(focused_target_))
            {
                Activate(focused_target_);
            }
            return true;
        case VK_ESCAPE:
            ::DestroyWindow(window_);
            return true;
        default:
            return false;
        }
    }

    void MoveFocus(const int direction)
    {
        int current = 0;
        const auto found = std::find(kFocusOrder.begin(), kFocusOrder.end(), focused_target_);
        if (found != kFocusOrder.end())
        {
            current = static_cast<int>(std::distance(kFocusOrder.begin(), found));
        }

        const int count = static_cast<int>(kFocusOrder.size());
        for (int attempt = 0; attempt < count; ++attempt)
        {
            current = (current + direction + count) % count;
            if (IsEnabled(kFocusOrder[static_cast<std::size_t>(current)]))
            {
                focused_target_ = kFocusOrder[static_cast<std::size_t>(current)];
                ::InvalidateRect(window_, nullptr, FALSE);
                return;
            }
        }
    }

    [[nodiscard]] bool IsEnabled(const FocusTarget target) const noexcept
    {
        if (target == FocusTarget::None)
        {
            return false;
        }
        if (target == FocusTarget::Play || target == FocusTarget::DeveloperDiagnostics)
        {
            return valid_source_ && configuration_path_.has_value();
        }
        return true;
    }

    void Activate(const FocusTarget target)
    {
        switch (target)
        {
        case FocusTarget::ExtractedFolder:
            ChooseDataSource(SourceKind::ExtractedFolder);
            break;
        case FocusTarget::OwnedIso:
            ChooseDataSource(SourceKind::OwnedIso);
            break;
        case FocusTarget::Gamepad:
            preferences_.gamepad_enabled = !preferences_.gamepad_enabled;
            ::InvalidateRect(window_, nullptr, FALSE);
            break;
        case FocusTarget::DeveloperDiagnostics:
            SaveAndLaunch(true);
            break;
        case FocusTarget::Play:
            SaveAndLaunch(false);
            break;
        case FocusTarget::Quit:
            ::DestroyWindow(window_);
            break;
        case FocusTarget::None:
        default:
            break;
        }
    }

    [[nodiscard]] std::expected<std::optional<std::filesystem::path>, HRESULT> PickDataSource(
        const SourceKind kind) const
    {
        ComPtr<IFileOpenDialog> dialog;
        HRESULT result = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            return std::unexpected(result);
        }

        FILEOPENDIALOGOPTIONS options{};
        result = dialog->GetOptions(&options);
        if (FAILED(result))
        {
            return std::unexpected(result);
        }
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_NOCHANGEDIR;
        if (kind == SourceKind::ExtractedFolder)
        {
            options |= FOS_PICKFOLDERS;
            (void)dialog->SetTitle(L"Choose extracted OpenOmega game data folder");
        }
        else
        {
            options |= FOS_FILEMUSTEXIST;
            constexpr COMDLG_FILTERSPEC filters[] = {
                {L"PlayStation 2 disc images (*.iso)", L"*.iso"},
                {L"All files (*.*)", L"*.*"},
            };
            result = dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
            if (FAILED(result))
            {
                return std::unexpected(result);
            }
            (void)dialog->SetFileTypeIndex(1U);
            (void)dialog->SetDefaultExtension(L"iso");
            (void)dialog->SetTitle(L"Choose your legally owned OpenOmega ISO image");
        }
        result = dialog->SetOptions(options);
        if (FAILED(result))
        {
            return std::unexpected(result);
        }

        result = dialog->Show(window_);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            return std::optional<std::filesystem::path>{};
        }
        if (FAILED(result))
        {
            return std::unexpected(result);
        }

        ComPtr<IShellItem> selected_item;
        result = dialog->GetResult(selected_item.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            return std::unexpected(result);
        }
        PWSTR selected_path = nullptr;
        result = selected_item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
        if (FAILED(result))
        {
            return std::unexpected(result);
        }
        std::filesystem::path path(selected_path);
        ::CoTaskMemFree(selected_path);
        return std::optional<std::filesystem::path>(std::move(path));
    }

    void ChooseDataSource(const SourceKind kind)
    {
        auto selected = PickDataSource(kind);
        if (!selected)
        {
            status_tone_ = StatusTone::Error;
            status_text_ = L"The Windows file picker could not be opened.";
            ::InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (!*selected)
        {
            return;
        }
        if (kind == SourceKind::OwnedIso && !IsIsoPath(**selected))
        {
            preferences_.data_source = **selected;
            source_kind_ = kind;
            source_summary_ = L"OWNED ISO  //  " + SelectedLeaf(**selected);
            valid_source_ = false;
            status_tone_ = StatusTone::Error;
            status_text_ = L"Choose a disc image with the .iso extension.";
            ::InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        ValidateDataSource(**selected, kind);
    }

    void ValidateDataSource(const std::filesystem::path& path, const SourceKind kind)
    {
        preferences_.data_source = path;
        source_kind_ = kind;
        source_summary_ =
            (kind == SourceKind::OwnedIso ? L"OWNED ISO  //  " : L"EXTRACTED FOLDER  //  ") +
            SelectedLeaf(path);
        valid_source_ = false;
        status_tone_ = StatusTone::Busy;
        status_text_ = L"Validating owned game data...";
        ::InvalidateRect(window_, nullptr, FALSE);
        ::UpdateWindow(window_);

        try
        {
            auto service =
                content::GameDataService::Open(content::GameDataServiceConfig{.root = path});
            if (!service)
            {
                const content::GameDataError& error = service.error();
                const std::wstring code =
                    Utf8ToVisibleWide(content::GameDataErrorCodeName(error.code));
                const std::wstring message = Utf8ToVisibleWide(error.message);
                status_tone_ = StatusTone::Error;
                status_text_ = L"Validation failed [" + code + L"]";
                if (!message.empty())
                {
                    status_text_ += L": " + message;
                }
            }
            else
            {
                valid_source_ = true;
                status_tone_ = StatusTone::Valid;
                status_text_ =
                    L"Ready / " +
                    Utf8ToVisibleWide(content::RetailBuildName(service->identity().build)) +
                    L" data verified.";
            }
        }
        catch (...)
        {
            status_tone_ = StatusTone::Error;
            status_text_ = L"Validation could not be completed.";
        }
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    void LoadRememberedPreferences()
    {
        if (!configuration_path_)
        {
            status_tone_ = StatusTone::Error;
            status_text_ = L"Local application settings are unavailable.";
            return;
        }

        auto loaded = LoadLauncherPreferences(*configuration_path_);
        if (!loaded)
        {
            status_tone_ = StatusTone::Error;
            status_text_ = L"Saved launcher settings could not be loaded.";
            return;
        }
        preferences_ = std::move(*loaded);
        if (!preferences_.data_source)
        {
            return;
        }
        const SourceKind kind = IsIsoPath(*preferences_.data_source) ? SourceKind::OwnedIso
                                                                     : SourceKind::ExtractedFolder;
        ValidateDataSource(*preferences_.data_source, kind);
    }

    void SaveAndLaunch(const bool developer_diagnostics)
    {
        if ((!IsEnabled(FocusTarget::Play) &&
             !IsEnabled(FocusTarget::DeveloperDiagnostics)) ||
            !configuration_path_)
        {
            return;
        }

        auto saved = SaveLauncherPreferencesAtomically(*configuration_path_, preferences_);
        if (!saved)
        {
            status_tone_ = StatusTone::Error;
            status_text_ = L"Launcher settings could not be saved.";
            ::InvalidateRect(window_, nullptr, FALSE);
            return;
        }

        status_tone_ = StatusTone::Busy;
        status_text_ = developer_diagnostics
                           ? L"Starting the native game (developer diagnostics)..."
                           : L"Starting the native game...";
        ::InvalidateRect(window_, nullptr, FALSE);
        ::UpdateWindow(window_);

        auto executable_path = CurrentExecutablePath();
        if (!executable_path)
        {
            SetLaunchFailure(executable_path.error());
            return;
        }
        const std::filesystem::path executable_directory = executable_path->parent_path();
        const std::filesystem::path game_executable = executable_directory / L"openomega.exe";

        // Redirect the child's stdout+stderr to a log so a headless failure (the
        // game runs in its own window-subsystem process with no console) can be
        // surfaced back here instead of the window silently vanishing. This is
        // best-effort: if any handle cannot be prepared we still launch, just
        // without captured diagnostics.
        SECURITY_ATTRIBUTES inheritable{};
        inheritable.nLength = static_cast<DWORD>(sizeof(inheritable));
        inheritable.bInheritHandle = TRUE;

        std::optional<std::filesystem::path> log_path = DefaultRunLogPath();
        HANDLE log_handle = INVALID_HANDLE_VALUE;
        HANDLE null_input = INVALID_HANDLE_VALUE;
        if (log_path)
        {
            log_handle = ::CreateFileW(log_path->c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                       &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (log_handle != INVALID_HANDLE_VALUE)
            {
                null_input = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           &inheritable, OPEN_EXISTING, 0U, nullptr);
            }
        }
        const bool capture_output =
            log_handle != INVALID_HANDLE_VALUE && null_input != INVALID_HANDLE_VALUE;

        std::wstring command_line = L"\"" + game_executable.wstring() + L"\"";
        if (developer_diagnostics)
        {
            command_line += L" --developer-diagnostics";
        }
        std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
        command_buffer.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = static_cast<DWORD>(sizeof(startup));
        if (capture_output)
        {
            startup.dwFlags |= STARTF_USESTDHANDLES;
            startup.hStdInput = null_input;
            startup.hStdOutput = log_handle;
            startup.hStdError = log_handle;
        }

        PROCESS_INFORMATION process{};
        const BOOL launched = ::CreateProcessW(
            game_executable.c_str(), command_buffer.data(), nullptr, nullptr,
            capture_output ? TRUE : FALSE, 0U, nullptr, executable_directory.c_str(), &startup,
            &process);

        // The child owns its own copies of the inherited handles now.
        if (log_handle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(log_handle);
        }
        if (null_input != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(null_input);
        }

        if (!launched)
        {
            SetLaunchFailure(::GetLastError());
            return;
        }
        ::CloseHandle(process.hThread);

        // Supervise the child so its exit (clean or failed) is observed. On a
        // successful registration the launcher hides itself and waits; the wait
        // callback posts kGameExitedMessage back to the UI thread.
        run_log_path_ = capture_output ? log_path : std::nullopt;
        game_process_ = process.hProcess;
        if (::RegisterWaitForSingleObject(&game_wait_, process.hProcess, &GameWaitCallback, window_,
                                          INFINITE, WT_EXECUTEONLYONCE) == FALSE)
        {
            // Supervision unavailable: fall back to the original detached launch
            // so the game still runs; we simply cannot surface a later failure.
            game_wait_ = nullptr;
            game_process_ = nullptr;
            run_log_path_ = std::nullopt;
            ::CloseHandle(process.hProcess);
            ::DestroyWindow(window_);
            return;
        }
        ::ShowWindow(window_, SW_HIDE);
    }

    static VOID CALLBACK GameWaitCallback(PVOID context, BOOLEAN /*timed_out*/)
    {
        auto* const window = static_cast<HWND>(context);
        if (window != nullptr)
        {
            static_cast<void>(::PostMessageW(window, kGameExitedMessage, 0U, 0U));
        }
    }

    void OnGameExited()
    {
        if (game_wait_ != nullptr)
        {
            // INVALID_HANDLE_VALUE blocks until any in-flight callback returns; the
            // callback merely posts, so there is no re-entrancy on the UI thread.
            static_cast<void>(::UnregisterWaitEx(game_wait_, INVALID_HANDLE_VALUE));
            game_wait_ = nullptr;
        }

        DWORD exit_code = 1U;
        if (game_process_ != nullptr)
        {
            static_cast<void>(::GetExitCodeProcess(game_process_, &exit_code));
            ::CloseHandle(game_process_);
            game_process_ = nullptr;
        }

        if (exit_code == 0U)
        {
            ::DestroyWindow(window_);
            return;
        }

        std::wstring summary;
        if (run_log_path_)
        {
            summary = ReadRunLogFailureSummary(*run_log_path_);
        }
        status_tone_ = StatusTone::Error;
        std::wstring text = L"The game exited (code " + std::to_wstring(exit_code) + L").";
        if (!summary.empty())
        {
            text += L" " + summary;
        }
        else
        {
            text += L" See last-run.log in %LOCALAPPDATA%\\OpenOmega.";
        }
        status_text_ = std::move(text);
        ::ShowWindow(window_, SW_SHOW);
        static_cast<void>(::SetForegroundWindow(window_));
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    void SetLaunchFailure(const DWORD error)
    {
        status_tone_ = StatusTone::Error;
        status_text_ = L"The native game could not be started (Windows error " +
                       std::to_wstring(error) + L").";
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    std::optional<std::filesystem::path> configuration_path_;
    // Supervised game process (null unless a launch is in flight and being watched).
    HANDLE game_process_ = nullptr;
    HANDLE game_wait_ = nullptr;
    std::optional<std::filesystem::path> run_log_path_;
    LauncherPreferences preferences_{};
    SourceKind source_kind_ = SourceKind::None;
    bool valid_source_ = false;
    bool mouse_tracking_ = false;
    FocusTarget focused_target_ = FocusTarget::ExtractedFolder;
    FocusTarget hovered_target_ = FocusTarget::None;
    FocusTarget pressed_target_ = FocusTarget::None;
    StatusTone status_tone_ = StatusTone::Neutral;
    std::wstring source_summary_ = L"No game data selected";
    std::wstring status_text_ = L"Select a source to enable Play.";

    ComPtr<ID2D1Factory> d2d_factory_;
    ComPtr<ID2D1HwndRenderTarget> render_target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> write_factory_;
    ComPtr<IDWriteTextFormat> title_format_;
    ComPtr<IDWriteTextFormat> kicker_format_;
    ComPtr<IDWriteTextFormat> heading_format_;
    ComPtr<IDWriteTextFormat> label_format_;
    ComPtr<IDWriteTextFormat> body_format_;
    ComPtr<IDWriteTextFormat> button_format_;
    ComPtr<IDWriteTextFormat> status_format_;
};
} // namespace

int RunLauncher(HINSTANCE instance, const int show_command)
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_launcher_host");
    LauncherWindow launcher(instance, DefaultConfigurationPath());
    if (!launcher.CreateAndShow(show_command))
    {
        ::MessageBoxW(nullptr, L"OpenOmega could not create the launcher window.",
                      L"OpenOmega Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
    return launcher.RunMessageLoop();
}
} // namespace omega::launcher
