option(BUILD_TESTS "Build tests." ON)

macro(setup_clang)
    # Find libclang using the environment variables LLVM_INSTALL_DIR,
    # CLANG_INSTALL_DIR using standard cmake.
    # Use CLANG_INCLUDE_DIRS and link to libclang.
    if(DEFINED ENV{LLVM_INSTALL_DIR})
        list(PREPEND CMAKE_PREFIX_PATH "$ENV{LLVM_INSTALL_DIR}")
        list(PREPEND CMAKE_FIND_ROOT_PATH "$ENV{LLVM_INSTALL_DIR}")
    elseif(DEFINED ENV{CLANG_INSTALL_DIR})
        list(PREPEND CMAKE_PREFIX_PATH "$ENV{CLANG_INSTALL_DIR}")
        list(PREPEND CMAKE_FIND_ROOT_PATH "$ENV{CLANG_INSTALL_DIR}")
    endif()

    qfp_find_package(Clang CONFIG REQUIRED PROVIDED_TARGETS libclang)
    # Need to explicitly handle the version check, because the Clang package doesn't.
    if (LLVM_PACKAGE_VERSION AND LLVM_PACKAGE_VERSION VERSION_LESS "9.0")
        message(FATAL_ERROR "You need LLVM version 9.0 or greater to build.")
    endif()

    # CLANG_LIBRARY is read out from the cmake cache to deploy libclang
    get_target_property(CLANG_BUILD_TYPE libclang IMPORTED_CONFIGURATIONS)
    get_target_property(CLANG_LIBRARY_NAME libclang IMPORTED_LOCATION_${CLANG_BUILD_TYPE})
    set(CLANG_LIBRARY "${CLANG_LIBRARY_NAME}" CACHE FILEPATH "libclang")
    message(STATUS "CLANG: ${Clang_DIR}, ${CLANG_LIBRARY} detected")
endmacro()
