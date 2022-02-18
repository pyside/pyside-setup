cmake_minimum_required(VERSION 3.16)

project(tst_pyranges)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core)
find_package(Qt6 COMPONENTS Test)
find_package(Python COMPONENTS Interpreter Development REQUIRED)

set(tst_pyranges_SRC
    tst_pyranges.cpp)

add_executable(tst_pyranges ${tst_pyranges_SRC})

set(libshiboken_relpath "../../../../shiboken6/libshiboken")

target_include_directories(tst_pyranges PRIVATE
                           "${CMAKE_CURRENT_LIST_DIR}/${libshiboken_relpath}"
                           "${CMAKE_CURRENT_BINARY_DIR}/${libshiboken_relpath}"
                           ${Python_INCLUDE_DIRS})

target_link_libraries(tst_pyranges PRIVATE Qt::Core Qt::Test ${Python_LIBRARIES})

add_test(NAME "tst_pyranges" COMMAND tst_pyranges)

if(WIN32)
    # The Python DLL is not necessarily in PATH (per-user installation, virtual
    # environment); a missing DLL shows a modal error box instead of failing.
    foreach(dir IN LISTS Python_RUNTIME_LIBRARY_DIRS)
        set_property(TEST tst_pyranges APPEND PROPERTY ENVIRONMENT_MODIFICATION
                     "PATH=path_list_prepend:${dir}")
    endforeach()
endif()

if (INSTALL_TESTS)
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/tst_pyranges
            DESTINATION ${TEST_INSTALL_DIR})
endif()
