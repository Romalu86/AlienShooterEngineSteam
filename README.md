# Alien Shooter Steam 1.22 + Retail source project

A Win32/x86 source project focused on compatibility with Alien Shooter Steam 1.22 while also providing a standalone Retail build. 

The repository is intended for preservation, maintenance, study and non-commercial modification. It does **not** include original game data or Steam redistributable binaries.

## Current status

- Compatibility baseline: **Alien Shooter Steam 1.22**.
- Toolset: **MSVC v141**, Win32/x86.
- Visual Studio configurations:
  - **Steam | Win32** — release build with Steamworks support.
  - **Steam Debug | Win32** — debug build with Steamworks support.
  - **Retail | Win32** — release build without Steamworks.
  - **Retail Debug | Win32** — debug build without Steamworks.

- Compiler/runtime layout can differ from the historical executable when a later v141 compiler is used. Functional compatibility is the goal; byte-for-byte executable identity is not required.

## Added branch features

### NVID limit: 8192

The engine-side VID table now supports **8192** entries instead of the legacy **2048** limit.

- The original 2048-entry application layout is preserved for compatibility.
- A host-side table stores the complete 8192-entry range.
- Legacy NVID query encoding is preserved below 2048.
- An extended query encoding is used for NVID values from 2048 through 8191.
- Resource-loading paths reject out-of-range NVID values instead of indexing beyond the table.
- Child/link VID lookup and script-side VID lookup use the expanded table consistently.

### High-FPS UNIT/pathfinding fix

UNIT turning no longer stalls when a very small frame delta makes the integer angular step round to zero.

- Normal frame rates continue through the existing rotation path.
- Only zero-quantized high-FPS turns accumulate fractional angular progress.
- Accumulated progress is converted into the smallest valid rotation step when enough progress has built up.
- The fix is stored outside the original sprite memory layout and does not change serialized game data.

### Retail build without Steamworks

The `Retail` configurations compile with `AS1_WITH_STEAM=0` and do not link or require `steam_api.dll`.

The Retail Store backend keeps the existing script-facing Store API usable offline:

- integer statistics can be set, queried and persisted;
- achievements can be set, queried, cleared and reset;
- offline data is stored as `stats.dat` in the game's configured save directory, normally `Saves`;
- leaderboard calls complete synchronously through a local one-player leaderboard so Steam-oriented scripts do not wait for callbacks that cannot arrive;
- leaderboard score updates use keep-best behavior;
- the player name is taken from the Windows `USERNAME` environment variable, with `Player` as fallback;
- Steam overlay/store activation is a safe no-op in Retail mode.

The `Steam` configurations continue to use the Steam backend and `steam_api.dll`.

## Requirements

- Windows.
- Visual Studio 2022/2026 with **MSVC v141 - VS 2017 C++ x64/x86 build tools**, or Visual Studio 2017 / Build Tools 2017.
- Win32/x86 build support.
- A legally obtained Alien Shooter installation for runtime game data.
- Legacy DirectX runtime providing `d3dx9_43.dll`.
- For Steam builds only: `steam_api.dll` from a legally obtained Steam installation.

## Build

1. Install the **Desktop development with C++** workload.
2. Install **MSVC v141 - VS 2017 C++ x64/x86 build tools** when using Visual Studio 2022/2026.
3. Open `AlienShooter.sln`.
4. Select one of the four Win32 configurations.
5. Build the `AlienShooter` project.

The project intentionally requests `PlatformToolset=v141` without hard-coding a specific `VCToolsVersion`. This lets modern Visual Studio installations use the v141 toolset that is actually installed instead of failing when an unavailable historical compiler folder is requested.

Required x86 import libraries are generated from the checked-in `.def` files into the configuration's intermediate directory:

- `d3dx9_43.lib` is generated for all builds;
- `steam_api.lib` is generated only for Steam builds.

The generator accepts either the Hostx64/x86 or Hostx86/x86 MSVC `lib.exe`, which keeps the project usable across modern Visual Studio installations without checking proprietary import-library binaries into the source tree.

### Output directories

- Steam: `bin\Steam\Win32\`
- Steam Debug: `bin\Steam\Debug\Win32\`
- Retail: `bin\Retail\Win32\`
- Retail Debug: `bin\Retail\Debug\Win32\`

Builds emit `AlienShooter.exe`; the current project settings also emit PDB and MAP diagnostics.

For runtime testing, place the built executable beside a legally obtained copy of the game data. Steam builds additionally require the Steam runtime; Retail builds do not.

## Repository contents

The project contains source code, Visual Studio project files, the required Win32 resource payload and third-party source components listed in `THIRD_PARTY_NOTICES.md`.

Original game data, original executables and Steam redistributable binaries are not part of the source release.

## License

This project is available for **non-commercial use only** under [LICENSE.md](LICENSE.md).

Third-party components remain under their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Game content is not licensed by this repository; see [GAME_CONTENT_NOTICE.md](GAME_CONTENT_NOTICE.md).
