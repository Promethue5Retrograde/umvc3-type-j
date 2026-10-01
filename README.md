# UMVC3 TYPE J

Native plugin source for the v0.28 public beta of **Jaynalblast Edition**.
This directory provides the exact source of the plugin submitted to Nexus,
plus a standalone build script and release checksums for moderation review.
It is not the complete gameplay patcher or a copy of the game.

## Release

- [Nexus mod page](https://www.nexusmods.com/ultimatemarvelvscapcom3/mods/505)
- Nexus file ID: `1916`, version `0.28-beta`.
- [GameBanana gameplay beta](https://gamebanana.com/mods/722649)
- [Project community](https://www.reddit.com/user/UMVC3_TYPE_J/)

`ZeroConfirmASI.cpp` is extracted without modification from the submitted ZIP.
Its SHA256 is recorded in `RELEASE_HASHES.json`. This source publication does
not replace the submitted file, change gameplay, or claim moderator approval.

## Build

Use Windows x64 and the x64 [w64devkit](https://github.com/skeeto/w64devkit/releases)
toolchain. No third-party C++ source dependency is required by this plugin.

From a w64devkit shell, in this directory:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ -o ZeroConfirm.asi ZeroConfirmASI.cpp -luser32 -lgdi32
```

Or from PowerShell, with the compiler's directory available on PATH:

```powershell
.\build.ps1
```

An explicit compiler path is also supported:

```powershell
.\build.ps1 -CompilerPath 'C:\Tools\w64devkit\bin\g++.exe'
```

The PowerShell script writes `build/ZeroConfirm.asi`, prints the compiler
version and build hash, and does not install or load the plugin. Flags match
the submitted project's build command. Compiler versions and PE timestamps
can affect binary hashes; a fresh build is not promised to be byte-identical
to the already submitted binary. Use the source hash to identify this revision.

## Reviewer Notes

The plugin runs inside `umvc3.exe` through an ASI loader. It implements native
hyper-meter hooks, the Zero character-selection prompts, and the prototype
Doom Hidden Missiles rule. Review-relevant behavior includes:

- `VirtualProtect`, executable trampolines, and in-process game-state writes.
- A `WH_KEYBOARD_LL` hook that handles Enter/F6 in its game-input context and
  forwards other input. The source does not implement a general keystroke log.
- Worker threads monitoring game state and the local debug command file.
- Appending diagnostic messages to `ZeroConfirm.log` beside the game executable.
- Reading and consuming `ZeroConfirm.debug_input` beside the game executable.

The plugin source has no network client, updater, account-credential access,
registry persistence, or shell-command execution. These are source-inspection
observations, not an independent malware-scan certification.

The submitted ZIP also includes ThirteenAG's **Ultimate ASI Loader** as
`dinput8.dll`. Its source and build information are upstream at
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
The loader is not built by this directory. Its MIT license is included in the
mod package and is available upstream. The loader and plugin have separate
hashes in `RELEASE_HASHES.json`.

The reported VirusTotal result was one detection out of 58 engines, by Bkav
Pro (`W32.Malware.61440D75`). That is not a confirmed false positive. Nexus
moderation decides whether to release the quarantined file.

## Credits And Permissions

Prometheu5 Studios: project direction and testing. Jaynalblast: design feedback
and beta testing. Original game and trademarks belong to Capcom/Marvel; this
project is unofficial. Third-party component licenses remain in force.

Source is published for inspection and review. No new open-source license is
granted here; existing mod-page permissions remain unchanged.

Development tools: OpenAI Codex / ChatGPT assisted with code, reverse-engineering
and documentation. No generated image assets are included in this source-only
publication.
