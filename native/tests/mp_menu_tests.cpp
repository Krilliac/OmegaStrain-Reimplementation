#include "omega/multiplayer/mp_menu.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
using omega::multiplayer::HostMode;
using omega::multiplayer::IsAllowedMpChar;
using omega::multiplayer::kMpTextCapacity;
using omega::multiplayer::MpMenuInput;
using omega::multiplayer::MpMenuState;
using omega::multiplayer::MpMenuStep;
using omega::multiplayer::MpScreen;
using omega::multiplayer::MpTextBackspace;
using omega::multiplayer::MpTextField;
using omega::multiplayer::MpTextFieldKind;
using omega::multiplayer::MpTextInsert;
using omega::multiplayer::SessionRequestType;
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

[[nodiscard]] MpTextField TypeInto(MpTextField field, const std::string_view text, const MpTextFieldKind kind)
{
    for (const char symbol : text)
        MpTextInsert(field, symbol, kind);
    return field;
}

void TestTextField()
{
    // Name field admits letters/digits/space/-_. and rejects ':' '/'.
    Check(IsAllowedMpChar('A', MpTextFieldKind::Name) && IsAllowedMpChar('z', MpTextFieldKind::Name) &&
              IsAllowedMpChar('7', MpTextFieldKind::Name) && IsAllowedMpChar(' ', MpTextFieldKind::Name) &&
              IsAllowedMpChar('-', MpTextFieldKind::Name),
          "name field admits letters/digits/space/-");
    Check(!IsAllowedMpChar(':', MpTextFieldKind::Name) && !IsAllowedMpChar('/', MpTextFieldKind::Name),
          "name field rejects ':' and '/'");

    // Address field admits digits/./: and rejects letters/space.
    Check(IsAllowedMpChar('1', MpTextFieldKind::Address) && IsAllowedMpChar('.', MpTextFieldKind::Address) &&
              IsAllowedMpChar(':', MpTextFieldKind::Address),
          "address field admits digits '.' ':'");
    Check(!IsAllowedMpChar('A', MpTextFieldKind::Address) && !IsAllowedMpChar(' ', MpTextFieldKind::Address),
          "address field rejects letters and space");

    const MpTextField name = TypeInto(MpTextField{}, "OMEGA HOST", MpTextFieldKind::Name);
    Check(name.view() == "OMEGA HOST", "typing appends admitted characters in order");

    // Disallowed characters are dropped (not inserted).
    const MpTextField filtered = TypeInto(MpTextField{}, "1.2.3.4:33333", MpTextFieldKind::Address);
    Check(filtered.view() == "1.2.3.4:33333", "address accepts a full IP:port");
    const MpTextField dropped = TypeInto(MpTextField{}, "1a2:", MpTextFieldKind::Address);
    Check(dropped.view() == "12:", "address drops the disallowed letter");

    // Backspace removes the last character; empty backspace is a no-op.
    MpTextField edit = TypeInto(MpTextField{}, "ABC", MpTextFieldKind::Name);
    MpTextBackspace(edit);
    Check(edit.view() == "AB", "backspace removes the last character");
    MpTextField empty{};
    MpTextBackspace(empty);
    Check(empty.length == 0U, "backspace on empty is a no-op");

    // Overflow at the fixed capacity is silently ignored.
    MpTextField overflow{};
    for (std::size_t index = 0U; index < kMpTextCapacity + 8U; ++index)
        MpTextInsert(overflow, 'x', MpTextFieldKind::Name);
    Check(overflow.length == kMpTextCapacity, "text field clamps at the fixed capacity");
}

void TestNavigationAndTransitions()
{
    // Down/up move the selection clamped within the Root screen (4 rows).
    MpMenuState root{};
    root = StepMpMenu(root, MpMenuInput{.down = true}).state;
    Check(root.selection == 1U, "down advances the selection");
    root = StepMpMenu(root, MpMenuInput{.up = true}).state;
    Check(root.selection == 0U, "up retreats the selection");
    // Up at the top clamps.
    root = StepMpMenu(root, MpMenuInput{.up = true}).state;
    Check(root.selection == 0U, "up clamps at the first row");
    // Down past the last row clamps (Root has 4 rows).
    for (int i = 0; i < 10; ++i)
        root = StepMpMenu(root, MpMenuInput{.down = true}).state;
    Check(root.selection == 3U, "down clamps at the last row");

    // Root selections route to the sub-screens.
    Check(StepMpMenu(MpMenuState{.selection = 0U}, MpMenuInput{.primary = true}).state.screen == MpScreen::HostGame,
          "Root row 0 opens Host Game");
    Check(StepMpMenu(MpMenuState{.selection = 1U}, MpMenuInput{.primary = true}).state.screen ==
              MpScreen::DirectConnect,
          "Root row 1 opens Direct Connect");
    Check(StepMpMenu(MpMenuState{.selection = 2U}, MpMenuInput{.primary = true}).state.screen == MpScreen::ServerList,
          "Root row 2 opens Server List");
    Check(StepMpMenu(MpMenuState{.selection = 3U}, MpMenuInput{.primary = true}).state.exit_requested,
          "Root Back requests exit");

    // Cancel backs a sub-screen to Root and Root to exit.
    Check(StepMpMenu(MpMenuState{.screen = MpScreen::HostGame, .selection = 2U}, MpMenuInput{.cancel = true})
                  .state.screen == MpScreen::Root,
          "cancel returns a sub-screen to Root");
    Check(StepMpMenu(MpMenuState{}, MpMenuInput{.cancel = true}).state.exit_requested,
          "cancel at Root requests exit");
}

void TestHostGame()
{
    MpMenuState host{.screen = MpScreen::HostGame, .selection = 0U};
    // Row 0 toggles the host mode.
    host = StepMpMenu(host, MpMenuInput{.primary = true}).state;
    Check(host.host_mode == HostMode::Dedicated, "Host Game row 0 toggles Listen -> Dedicated");
    host = StepMpMenu(host, MpMenuInput{.primary = true}).state;
    Check(host.host_mode == HostMode::Listen, "Host Game row 0 toggles Dedicated -> Listen");

    // Row 1 is the server-name field: primary enters edit focus; typing edits it.
    host.selection = 1U;
    host = StepMpMenu(host, MpMenuInput{.primary = true}).state;
    Check(host.editing, "primary on the name field enters edit focus");
    host = StepMpMenu(host, MpMenuInput{.text_char = 'H'}).state;
    host = StepMpMenu(host, MpMenuInput{.text_char = 'Q'}).state;
    Check(host.server_name.view() == "HQ", "typing while focused edits the server name");
    // Navigation is ignored while editing.
    host = StepMpMenu(host, MpMenuInput{.down = true}).state;
    Check(host.selection == 1U && host.editing, "navigation is ignored while editing");
    host = StepMpMenu(host, MpMenuInput{.backspace = true}).state;
    Check(host.server_name.view() == "H", "backspace edits the focused field");
    // Primary exits focus without dispatching.
    host = StepMpMenu(host, MpMenuInput{.primary = true}).state;
    Check(!host.editing, "primary exits edit focus");

    // Row 2 START dispatches a HostSession request carrying mode + name.
    host.selection = 2U;
    const MpMenuStep started = StepMpMenu(host, MpMenuInput{.primary = true});
    Check(started.request.type == SessionRequestType::HostSession &&
              started.request.host_mode == HostMode::Listen && started.request.server_name.view() == "H",
          "START dispatches HostSession with the host mode and server name");
}

void TestDirectConnectAndServerList()
{
    // Direct Connect: edit the address, then CONNECT dispatches it.
    MpMenuState dc{.screen = MpScreen::DirectConnect, .selection = 0U};
    dc = StepMpMenu(dc, MpMenuInput{.primary = true}).state; // focus address
    for (const char symbol : std::string_view("127.0.0.1:33333"))
        dc = StepMpMenu(dc, MpMenuInput{.text_char = symbol}).state;
    Check(dc.address.view() == "127.0.0.1:33333", "address field accepts the typed IP:port");
    dc = StepMpMenu(dc, MpMenuInput{.primary = true}).state; // exit focus
    dc.selection = 1U;                                       // CONNECT
    const MpMenuStep connected = StepMpMenu(dc, MpMenuInput{.primary = true});
    Check(connected.request.type == SessionRequestType::DirectConnect &&
              connected.request.address.view() == "127.0.0.1:33333",
          "CONNECT dispatches DirectConnect with the typed address");

    // Server List: JOIN and REFRESH dispatch their requests.
    Check(StepMpMenu(MpMenuState{.screen = MpScreen::ServerList, .selection = 0U}, MpMenuInput{.primary = true})
                  .request.type == SessionRequestType::JoinServer,
          "Server List row 0 dispatches JoinServer");
    Check(StepMpMenu(MpMenuState{.screen = MpScreen::ServerList, .selection = 1U}, MpMenuInput{.primary = true})
                  .request.type == SessionRequestType::RefreshServers,
          "Server List row 1 dispatches RefreshServers");
    Check(StepMpMenu(MpMenuState{.screen = MpScreen::ServerList, .selection = 2U}, MpMenuInput{.primary = true})
                  .state.screen == MpScreen::Root,
          "Server List Back returns to Root");
}

void TestNoInputIsInert()
{
    const MpMenuState before{.screen = MpScreen::HostGame, .selection = 1U};
    const MpMenuStep step = StepMpMenu(before, MpMenuInput{});
    Check(step.state == before && step.request.type == SessionRequestType::None,
          "an empty input frame changes nothing and dispatches nothing");
}
} // namespace

int main()
{
    // Compile-time guarantees of the pure step for one representative path.
    static_assert(StepMpMenu(MpMenuState{.selection = 0U}, MpMenuInput{.primary = true}).state.screen ==
                  MpScreen::HostGame);

    TestTextField();
    TestNavigationAndTransitions();
    TestHostGame();
    TestDirectConnectAndServerList();
    TestNoInputIsInert();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " multiplayer menu test(s) failed\n";
        return 1;
    }
    std::cout << "multiplayer menu tests passed\n";
    return 0;
}
