if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO GlitchedPolygons/l8w8jwt
    REF ea57a66c4139177ecec5b0a8cfe17e780c653395
    SHA512 b1a10bcea6bcbb77a75e2ff39900dd4274be6c2a2536c77581b5bdafa7e0ab01da7df134482f629539d24c6eec5430911ef302b87f9495d74e4befcaa8275617
    PATCHES
        fix-mbedtls-vcpkg.patch
)

if(NOT EXISTS "${SOURCE_PATH}/lib/chillbuff/CMakeLists.txt")
    vcpkg_from_github(OUT_SOURCE_PATH chillbuff
        REPO GlitchedPolygons/chillbuff
        REF cc6e14db3145d64d23e7d567712f5951324bc70a
        SHA512 06b38307c2563c749d04b128e5d6dbc020982a4fb47a4f35f33ff62932615b3a794f52ce774c5951c8eacefdd3e4419a536685c58edc3c60f49fe52fd6f6c3d6
    )
    file(REMOVE_RECURSE "${SOURCE_PATH}/lib/chillbuff/")
    file(RENAME "${chillbuff}" "${SOURCE_PATH}/lib/chillbuff/")
endif()

if(NOT EXISTS "${SOURCE_PATH}/lib/checknum/include/checknum.h")
    vcpkg_from_github(OUT_SOURCE_PATH checknum
        REPO GlitchedPolygons/checknum
        REF ae65120919222692672c65c43e4ca0a7198680f3
        SHA512 8728ff9bc3491580fc319b0b0e5b27eb526e064207fa3084ca06cbea759f2a4cdc9580c899a351f55d5ce4f03bbd9d29cf6854e4a801e0a581ce08dd0addb6cf
    )
    file(REMOVE_RECURSE "${SOURCE_PATH}/lib/checknum/")
    file(RENAME "${checknum}" "${SOURCE_PATH}/lib/checknum/")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=Off
        -DL8W8JWT_PACKAGE=On
        -DL8W8JWT_ENABLE_EDDSA=Off
)
vcpkg_cmake_build()


# vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/l8w8jwt)

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" 
"The package l8w8jwt provides CMake targets:

    find_package(l8w8jwt CONFIG REQUIRED)
    target_link_libraries(main PRIVATE l8w8jwt)

Or use the library directly:
    target_link_libraries(main PRIVATE l8w8jwt)
")



file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")