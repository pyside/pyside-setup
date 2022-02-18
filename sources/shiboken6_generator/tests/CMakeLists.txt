cmake_minimum_required(VERSION 3.25)

# dumpcodemodel depends on apiextractor which is not cross-built.
add_subdirectory(dumpcodemodel)

if (NOT APIEXTRACTOR_DOCSTRINGS_DISABLED)
    add_subdirectory(qtxmltosphinxtest)
endif()

# macOS: Causes a link error related to Python libraries
if(NOT SHIBOKEN_IS_CROSS_BUILD AND NOT APPLE)
    add_subdirectory(libshiboken/pyranges)
endif()
