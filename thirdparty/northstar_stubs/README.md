# NorthstarStubs

D3D11 and GFSDK stubs for the Northstar dedicated server.

These stubs were originally developed for and are included in [pg9182's docker image](https://github.com/pg9182/northstar-dedicated) to eliminate the need for a real or emulated GPU and the resulting overhead (~1GB of RAM on Linux, multiple GB on Windows, on emulated GPUs). Northstar *unreleased* and later will include these stubs by default.

## Development

### Build

To compile the stubs on Linux, install mingw-w64 (both GCC and Clang work fine) and meson, then run:

```bash
meson setup --cross-file misc/mingw-w64-cross.txt build
meson compile -C build
meson install -C build --destdir destdir
```

### Usage

These stubs should be loaded in place of the original DLLs. On Windows, place in the `bin/x64_retail` directory, overwriting the existing DLLs. On Linux/Wine, put them in the same directory as the game executable, and set the Wine DLL overrides such that d3d11 is native and d3d9 is not present.

The stubs can also be loaded via a launcher from an absolute path with LoadLibraryEx since relative imports will automatically refer to the first DLL loaded with the same name.

If the stubs have loaded correctly, you should see a message in the console like `initializing d3d11 stub for northstar`.

See [here](https://docs.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order) for more information about the DLL search order on Windows.
