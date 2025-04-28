# ports/rconpp/portfile.cmake

include(vcpkg_from_git)

# 1) Clone latest main
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/Jaskowicz1/rconpp.git"
    REF d77993b1e8993701dbf6b2974b41045a915c7b42
    HEAD_REF main
)

# 2) Configure via CMake
vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS
      -DBUILD_SHARED_LIBS=ON             # enable shared by default
      -DBUILD_TESTS=OFF                  # skip unit tests
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

# 3) Build and install
vcpkg_build_cmake()
vcpkg_install_cmake()

# 4) Copy PDBs on Windows (if any)
vcpkg_copy_pdbs()