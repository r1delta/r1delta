vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO TsudaKageyu/minhook
    REF c3fcafdc10146beb5919319d0683e44e3c30d537
    SHA512 72eede39e2a0ae5b9024769a9aad3878106526f434268b1cff0c1389a015656905b827449a05a5c16df47aa27cd6e4c28959368b710cc499d8cb9a04c53863a5
    HEAD_REF master
    PATCHES
        r1delta-reliability.patch
)

vcpkg_configure_cmake(
    SOURCE_PATH "${SOURCE_PATH}"
    PREFER_NINJA
)

vcpkg_install_cmake()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
