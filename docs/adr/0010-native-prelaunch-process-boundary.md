# ADR 0010: Native prelaunch process boundary

- Status: Accepted
- Date: 2026-07-22

## Context

OpenOmega previously exposed configuration-like rows inside the same front-end state machine that
owns profiles, characters, the briefing room, mission selection, and gameplay entry. That made the
zero-data startup surface look like part launcher and part game. It also tied a deliberately tiny
nearest-scaled diagnostic font to product setup tasks that need ordinary readable desktop UI.

The user needs a double-click entry point for selecting an owner-provided extracted NTSC-U data
tree or owned ISO, opting into a gamepad, and starting the native game. Proprietary inputs must
remain outside the repository and diagnostics must not echo their host paths.

## Decision

Windows builds provide two adjacent executables:

- `openomega_launcher.exe` is a small native prelaunch configurator. It owns game-data selection,
  bounded validation, the default-off gamepad preference, and process launch.
- `openomega.exe` is the game. It owns boot media, profiles, character creation and selection,
  briefing and mission selection, simulation, rendering, audio, input, and saves.

The launcher does not link `omega_app_core`, `omega_app_host`, `omega_profiles`,
`omega_gameplay`, or `omega_simulation`. Its reusable configuration code lives in
`omega_launcher_core`; its Windows host uses Direct2D and DirectWrite with system fonts and COM
file dialogs. No retail art, font, executable code, or asset is embedded.

The normal Play action atomically writes the project-owned default configuration profile, then
starts the adjacent `openomega.exe` with an explicit application path and working directory. The
game reopens and validates the selected data source authoritatively. Keyboard and mouse remain the
default input path; gamepad support is opt-in.

Visual Studio makes the launcher the solution startup project while retaining `openomega` as a
separate right-click startup/debug target. Debugging the launcher does not promise automatic child
process attachment; direct game debugging remains available through the game project.

## Consequences

- The initial product surface is narrow and understandable: data source, one real input setting,
  Play, and Quit.
- Profiles, agents, briefing, missions, and gameplay can evolve without acquiring desktop-dialog
  responsibilities.
- DirectWrite provides compact antialiased text without a bundled font or a new third-party text
  dependency. The launcher is therefore Windows-specific for now; the game remains SDL-based.
- Packaging and tests must treat the GUI launcher and console game as distinct PE products.
- Any selected path written to configuration is UTF-8. Windows runtime path construction must
  decode those bytes as UTF-8 rather than through the active ANSI code page.
