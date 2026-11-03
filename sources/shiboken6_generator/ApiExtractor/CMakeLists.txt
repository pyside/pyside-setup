# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.25)
cmake_policy(VERSION 3.22)

set(CMAKE_AUTOMOC ON)

set(apiextractor_SRC
abstractmetaargument.cpp abstractmetaargument.h
abstractmetabuilder.cpp abstractmetabuilder.h abstractmetabuilder_p.h
abstractmetabuilder_helpers.cpp
abstractmetabuilder_testutil.cpp abstractmetabuilder_testutil.h
abstractmetaenum.cpp abstractmetaenum.h
abstractmetafield.cpp abstractmetafield.h
abstractmetafunction.cpp abstractmetafunction.h
abstractmetalang.cpp abstractmetalang.h abstractmetalang_helpers.h abstractmetalang_typedefs.h
abstractmetatype.cpp abstractmetatype.h
addedfunctionparser.h
addedfunction.cpp addedfunction.h
anystringview_helpers.cpp anystringview_helpers.h
apiextractor.cpp apiextractor.h apiextractorflags.h
apiextractorresult.cpp apiextractorresult.h
arraytypeentry.h
classdocumentation.cpp classdocumentation.h
codesnip.cpp codesnip.h
codesniphelpers.cpp codesniphelpers.h
complextypeentry.h
conditionalstreamreader.cpp conditionalstreamreader.h
configurabletypeentry.h
constantvaluetypeentry.h
containertypeentry.h
customconversion.cpp customconversion.h customconversion_typedefs.h
customtypenentry.h
debughelpers_p.h
dependency.h
documentation.cpp documentation.h
documentation_enums.h
dotview.cpp dotview.h
filecache.cpp filecache.h
enclosingclassmixin.cpp enclosingclassmixin.h
enumtypeentry.h
enumvaluetypeentry.h
exception.h
fileout.cpp fileout.h
flagstypeentry.h
functiontypeentry.h
graph.h
header_paths.h
include.cpp include.h
messages.cpp messages.h
modifications.cpp modifications.h modifications_typedefs.h
namespacetypeentry.h
objecttypeentry.h
optionsparser.cpp optionsparser.h
predefined_templates.cpp predefined_templates.h
primitivetypeentry.h
propertyspec.cpp propertyspec.h
pymethoddefentry.cpp pymethoddefentry.h
pythontypeentry.h
reporthandler.cpp reporthandler.h
smartpointertypeentry.h
sourcelocation.cpp sourcelocation.h
templateargumententry.h
textstream.cpp textstream.h
typedatabase.cpp typedatabase.h typedatabase_p.h typedatabase_typedefs.h
typedefentry.h
typeparser.cpp typeparser.h
typesystem.cpp typesystem.h typesystem_enums.h typesystem_typedefs.h
typesystemparser.cpp typesystemparser_p.h
typesystemparser_checks.cpp typesystemparser_checks.h
usingmember.h
valuetypeentry.h
varargstypeentry.h
voidtypeentry.h
xmlutils.cpp xmlutils.h xmlutils_libxslt.h
# Clang
clangparser/clangbuilder.cpp clangparser/clangbuilder.h
clangparser/clangdebugutils.cpp clangparser/clangdebugutils.h
clangparser/clangparser.cpp clangparser/clangparser.h
clangparser/clangtype.cpp clangparser/clangtype.h
clangparser/clangutils.cpp clangparser/clangutils.h clangparser/clang_typedefs.h
clangparser/compilersupport.cpp clangparser/compilersupport.h
clangparser/triplet.cpp clangparser/triplet.h
# Old parser
parser/codemodel.cpp parser/codemodel.h parser/codemodel_fwd.h parser/codemodel_enums.h
parser/enumvalue.cpp parser/enumvalue.h
parser/typeinfo.cpp parser/typeinfo.h
)

find_package(LibXml2 2.6.32)
find_package(LibXslt 1.1.19)

set(HAS_LIBXSLT 0)
if (LIBXSLT_FOUND AND LIBXML2_FOUND)
    set(HAS_LIBXSLT 1)
endif()

if(NOT HAS_LIBXSLT)
    message(WARNING
            "Doxygen/XSLT documentation modification will not work due to missing dependency (libxslt not found).")
endif()

# Export to parent scope so that generator/CMakeLists.txt gets it
set(DISABLE_DOCSTRINGS ${DISABLE_DOCSTRINGS} PARENT_SCOPE)

add_library(apiextractor STATIC ${apiextractor_SRC})
target_include_directories(apiextractor PRIVATE ${CLANG_INCLUDE_DIRS}
                                                ${CMAKE_CURRENT_SOURCE_DIR}
                                                ${CMAKE_CURRENT_BINARY_DIR}
                                                ${CMAKE_CURRENT_SOURCE_DIR}/parser)
target_include_directories(apiextractor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(apiextractor PUBLIC Qt::Core)
target_link_libraries(apiextractor PRIVATE libclang)

if (HAS_LIBXSLT)
    target_compile_definitions(apiextractor PUBLIC HAVE_LIBXSLT)
    target_sources(apiextractor PRIVATE xmlutils_libxslt.cpp)
    target_include_directories(apiextractor
                               PRIVATE ${LIBXSLT_INCLUDE_DIR} ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(apiextractor
                          PRIVATE ${LIBXSLT_LIBRARIES} ${LIBXML2_LIBRARIES})
endif()

if (NOT DISABLE_DOCSTRINGS)
    target_sources(apiextractor PRIVATE
                   docparser.cpp docparser.h
                   doxygenparser.cpp doxygenparser.h
                   qtdocparser.cpp qtdocparser.h)
endif()

target_compile_definitions(apiextractor
                           PRIVATE CMAKE_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
                           PRIVATE QT_LEAN_HEADERS=1)

if (BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/tests)
    enable_testing()
    add_subdirectory(tests)
endif()
