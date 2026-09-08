# Alien Shooter Steam 1.22 + Retail source project

Windows source project for Alien Shooter Steam 1.22, intended for preservation, study, maintenance and non-commercial modification.

The repository contains Steam and Retail targets in the same Visual Studio project, each with Release and Debug variants:

- **Steam | Win32** — Steam release build.
- **Steam Debug | Win32** — Steam debug build with debug CRT and PDB generation.
- **Retail | Win32** — off-Steam release build.
- **Retail Debug | Win32** — off-Steam debug build with debug CRT and PDB generation.

The Release configurations keep the short names **Steam** and **Retail** in Visual Studio.

No source files need to be swapped when changing between Steam and Retail.

## Requirements

- Windows
- Visual Studio 2022 or newer (including Visual Studio 2026) with **Desktop development with C++** and the MSVC v143 x86 toolset
- Win32/x86 toolchain
- A legally obtained Alien Shooter installation/game data for runtime content
- Legacy DirectX runtime providing `d3dx9_43.dll`
- For the **Steam** build only: `steam_api.dll` from the legally obtained Steam installation

## Building in Visual Studio

1. Open `AlienShooter.sln`.
2. Select one of the four Win32 configurations:
   - `Steam | Win32`
   - `Steam Debug | Win32`
   - `Retail | Win32`
   - `Retail Debug | Win32`
3. Build the `AlienShooter` project.

On Visual Studio 2026, keep the project on the **v143** toolset unless intentionally retargeting the engine. The build now creates `d3dx9_43.lib` (and `steam_api.lib` for Steam builds) automatically from the checked-in `.def` files. The runtime DLLs are still required when running the executable.

Outputs:

- Steam: `bin\Steam\Win32\AlienShooter.exe`
- Steam Debug: `bin\Steam\Debug\Win32\AlienShooter.exe`
- Retail: `bin\Retail\Win32\AlienShooter.exe`
- Retail Debug: `bin\Retail\Debug\Win32\AlienShooter.exe`

## Steam build

The `Steam` and `Steam Debug` configurations compile with `AS1_WITH_STEAM=1`. The x86 `steam_api.lib` import library is generated automatically from `sources\win\imports\steam_api.def` at build time, so no checked-in binary `.lib` is required. At runtime it uses the original Steam Store path for statistics, achievements, leaderboards and the player's Steam persona name.

The Steam executable requires the Steam runtime/`steam_api.dll` beside the legally obtained game data, matching the normal Steam installation.

## Retail build

The `Retail` and `Retail Debug` configurations compile with `AS1_WITH_STEAM=0` and `AS1_RETAIL_BUILD=1`. They do not link `steam_api.lib` and does not import or require `steam_api.dll`.

Retail preserves the Steam-era numeric Store extern ABI used by the game scripts, but services it locally:

- `SetStat` / `GetStat` work offline.
- Achievements can be set, queried, cleared and reset offline.
- Statistics and achievements persist between runs.
- Steam-era leaderboard calls complete synchronously through a local one-player leaderboard so scripts never wait for Steam callbacks that cannot arrive.
- The Retail player name is taken from the Windows `USERNAME` environment variable, with `Player` as a fallback.
- Steam overlay/store activation is a safe no-op in Retail.

The Retail Store backend is initialized even when the game configuration does not contain a `Steam=` entry. If an existing configuration still contains `Steam=33100`, the Retail executable also continues normally without loading Steam API.

Offline Store data is saved as `stats.dat` inside the game's existing save directory (`Saves` in the normal installation). The engine uses its configured application save path when available and falls back to the root `Saves` directory. No `%LOCALAPPDATA%` folder or additional save hierarchy is created.

## Project/runtime details

The project keeps text `.def` files in `sources\win\imports` and automatically generates the required x86 import libraries into the configuration intermediate directory (`.build`) before linking. This avoids depending on legacy DirectX SDK `.lib` files being installed or checked into Git. The original Steam 1.22 Win32 resource payload is linked from `sources\win\resources\AlienShooter_retail_exact.res` in both configurations.

The checked-in `symbols/AlienShooter.pdb` is a reference symbol file. Debug builds generate their own PDBs in the corresponding `.build` intermediate directories.

For testing, place the selected `AlienShooter.exe` beside a legally obtained copy of the game data. The Steam executable additionally needs the Steam runtime; the Retail executable does not.

## Repository contents

The repository contains source code, Visual Studio project files, the original Steam 1.22 Win32 resource payload, a reference `AlienShooter.pdb` in `symbols/`, and the third-party source components listed in `THIRD_PARTY_NOTICES.md`. Original game data, executables and Steam redistributable binaries are not included.

## License

This project is available for **non-commercial use only** under [LICENSE.md](LICENSE.md). Third-party components remain under their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Game content is not licensed by this repository; see [GAME_CONTENT_NOTICE.md](GAME_CONTENT_NOTICE.md).
