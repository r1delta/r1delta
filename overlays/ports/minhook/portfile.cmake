vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO TsudaKageyu/minhook
    REF 9fbd087432700d73fc571118d6a9697a36443d88
    SHA512 3d1f61cfd0b8164cb0a123dde9d1a318216778d5483567808291a05099acb52cef03e57d03cd584cf7eb8c4e8c3e8db54c421f8f4cc9defed45103e4cb8ff60c
    HEAD_REF master
)

vcpkg_download_distfile(
    CMAKE_SUPPORT_PATCH
    URLS https://github.com/TsudaKageyu/minhook/commit/3f2e34976c1685ee372a09f54c0c8c8f4240ef90.patch
    FILENAME minhook-cmake-support.patch
    SHA512 be44301a32e5c439830ae50774819ddd48977d261b7b65fd22ae742eb3abcdf0f56623abb7e4fe400540fdec5f90c9fa5664d1d634b5303d33097b6e1f93b06d
)

vcpkg_apply_patches(
    SOURCE_PATH "${SOURCE_PATH}"
    PATCHES "${CMAKE_SUPPORT_PATCH}"
)

vcpkg_configure_cmake(
    SOURCE_PATH "${SOURCE_PATH}"
    PREFER_NINJA
)

vcpkg_install_cmake()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
