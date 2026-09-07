# Alien Shooter Steam 1.22 source project

Windows source project for Alien Shooter Steam 1.22, intended for preservation, study, maintenance and non-commercial modification.

## Requirements

- Windows
- Visual Studio 2022 with **Desktop development with C++**
- Win32/x86 toolchain
- A legally obtained Steam installation of Alien Shooter for runtime game data and `steam_api.dll`
- Legacy DirectX runtime providing `d3dx9_43.dll`

## Build

1. Open `AlienShooter.sln`.
2. Select `Release | Win32`.
3. Build the `AlienShooter` project.

The project uses the checked-in x86 import libraries in `sources\win\imports`, matching the established working Win32 build setup. The original Steam 1.22 Win32 resource set is linked from `sources\win\resources\AlienShooter_retail_exact.res`. The resource payload has been checked against the original Steam 1.22 executable by resource type, identifier, language, size and content hash. No replacement icon, bitmap or generated resource script is used.

The Release output directory is:

`bin\Release\Win32\`

It contains only:

`AlienShooter.exe`

The checked-in `symbols/AlienShooter.pdb` is a reference symbol file and is not written to the Release output directory.

For testing, place `AlienShooter.exe` in a legally obtained Alien Shooter Steam installation so the game data and Steam runtime are available.

## Repository contents

The repository contains source code, project files, the original Steam 1.22 Win32 resource payload, a reference `AlienShooter.pdb` symbol file in `symbols/`, and the third-party source components listed in `THIRD_PARTY_NOTICES.md`. Original game data, executables and Steam redistributable binaries are not included.

## License

This project is available for **non-commercial use only** under [LICENSE.md](LICENSE.md). Third-party components remain under their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Game content is not licensed by this repository; see [GAME_CONTENT_NOTICE.md](GAME_CONTENT_NOTICE.md).
