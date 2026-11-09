# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

set(QT_MAJOR_VERSION 6)

# Locate Java
include(UseJava)
# Find JDK 8.0
find_package(Java 1.8 COMPONENTS Development REQUIRED)
# Find QtJavaHelpers.java
include("${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}/cmake/Qt6/QtJavaHelpers.cmake")

macro(create_and_install_qt_javabindings)

    # create Qt6AndroidBindings.jar from the following {java_sources}
    set(android_main_srcs "${QT6_INSTALL_PREFIX}/src/android/java/src/org/qtproject/qt/android/bindings")
    set(java_sources
        ${android_main_srcs}/QtActivity.java
        ${android_main_srcs}/QtApplication.java
        ${android_main_srcs}/QtService.java
    )
    # set android.jar from the sdk, for compiling the java files into .jar
    # Use ANDROID_API_VERSION from environment if set, otherwise fall back to CMAKE_ANDROID_API
    if(DEFINED ENV{ANDROID_API_VERSION})
        set(ANDROID_SDK_API_LEVEL "$ENV{ANDROID_API_VERSION}")
    else()
        set(ANDROID_SDK_API_LEVEL "android-${CMAKE_ANDROID_API}")
    endif()
    set(sdk_jar_location "${ANDROID_SDK_ROOT}/platforms/${ANDROID_SDK_API_LEVEL}/android.jar")
    if (NOT EXISTS "${sdk_jar_location}")
        message(FATAL_ERROR "Could not locate Android SDK jar for api '${ANDROID_SDK_API_LEVEL}' - ${sdk_jar_location}")
    endif()

    # this variable is accessed by qt_internal_add_jar
    set(QT_ANDROID_JAR ${sdk_jar_location})

    set(qt_jar_location "${QT6_INSTALL_PREFIX}/jar/Qt6Android.jar")
    if (NOT EXISTS "${qt_jar_location}")
        message(FATAL_ERROR "${qt_jar_location} does not exist. Qt6 installation maybe corrupted.")
    endif()

    list(APPEND included_jars ${sdk_jar_location} ${qt_jar_location})

    qt_internal_add_jar(Qt${QT_MAJOR_VERSION}AndroidBindings
        INCLUDE_JARS ${included_jars}
        SOURCES ${java_sources}
    )

    install_jar(Qt${QT_MAJOR_VERSION}AndroidBindings
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/jar"
        COMPONENT Devel
    )

    # install other relevant Android jars from the Qt installation.
    # All the jars would be later packaged together with the Android wheels
    install(DIRECTORY ${QT6_INSTALL_PREFIX}/jar/ DESTINATION lib/jar)
endmacro()
