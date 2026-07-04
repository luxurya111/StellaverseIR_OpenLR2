# StellaverseIR_OpenLR2

OpenLR2 CustomIR module for [Stellaverse IR](https://ir.stellabms.xyz/).

This repository is published only to explain how the DLL module works and to help others develop similar modules by reference. Production endpoints and encryption headers are **not** provided in this repository.

See [`examples/http_auth.sample.cpp`](examples/http_auth.sample.cpp) for a minimal skeleton: copy it to `http_auth/http_auth.cpp` and implement your own host, paths, and request authentication there.

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

Request authentication and IR endpoint configuration live under `http_auth/` (gitignored, not published). If absent, `src/http_auth_stub.cpp` is linked so the project still builds; network calls will not work until you add a real implementation.

```bash
# Optional: start from the minimal sample, then fill in your IR details
mkdir http_auth
copy examples\http_auth.sample.cpp http_auth\http_auth.cpp

cmake -B ./build --preset windows-vs-x64
cmake --build ./build --preset windows-vs-x64-release
```

For 32-bit builds, use the `windows-vs-x86` / `windows-vs-x86-release` presets.

See GitHub Actions workflow files for a full packaging example.

## CustomIR API

See the [OpenLR2 CustomIR documentation](https://github.com/GOMazk/OpenLR2/blob/master/ExampleIR/README.md).

## Acknowledgements

Project layout, build setup, and CustomIR packaging were inspired by [BokutachiIR_OpenLR2](https://github.com/MatVeiQaaa/BokutachiIR_OpenLR2) by MatVeiQaaa.
