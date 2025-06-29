set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)  # Use static libraries (.lib)
set(VCPKG_PLATFORM_TOOLSET v143)     # Explicitly set for VS 2022

# set(VCPKG_CMAKE_CONFIGURE_OPTIONS ${VCPKG_CMAKE_CONFIGURE_OPTIONS} -DCMAKE_POLICY_VERSION_MINIMUM=3.5)