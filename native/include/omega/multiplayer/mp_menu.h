#pragma once

// Project-owned multiplayer menu logic. This header contains only bounded text
// editing, menu state, and a deterministic reducer. Emitted actions are UI
// intents; their consumer decides how (or whether) to implement them.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace omega::multiplayer
{
enum class HostMode : std::uint8_t
{
    Listen = 0U,
    Dedicated = 1U,
};

[[nodiscard]] constexpr std::string_view HostModeName(const HostMode mode) noexcept
{
    return mode == HostMode::Dedicated ? "DEDICATED" : "LISTEN";
}

enum class MpMenuActionType : std::uint8_t
{
    None = 0U,
    StartHost,
    Connect,
    JoinSelectedServer,
    RefreshServerList,
};

// Maximum content bytes in a multiplayer text field. Storage has exactly this
// many cells and is not a C string: a full field has no trailing NUL byte.
inline constexpr std::size_t kMpTextCapacity = 31U;

enum class MpTextFieldKind : std::uint8_t
{
    Name = 0U,
    ConnectionTarget = 1U,
};

struct MpTextField;

constexpr void MpTextInsert(
    MpTextField& field,
    char symbol,
    MpTextFieldKind kind) noexcept;
constexpr void MpTextBackspace(MpTextField& field) noexcept;

// Append-only, allocation-free text input with a caret at the end. Mutation is
// restricted to the helpers below so length can never exceed the fixed bound.
struct MpTextField
{
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(cells_.data(), length_);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return length_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return length_ == 0U;
    }

    friend constexpr bool operator==(const MpTextField&, const MpTextField&) noexcept = default;

private:
    std::array<char, kMpTextCapacity> cells_{};
    std::uint8_t length_ = 0U;

    friend constexpr void MpTextInsert(
        MpTextField& field,
        char symbol,
        MpTextFieldKind kind) noexcept;
    friend constexpr void MpTextBackspace(MpTextField& field) noexcept;
};

static_assert(kMpTextCapacity <= 255U);

[[nodiscard]] constexpr bool IsAllowedMpChar(
    const char symbol,
    const MpTextFieldKind kind) noexcept
{
    const auto byte = static_cast<unsigned char>(symbol);
    if (kind == MpTextFieldKind::ConnectionTarget)
    {
        // Admission is intentionally syntax-neutral. This accepts visible
        // single-byte ASCII without choosing IPv4, IPv6, DNS, URI, or port
        // rules; the action consumer validates whichever grammar it supports.
        return byte >= 0x21U && byte <= 0x7EU;
    }

    const bool digit =
        byte >= static_cast<unsigned char>('0') &&
        byte <= static_cast<unsigned char>('9');
    const bool upper =
        byte >= static_cast<unsigned char>('A') &&
        byte <= static_cast<unsigned char>('Z');
    const bool lower =
        byte >= static_cast<unsigned char>('a') &&
        byte <= static_cast<unsigned char>('z');
    return digit || upper || lower || symbol == ' ' || symbol == '-' ||
           symbol == '_' || symbol == '.';
}

constexpr void MpTextInsert(
    MpTextField& field,
    const char symbol,
    const MpTextFieldKind kind) noexcept
{
    if (field.length_ >= kMpTextCapacity || !IsAllowedMpChar(symbol, kind))
        return;

    field.cells_[field.length_] = symbol;
    field.length_ = static_cast<std::uint8_t>(field.length_ + 1U);
}

constexpr void MpTextBackspace(MpTextField& field) noexcept
{
    if (field.length_ == 0U)
        return;

    field.length_ = static_cast<std::uint8_t>(field.length_ - 1U);
    field.cells_[field.length_] = '\0';
}

enum class MpScreen : std::uint8_t
{
    Root = 0U,
    HostGame = 1U,
    DirectConnect = 2U,
    ServerList = 3U,
};

[[nodiscard]] constexpr std::uint8_t MpScreenRowCount(
    const MpScreen screen) noexcept
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

[[nodiscard]] constexpr bool MpRowIsTextField(
    const MpScreen screen,
    const std::uint8_t row) noexcept
{
    return (screen == MpScreen::HostGame && row == 1U) ||
           (screen == MpScreen::DirectConnect && row == 0U);
}

struct MpMenuState
{
    MpScreen screen = MpScreen::Root;
    std::uint8_t selection = 0U;
    HostMode host_mode = HostMode::Listen;
    bool editing = false;
    bool exit_requested = false;
    MpTextField server_name{};
    MpTextField connection_target{};

    friend constexpr bool operator==(const MpMenuState&, const MpMenuState&) noexcept = default;
};

// Owned snapshot emitted by one reducer step. It has no back-reference to menu
// state and makes no promise about transport, discovery, or session lifetime.
struct MpMenuAction
{
    MpMenuActionType type = MpMenuActionType::None;
    HostMode host_mode = HostMode::Listen;
    MpTextField server_name{};
    MpTextField connection_target{};

    friend constexpr bool operator==(const MpMenuAction&, const MpMenuAction&) noexcept = default;
};

struct MpMenuInput
{
    bool up = false;
    bool down = false;
    bool primary = false;
    bool cancel = false;
    bool backspace = false;
    char text_char = '\0';

    friend constexpr bool operator==(const MpMenuInput&, const MpMenuInput&) noexcept = default;
};

struct MpMenuStep
{
    MpMenuState state{};
    MpMenuAction action{};

    friend constexpr bool operator==(const MpMenuStep&, const MpMenuStep&) noexcept = default;
};

[[nodiscard]] constexpr MpTextField& MpActiveField(
    MpMenuState& state) noexcept
{
    return state.screen == MpScreen::DirectConnect
               ? state.connection_target
               : state.server_name;
}

[[nodiscard]] constexpr MpTextFieldKind MpActiveFieldKind(
    const MpScreen screen) noexcept
{
    return screen == MpScreen::DirectConnect
               ? MpTextFieldKind::ConnectionTarget
               : MpTextFieldKind::Name;
}

// One deterministic menu step. Exited states are terminal. For active states,
// stale selections are clamped and stale edit focus is cleared. Input priority:
// edit exit, edit mutation, cancel, primary action, then exclusive navigation.
[[nodiscard]] constexpr MpMenuStep StepMpMenu(
    MpMenuState state,
    const MpMenuInput input) noexcept
{
    MpMenuAction action{};

    if (state.exit_requested)
        return MpMenuStep{.state = state, .action = action};

    const std::uint8_t rows = MpScreenRowCount(state.screen);
    if (state.selection >= rows)
        state.selection = static_cast<std::uint8_t>(rows - 1U);
    if (state.editing && !MpRowIsTextField(state.screen, state.selection))
        state.editing = false;

    if (state.editing)
    {
        if (input.primary || input.cancel)
        {
            state.editing = false;
            return MpMenuStep{.state = state, .action = action};
        }
        if (input.backspace)
            MpTextBackspace(MpActiveField(state));
        if (input.text_char != '\0')
        {
            MpTextInsert(
                MpActiveField(state),
                input.text_char,
                MpActiveFieldKind(state.screen));
        }
        return MpMenuStep{.state = state, .action = action};
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
        return MpMenuStep{.state = state, .action = action};
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
            case 0U:
                state.host_mode =
                    state.host_mode == HostMode::Listen
                        ? HostMode::Dedicated
                        : HostMode::Listen;
                break;
            case 1U:
                state.editing = true;
                break;
            case 2U:
                action.type = MpMenuActionType::StartHost;
                action.host_mode = state.host_mode;
                action.server_name = state.server_name;
                break;
            default:
                state.screen = MpScreen::Root;
                state.selection = 0U;
                break;
            }
            break;
        case MpScreen::DirectConnect:
            switch (state.selection)
            {
            case 0U:
                state.editing = true;
                break;
            case 1U:
                action.type = MpMenuActionType::Connect;
                action.connection_target = state.connection_target;
                break;
            default:
                state.screen = MpScreen::Root;
                state.selection = 0U;
                break;
            }
            break;
        case MpScreen::ServerList:
            switch (state.selection)
            {
            case 0U:
                action.type = MpMenuActionType::JoinSelectedServer;
                break;
            case 1U:
                action.type = MpMenuActionType::RefreshServerList;
                break;
            default:
                state.screen = MpScreen::Root;
                state.selection = 0U;
                break;
            }
            break;
        }
        return MpMenuStep{.state = state, .action = action};
    }

    if (input.up != input.down)
    {
        if (input.up && state.selection > 0U)
            state.selection = static_cast<std::uint8_t>(state.selection - 1U);
        else if (input.down && state.selection + 1U < rows)
            state.selection = static_cast<std::uint8_t>(state.selection + 1U);
    }

    return MpMenuStep{.state = state, .action = action};
}

[[nodiscard]] constexpr std::string_view MpMenuActionTypeName(
    const MpMenuActionType type) noexcept
{
    switch (type)
    {
    case MpMenuActionType::None:
        return "none";
    case MpMenuActionType::StartHost:
        return "start-host";
    case MpMenuActionType::Connect:
        return "connect";
    case MpMenuActionType::JoinSelectedServer:
        return "join-selected-server";
    case MpMenuActionType::RefreshServerList:
        return "refresh-server-list";
    }
    return "none";
}
} // namespace omega::multiplayer
