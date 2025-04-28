# ports/rconpp/portfile.cmake

include(vcpkg_from_github)
include(vcpkg_configure_cmake)
include(vcpkg_build_cmake)
include(vcpkg_install_cmake)
include(vcpkg_copy_pdbs)
include(vcpkg_install_copyright)

# 1) Fetch the v1.0 tag (strips top-level folder)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO           Jaskowicz1/rconpp
    REF            d77993b1e8993701dbf6b2974b41045a915c7b42
    SHA512         f39fad6d8f7cf443e41cde4c40a3cfc5c10546e20770fcf5261d74fb99ca82f342b3b8b5447499326445070556cc9566bc2bc02a813d41cafa3c4e7825c3a8ad
)

# 1.a) The real project is in the "rcon" subdirectory:
# //set(RCON_SOURCE_PATH "${SOURCE_PATH}/rcon")

# 2) Configure & build
vcpkg_configure_cmake(
    SOURCE_PATH    ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS
      -DBUILD_SHARED_LIBS=ON
      -DBUILD_TESTS=OFF
)
vcpkg_build_cmake()

# 3) Upstream `install()` drops:
#      lib/rconpp.lib          ← good
#      include/rconpp/...      ← we’ll re-copy this ourselves
vcpkg_install_cmake()

# 4) Remove any stray include/ folders that were created
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/include"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

# 5) Copy your headers from rcon/include → installed include/
#    This will produce: installed/.../include/rconpp/*.hpp
file(COPY
    "${SOURCE_PATH}/include"
    DESTINATION "${CURRENT_PACKAGES_DIR}"
)

# 6) PDBs & LICENSE
vcpkg_copy_pdbs()
vcpkg_install_copyright(
    FILE_LIST "${SOURCE_PATH}/LICENSE"
)