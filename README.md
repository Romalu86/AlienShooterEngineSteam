# Alien Shooter Steam 1.22 source project

A Win32/x86 source project focused on compatibility with the Steam 1.22 release of Alien Shooter.

The repository is intended for preservation, maintenance, study and non-commercial modification. It does **not** include original game data or Steam redistributable binaries.

## Current status

- Target: **Alien Shooter Steam 1.22**.
- Configuration: **Release | Win32**.
- Toolset: **MSVC v141**.

## Requirements

- Windows.
- Visual Studio 2022/2026 with **MSVC v141 - VS 2017 C++ x64/x86 build tools**, or Visual Studio 2017 / Build Tools 2017.
- Win32/x86 build support.
- A legally obtained Alien Shooter Steam installation for runtime game data and `steam_api.dll`.
- Legacy DirectX runtime providing `d3dx9_43.dll`.

## Build

1. Install the **Desktop development with C++** workload.
2. Install the optional **MSVC v141 - VS 2017 C++ x64/x86 build tools** component when using Visual Studio 2022/2026.
3. Open `AlienShooter.sln`.
4. Select `Release | Win32`.
5. Build the `AlienShooter` project.

The project intentionally requests `PlatformToolset=v141` without hard-coding a specific `VCToolsVersion`. This lets modern Visual Studio installations use the v141 toolset that is actually installed instead of failing when an unavailable historical compiler folder is requested.

Required x86 import libraries for `d3dx9_43.dll` and `steam_api.dll` are generated from the checked-in `.def` files, so pre-generated proprietary import libraries are not required.

Release output is written to:

`bin\Release\Win32\`

The Release build produces:

- `AlienShooter.exe`
- `AlienShooter.pdb`
- `AlienShooter.map`

For runtime testing, place the built executable in a legally obtained Alien Shooter Steam installation.

## Repository contents

The project contains source code, Visual Studio project files, the required Win32 resource payload and third-party source components listed in `THIRD_PARTY_NOTICES.md`.

Original game data, original executables and Steam redistributable binaries are not part of the source release.

## License

This project is available for **non-commercial use only** under [LICENSE.md](LICENSE.md).

Third-party components remain under their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Game content is not licensed by this repository; see [GAME_CONTENT_NOTICE.md](GAME_CONTENT_NOTICE.md).
