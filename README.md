# StellaverseIR_OpenLR2

OpenLR2 CustomIR module for [Stellaverse IR](https://ir.stellabms.xyz/).

For the module to work, a `key.txt` file must be present in the same folder as the `.dll` file.
It contains your Stellaverse IR API key (up to 32 characters).

Put the `.dll` into `LR2files/CustomIRs/StellaverseIR`.
You must create those folders yourself.
In the end, you will have the files placed in the following fashion:

```
LR2files/CustomIRs/StellaverseIR/
  - key.txt
  - StellaverseIR.x64.dll (or StellaverseIR.x86.dll)
  - StellaverseIR.x64.pdb (optional, if you are a developer)
```

`key.txt` is a plain-text file containing only the API key.

Set OpenLR2's display IR (`display_ir`) to `StellaverseIR` if you want Stellaverse leaderboards, ghosts, and the F5 web ranking button.

## Building

Windows only (uses WinHTTP).

Requires CMake 3.23+, Visual Studio 2022, a C++23 toolchain, and `VCPKG_ROOT` pointing at a [vcpkg](https://github.com/microsoft/vcpkg) install.
Dependencies (`nlohmann-json`, `sqlite3`) are pulled via the manifest (`vcpkg.json`). WinHTTP remains a Windows system library.

Request authentication and IR endpoint configuration live under `http_auth/` (not published). If absent, a public stub is linked so the project still builds; network calls will not work until the private implementation is present.

```bash
cmake -B ./build --preset windows-vs-x64
cmake --build ./build --preset windows-vs-x64-release
```

For 32-bit builds, use the `windows-vs-x86` / `windows-vs-x86-release` presets.

See GitHub Actions workflow files for a full packaging example.

## CustomIR API

See the [OpenLR2 CustomIR documentation](https://github.com/GOMazk/OpenLR2/blob/master/ExampleIR/README.md).

## Acknowledgements

Project layout, build setup, and CustomIR packaging were inspired by [BokutachiIR_OpenLR2](https://github.com/MatVeiQaaa/BokutachiIR_OpenLR2) by MatVeiQaaa.
