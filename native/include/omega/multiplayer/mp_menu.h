#pragma once

// Project-owned multiplayer front-end menu logic. This is a self-contained,
// pure state machine + session-request seam for custom hosting UX (Host Game,
// Direct Connect, Server List). It authors NO retail data and touches NO
// networking: session requests are a typed interface a future transport layer
// implements. Text entry uses a project text field (the retail GuiTextEditWidget
// factory is still un-RE'd). Kept deliberately separate from the retail
// front-end compositor and from the project ReduceFrontEnd reducer.

#include <array>
#include <cstdint>
#include <string_view>

namespace omega::multiplayer
{
// ---------------------------------------------------------------------------
// Session-request seam (the netcode target). A future transport layer consumes
// these; today the app only logs them. No sockets, threads, or replication.
// ---------------------------------------------------------------------------
enum class HostMode : std::uint8_t
{
    Listen = 0U,    // host plays AND serves (listen server)
    Dedicated = 1U, // host serves only (headless dedicated server)
};

[[nodiscard]] constexpr std::string_view HostModeName(const HostMode mode) noexcept
{
    return mode == HostMode::Dedicated ? "DEDICATED" : "LISTEN";
}

enum class SessionRequestType : std::uint8_t
{
    None = 0U,
    HostSession,   // start hosting with host_mode + server name (+ password)
    DirectConnect, // connect to a typed IP:port address
    JoinServer,    // join a server-list entry by index
    RefreshServers,// re-query the (future) master server / LAN
};

// Fixed capacity for the project text fields (server name / address / password).
inline constexpr std::size_t kMpTextCapacity = 31U; // + implicit NUL headroom

// ---------------------------------------------------------------------------
// Project text-input field: an append-model editable buffer with a caret at the
// end. Bounded, allocation-free, trivially copyable. Character admission is
// per-field so an address field accepts only IP:port bytes.
// ---------------------------------------------------------------------------
enum class MpTextFieldKind : std::uint8_t
{
    Name = 0U,    // server name / password: letters, digits, space, - _ .
    Address = 1U, // IP:port: digits, '.', ':'
};

struct MpTextField
{
    std::array<char, kMpTextCapacity> cells{};
    std::uint8_t length = 0U;

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(cells.data(), length);
    }

    friend constexpr bool operator==(const MpTextField&, const MpTextField&) noexcept = default;
};

[[nodiscard]] constexpr bool IsAllowedMpChar(const char symbol, const MpTextFieldKind kind) noexcept
{
    const auto byte = static_cast<unsigned char>(symbol);
    const bool digit = byte >= static_cast<unsigned char>('0') && byte <= static_cast<unsigned char>('9');
    if (kind == MpTextFieldKind::Address)
        return digit || symbol == '.' || symbol == ':';
    const bool upper = byte >= static_cast<unsigned char>('A') && byte <= static_cast<unsigned char>('Z');
    const bool lower = byte >= static_cast<unsigned char>('a') && byte <= static_cast<unsigned char>('z');
    return digit || upper || lower || symbol == ' ' || symbol == '-' || symbol == '_' || symbol == '.';
}

// [pure] Appends one admitted character at the caret (end). Rejects disallowed
// characters and silently ignores overflow at the fixed capacity.
constexpr void MpTextInsert(MpTextField& field, const char symbol, const MpTextFieldKind kind) noexcept
{
    if (field.length >= kMpTextCapacity)
        return;
    if (!IsAllowedMpChar(symbol, kind))
        return;
    field.cells[field.length] = symbol;
    field.length = static_cast<std::uint8_t>(field.length + 1U);
}

// [pure] Deletes the character before the caret (the last one).
constexpr void MpTextBackspace(MpTextField& field) noexcept
{
    if (field.length == 0U)
        return;
    field.length = static_cast<std::uint8_t>(field.length - 1U);
    field.cells[field.length] = '\0';
}

// ---------------------------------------------------------------------------
// Menu state machine. Four screens; each has a fixed selectable-row count. Rows
// that are text fields enter an edit "focus" on primary; while focused, typed
// characters edit the field and up/down are ignored.
// ---------------------------------------------------------------------------
enum class MpScreen : std::uint8_t
{
    Root = 0U,          // HOST GAME / DIRECT CONNECT / SERVER LIST / BACK
    HostGame = 1U,      // MODE / SERVER NAME(field) / START / BACK
    DirectConnect = 2U, // ADDRESS(field) / CONNECT / BACK
    ServerList = 3U,    // (stub row) JOIN / REFRESH / BACK
};

[[nodiscard]] constexpr std::uint8_t MpScreenRowCount(const MpScreen screen) noexcept
{
    switch (screen)
    {
    case MpScreen::Root:
        return 4U;
    case MpScreen::HostGame:
        return 4U;
    case MpScreen::DirectConnect:
        return 3U;
    case MpScreen::ServerList:
        return 3U;
    }
    return 1U;
}

// Which rows are editable text fields (enter edit focus on primary).
[[nodiscard]] constexpr bool MpRowIsTextField(const MpScreen screen, const std::uint8_t row) noexcept
{
    return (screen == MpScreen::HostGame && row == 1U) ||
           (screen == MpScreen::DirectConnect && row == 0U);
}

struct MpMenuState
{
    MpScreen screen = MpScreen::Root;
    std::uint8_t selection = 0U;
    HostMode host_mode = HostMode::Listen;
    bool editing = false; // text-field edit focus on the current selection
    bool exit_requested = false;
    MpTextField server_name{};
    MpTextField address{};

    friend constexpr bool operator==(const MpMenuState&, const MpMenuState&) noexcept = default;
};

// Owned snapshot of a dispatched request. Copies the relevant field contents so
// the transport layer needs no back-reference into menu state.
struct MpSessionRequest
{
    SessionRequestType type = SessionRequestType::None;
    HostMode host_mode = HostMode::Listen;
    MpTextField server_name{};
    MpTextField address{};
    std::uint8_t server_index = 0U;

    friend constexpr bool operator==(const MpSessionRequest&, const MpSessionRequest&) noexcept = default;
};

struct MpMenuInput
{
    bool up = false;
    bool down = false;
    bool primary = false;
    bool cancel = false;
    bool backspace = false;
    char text_char = '\0'; // one typed character this frame; '\0' = none

    friend constexpr bool operator==(const MpMenuInput&, const MpMenuInput&) noexcept = default;
};

struct MpMenuStep
{
    MpMenuState state{};
    MpSessionRequest request{};

    friend constexpr bool operator==(const MpMenuStep&, const MpMenuStep&) noexcept = default;
};

[[nodiscard]] constexpr MpTextField& MpActiveField(MpMenuState& state) noexcept
{
    return state.screen == MpScreen::DirectConnect ? state.address : state.server_name;
}

[[nodiscard]] constexpr MpTextFieldKind MpActiveFieldKind(const MpScreen screen) noexcept
{
    return screen == MpScreen::DirectConnect ? MpTextFieldKind::Address : MpTextFieldKind::Name;
}

// [pure; reentrant] One deterministic menu step. Priority while editing:
// primary/cancel exit the field, backspace/text edit it, navigation is ignored.
// Otherwise cancel backs out (sub-screen -> Root; Root -> exit_requested),
// primary activates the selected row, and up/down move the clamped selection.
[[nodiscard]] constexpr MpMenuStep StepMpMenu(MpMenuState state, const MpMenuInput input) noexcept
{
    MpSessionRequest request{};

    // Clamp a stale selection into the current screen before anything else.
    const std::uint8_t rows = MpScreenRowCount(state.screen);
    if (state.selection >= rows)
        state.selection = static_cast<std::uint8_t>(rows - 1U);

    if (state.editing)
    {
        if (input.primary || input.cancel)
        {
            state.editing = false;
            return MpMenuStep{.state = state, .request = request};
        }
        if (input.backspace)
            MpTextBackspace(MpActiveField(state));
        if (input.text_char != '\0')
            MpTextInsert(MpActiveField(state), input.text_char, MpActiveFieldKind(state.screen));
        return MpMenuStep{.state = state, .request = request};
    }

    if (input.cancel)
    {
        if (state.screen == MpScreen::Root)
        {
            state.exit_requested = true;
        }
        else
        {
            state.screen = MpScreen::Root;
            state.selection = 0U;
        }
        return MpMenuStep{.state = state, .request = request};
    }

    if (input.primary)
    {
        switch (state.screen)
        {
        case MpScreen::Root:
            switch (state.selection)
            {
            case 0U:
                state.screen = MpScreen::HostGame;
                state.selection = 0U;
                break;
            case 1U:
                state.screen = MpScreen::DirectConnect;
                state.selection = 0U;
                break;
            case 2U:
                state.screen = MpScreen::ServerList;
                state.selection = 0U;
                break;
            default:
                state.exit_requested = true;
                break;
            }
            break;
        case MpScreen::HostGame:
            switch (state.selection)
            {
            case 0U: // MODE toggle
                state.host_mode = state.host_mode == HostMode::Listen ? HostMode::Dedicated : HostMode::Listen;
                break;
            case 1U: // SERVER NAME field -> edit
                state.editing = true;
                break;
            case 2U: // START -> host session request
                request = MpSessionRequest{
                    .type = SessionRequestType::HostSession,
                    .host_mode = state.host_mode,
                    .server_name = state.server_name,
                };
                break;
            default: // BACK
                state.screen = MpScreen::Root;
                state.selection = 0U;
                break;
            }
            break;
        case MpScreen::DirectConnect:
            switch (state.selection)
            {
            case 0U: // ADDRESS field -> edit
                state.editing = true;
                break;
            case 1U: // CONNECT -> direct connect request
                request = MpSessionRequest{
                    .type = SessionRequestType::DirectConnect,
                    .address = state.address,
                };
                break;
            default: // BACK
                state.screen = MpScreen::Root;
                state.selection = 0U;
                break;
            }
            break;
        case MpScreen::ServerList:
            switch (state.selection)
            {
            case 0U: // JOIN (stub row 0)
                request = MpSessionRequest{
                    .type = SessionRequestType::JoinServer,
                    .server_index = 0U,
                };
                break;
            case 1U: // REFRESH
                request = MpSessionRequest{.type = SessionRequestType::RefreshServers};
                break;
            default: // BACK
                state.screen = MpScreen::Root;
                state.selection = 0U;
                break;
            }
            break;
        }
        return MpMenuStep{.state = state, .request = request};
    }

    if (input.up != input.down)
    {
        if (input.up && state.selection > 0U)
            state.selection = static_cast<std::uint8_t>(state.selection - 1U);
        else if (input.down && state.selection + 1U < rows)
            state.selection = static_cast<std::uint8_t>(state.selection + 1U);
    }

    return MpMenuStep{.state = state, .request = request};
}

[[nodiscard]] constexpr std::string_view SessionRequestTypeName(const SessionRequestType type) noexcept
{
    switch (type)
    {
    case SessionRequestType::None:
        return "none";
    case SessionRequestType::HostSession:
        return "host-session";
    case SessionRequestType::DirectConnect:
        return "direct-connect";
    case SessionRequestType::JoinServer:
        return "join-server";
    case SessionRequestType::RefreshServers:
        return "refresh-servers";
    }
    return "none";
}
} // namespace omega::multiplayer
