#include "omega/multiplayer/mp_menu.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
using omega::multiplayer::HostMode;
using omega::multiplayer::IsAllowedMpChar;
using omega::multiplayer::kMpTextCapacity;
using omega::multiplayer::MpMenuActionType;
using omega::multiplayer::MpMenuInput;
using omega::multiplayer::MpMenuState;
using omega::multiplayer::MpMenuStep;
using omega::multiplayer::MpScreen;
using omega::multiplayer::MpTextBackspace;
using omega::multiplayer::MpTextField;
using omega::multiplayer::MpTextFieldKind;
using omega::multiplayer::MpTextInsert;
using omega::multiplayer::StepMpMenu;

int g_failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }
}

[[nodiscard]] constexpr MpMenuState MenuAt(
    const MpScreen screen,
    const std::uint8_t selection) noexcept
{
    MpMenuState state{};
    state.screen = screen;
    state.selection = selection;
    return state;
}

[[nodiscard]] constexpr MpMenuInput Up() noexcept
{
    MpMenuInput input{};
    input.up = true;
    return input;
}

[[nodiscard]] constexpr MpMenuInput Down() noexcept
{
    MpMenuInput input{};
    input.down = true;
    return input;
}

[[nodiscard]] constexpr MpMenuInput Primary() noexcept
{
    MpMenuInput input{};
    input.primary = true;
    return input;
}

[[nodiscard]] constexpr MpMenuInput Cancel() noexcept
{
    MpMenuInput input{};
    input.cancel = true;
    return input;
}

[[nodiscard]] constexpr MpMenuInput Backspace() noexcept
{
    MpMenuInput input{};
    input.backspace = true;
    return input;
}

[[nodiscard]] constexpr MpMenuInput Text(const char symbol) noexcept
{
    MpMenuInput input{};
    input.text_char = symbol;
    return input;
}

[[nodiscard]] MpTextField TypeInto(
    MpTextField field,
    const std::string_view text,
    const MpTextFieldKind kind)
{
    for (const char symbol : text)
        MpTextInsert(field, symbol, kind);
    return field;
}

void TestTextField()
{
    Check(
        IsAllowedMpChar('A', MpTextFieldKind::Name) &&
            IsAllowedMpChar('z', MpTextFieldKind::Name) &&
            IsAllowedMpChar('7', MpTextFieldKind::Name) &&
            IsAllowedMpChar(' ', MpTextFieldKind::Name) &&
            IsAllowedMpChar('-', MpTextFieldKind::Name),
        "name field admits its project display-name characters");
    Check(
        !IsAllowedMpChar(':', MpTextFieldKind::Name) &&
            !IsAllowedMpChar('/', MpTextFieldKind::Name),
        "name field rejects punctuation outside its display-name set");

    Check(
        IsAllowedMpChar('[', MpTextFieldKind::ConnectionTarget) &&
            IsAllowedMpChar('a', MpTextFieldKind::ConnectionTarget) &&
            IsAllowedMpChar(':', MpTextFieldKind::ConnectionTarget) &&
            IsAllowedMpChar('/', MpTextFieldKind::ConnectionTarget),
        "connection target admits visible ASCII without choosing an address grammar");
    Check(
        !IsAllowedMpChar(' ', MpTextFieldKind::ConnectionTarget) &&
            !IsAllowedMpChar('\n', MpTextFieldKind::ConnectionTarget) &&
            !IsAllowedMpChar(
                static_cast<char>(0x80U),
                MpTextFieldKind::ConnectionTarget),
        "connection target rejects whitespace, controls, and non-ASCII bytes");

    const MpTextField name =
        TypeInto(MpTextField{}, "OMEGA HOST", MpTextFieldKind::Name);
    Check(name.view() == "OMEGA HOST", "typing preserves admitted bytes in order");

    const MpTextField target = TypeInto(
        MpTextField{},
        "[2001:db8::1]:33333",
        MpTextFieldKind::ConnectionTarget);
    Check(
        target.view() == "[2001:db8::1]:33333",
        "connection target does not impose an IPv4-only filter");

    MpTextField edit =
        TypeInto(MpTextField{}, "ABC", MpTextFieldKind::Name);
    MpTextBackspace(edit);
    Check(edit.view() == "AB", "backspace removes the final byte");
    MpTextField empty{};
    MpTextBackspace(empty);
    Check(empty.empty(), "backspace on an empty field is inert");

    MpTextField full{};
    for (std::size_t index = 0U; index < kMpTextCapacity + 8U; ++index)
        MpTextInsert(full, 'x', MpTextFieldKind::Name);
    Check(
        full.size() == kMpTextCapacity,
        "field content is clamped at the exact byte capacity");
    Check(
        full.view() == std::string_view(
                           "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                           kMpTextCapacity),
        "a full field retains all capacity bytes without requiring a terminator");
}

void TestNavigationAndTransitions()
{
    MpMenuState root{};
    root = StepMpMenu(root, Down()).state;
    Check(root.selection == 1U, "down advances selection");
    root = StepMpMenu(root, Up()).state;
    Check(root.selection == 0U, "up retreats selection");
    root = StepMpMenu(root, Up()).state;
    Check(root.selection == 0U, "up clamps at the first row");
    for (int index = 0; index < 10; ++index)
        root = StepMpMenu(root, Down()).state;
    Check(root.selection == 3U, "down clamps at the final row");

    MpMenuInput opposed{};
    opposed.up = true;
    opposed.down = true;
    const MpMenuState unmoved =
        StepMpMenu(MenuAt(MpScreen::Root, 2U), opposed).state;
    Check(unmoved.selection == 2U, "opposed navigation inputs cancel");

    Check(
        StepMpMenu(MenuAt(MpScreen::Root, 0U), Primary()).state.screen ==
            MpScreen::HostGame,
        "Root row 0 opens Host Game");
    Check(
        StepMpMenu(MenuAt(MpScreen::Root, 1U), Primary()).state.screen ==
            MpScreen::DirectConnect,
        "Root row 1 opens Direct Connect");
    Check(
        StepMpMenu(MenuAt(MpScreen::Root, 2U), Primary()).state.screen ==
            MpScreen::ServerList,
        "Root row 2 opens Server List");
    Check(
        StepMpMenu(MenuAt(MpScreen::Root, 3U), Primary()).state.exit_requested,
        "Root Back requests exit");

    const MpMenuState clamped =
        StepMpMenu(MenuAt(MpScreen::DirectConnect, 255U), MpMenuInput{}).state;
    Check(clamped.selection == 2U, "stale selection is clamped before use");

    MpMenuState stale_edit = MenuAt(MpScreen::Root, 0U);
    stale_edit.editing = true;
    stale_edit = StepMpMenu(stale_edit, MpMenuInput{}).state;
    Check(!stale_edit.editing, "stale edit focus is cleared");

    Check(
        StepMpMenu(MenuAt(MpScreen::HostGame, 2U), Cancel()).state.screen ==
            MpScreen::Root,
        "cancel returns a sub-screen to Root");
    Check(
        StepMpMenu(MpMenuState{}, Cancel()).state.exit_requested,
        "cancel at Root requests exit");
}

void TestHostGame()
{
    MpMenuState host = MenuAt(MpScreen::HostGame, 0U);
    host = StepMpMenu(host, Primary()).state;
    Check(
        host.host_mode == HostMode::Dedicated,
        "mode row toggles Listen to Dedicated");
    host = StepMpMenu(host, Primary()).state;
    Check(
        host.host_mode == HostMode::Listen,
        "mode row toggles Dedicated to Listen");

    host.selection = 1U;
    host = StepMpMenu(host, Primary()).state;
    Check(host.editing, "primary on the name row enters edit focus");
    host = StepMpMenu(host, Text('H')).state;
    host = StepMpMenu(host, Text('Q')).state;
    Check(host.server_name.view() == "HQ", "focused input edits the server name");
    host = StepMpMenu(host, Down()).state;
    Check(
        host.selection == 1U && host.editing,
        "navigation is ignored while editing");
    host = StepMpMenu(host, Backspace()).state;
    Check(host.server_name.view() == "H", "focused backspace edits the field");
    host = StepMpMenu(host, Primary()).state;
    Check(!host.editing, "primary exits edit focus");

    host.selection = 2U;
    const MpMenuStep started = StepMpMenu(host, Primary());
    Check(
        started.action.type == MpMenuActionType::StartHost &&
            started.action.host_mode == HostMode::Listen &&
            started.action.server_name.view() == "H",
        "Start emits an owned host-menu action snapshot");

    host.server_name =
        TypeInto(host.server_name, "ANGED", MpTextFieldKind::Name);
    Check(
        started.action.server_name.view() == "H",
        "emitted action does not alias later menu-state edits");
}

void TestConnectAndServerList()
{
    MpMenuState connect = MenuAt(MpScreen::DirectConnect, 0U);
    connect = StepMpMenu(connect, Primary()).state;
    for (const char symbol : std::string_view("host.example:33333"))
        connect = StepMpMenu(connect, Text(symbol)).state;
    Check(
        connect.connection_target.view() == "host.example:33333",
        "focused input edits the connection target");
    connect = StepMpMenu(connect, Primary()).state;
    connect.selection = 1U;
    const MpMenuStep connected = StepMpMenu(connect, Primary());
    Check(
        connected.action.type == MpMenuActionType::Connect &&
            connected.action.connection_target.view() == "host.example:33333",
        "Connect emits the typed target without defining its grammar");

    Check(
        StepMpMenu(MenuAt(MpScreen::ServerList, 0U), Primary()).action.type ==
            MpMenuActionType::JoinSelectedServer,
        "server-list Join emits a consumer-neutral selection action");
    Check(
        StepMpMenu(MenuAt(MpScreen::ServerList, 1U), Primary()).action.type ==
            MpMenuActionType::RefreshServerList,
        "server-list Refresh emits a refresh action");
    Check(
        StepMpMenu(MenuAt(MpScreen::ServerList, 2U), Primary()).state.screen ==
            MpScreen::Root,
        "server-list Back returns to Root");
}

void TestInvalidEnumCanonicalization()
{
    MpMenuState unknown_mode = MenuAt(MpScreen::HostGame, 2U);
    unknown_mode.host_mode = static_cast<HostMode>(0xffU);
    const MpMenuStep hosted = StepMpMenu(unknown_mode, Primary());
    Check(
        hosted.state.host_mode == HostMode::Listen &&
            hosted.action.type == MpMenuActionType::StartHost &&
            hosted.action.host_mode == HostMode::Listen,
        "active 0xff host mode canonicalizes to Listen before input dispatch");

    MpMenuState unknown_screen =
        MenuAt(static_cast<MpScreen>(0xffU), 0xffU);
    unknown_screen.host_mode = HostMode::Dedicated;
    unknown_screen.editing = true;
    unknown_screen.server_name =
        TypeInto(MpTextField{}, "PRESERVED", MpTextFieldKind::Name);
    MpMenuInput conflicting{};
    conflicting.down = true;
    conflicting.primary = true;
    conflicting.cancel = true;
    conflicting.backspace = true;
    conflicting.text_char = 'X';
    const MpMenuStep recovered = StepMpMenu(unknown_screen, conflicting);
    Check(
        recovered.state.screen == MpScreen::Root &&
            recovered.state.selection == 0U && !recovered.state.editing &&
            !recovered.state.exit_requested &&
            recovered.state.host_mode == HostMode::Dedicated &&
            recovered.state.server_name.view() == "PRESERVED" &&
            recovered.action.type == MpMenuActionType::None,
        "active 0xff screen resets control state and consumes the input step");
}

void TestInertStates()
{
    const MpMenuState before = MenuAt(MpScreen::HostGame, 1U);
    const MpMenuStep step = StepMpMenu(before, MpMenuInput{});
    Check(
        step.state == before && step.action.type == MpMenuActionType::None,
        "an empty input changes nothing and emits no action");

    MpMenuState exited = MenuAt(MpScreen::Root, 3U);
    exited.exit_requested = true;
    const MpMenuStep terminal = StepMpMenu(exited, Primary());
    Check(
        terminal.state == exited &&
            terminal.action.type == MpMenuActionType::None,
        "an exited menu state is terminal");

    MpMenuState invalid_terminal = exited;
    invalid_terminal.screen = static_cast<MpScreen>(0xffU);
    invalid_terminal.host_mode = static_cast<HostMode>(0xffU);
    invalid_terminal.selection = 0xffU;
    invalid_terminal.editing = true;
    const MpMenuStep untouched = StepMpMenu(invalid_terminal, Cancel());
    Check(
        untouched.state == invalid_terminal &&
            untouched.action.type == MpMenuActionType::None,
        "terminal state precedes 0xff enum canonicalization");
}
} // namespace

int main()
{
    static_assert(
        StepMpMenu(MenuAt(MpScreen::Root, 0U), Primary()).state.screen ==
        MpScreen::HostGame);

    TestTextField();
    TestNavigationAndTransitions();
    TestHostGame();
    TestConnectAndServerList();
    TestInvalidEnumCanonicalization();
    TestInertStates();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " multiplayer menu test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "multiplayer menu tests passed\n";
    return EXIT_SUCCESS;
}
