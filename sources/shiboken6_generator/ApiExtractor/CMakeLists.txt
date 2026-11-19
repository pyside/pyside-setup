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

qfp_find_package(WrapLibXml2 2.15.2 PROVIDED_TARGETS WrapLibXml2::WrapLibXml2)
qfp_find_package(WrapLibXslt 1.1.19 PROVIDED_TARGETS WrapLibXslt::WrapLibXslt)

set(HAS_LIBXSLT 0)
if(WrapLibXslt_FOUND AND WrapLibXml2_FOUND)
    set(HAS_LIBXSLT 1)

    # The version might be unknown, e.g. for a libxml2 without a readable xmlversion.h. A CPE
    # without a version would match every release, so only add one when the version is known.
    if(WrapLibXml2_VERSION)
        qfp_sbom_compute_cpe(libxml2_cpe
            VENDOR "xmlsoft" PRODUCT "libxml2" VERSION "${WrapLibXml2_VERSION}")
        qfp_find_package_extend_sbom(TARGETS WrapLibXml2::WrapLibXml2
            CPE "${libxml2_cpe}"
        )
    endif()
    if(WrapLibXslt_VERSION)
        qfp_sbom_compute_cpe(libxslt_cpe
            VENDOR "xmlsoft" PRODUCT "libxslt" VERSION "${WrapLibXslt_VERSION}")
        qfp_find_package_extend_sbom(TARGETS WrapLibXslt::WrapLibXslt
            CPE "${libxslt_cpe}"
        )
    endif()
endif()

if(NOT HAS_LIBXSLT)
    message(WARNING
            "Doxygen/XSLT documentation modification will not work due to missing dependency (libxslt not found).")
endif()

# Export to parent scope so that generator/CMakeLists.txt gets it
set(DISABLE_DOCSTRINGS ${DISABLE_DOCSTRINGS} PARENT_SCOPE)

qfp_sbom_get_open_source_tool_license(apiextractor_license_expression)
qfp_add_library(apiextractor
    NO_INSTALL
    SBOM_ARGS
        LICENSE_CONCLUDED_EXPRESSION "${apiextractor_license_expression}"
    FUNCTION_ARGS
        STATIC ${apiextractor_SRC}
)

target_include_directories(apiextractor PRIVATE ${CLANG_INCLUDE_DIRS}
                                                ${CMAKE_CURRENT_SOURCE_DIR}
                                                ${CMAKE_CURRENT_BINARY_DIR}
                                                ${CMAKE_CURRENT_SOURCE_DIR}/parser)
target_include_directories(apiextractor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(apiextractor PUBLIC Qt::Core)
target_link_libraries(apiextractor PRIVATE libclang)

# The Clang package doesn't set Clang_VERSION.
set(libclang_sbom_args "")
if(LLVM_PACKAGE_VERSION)
    qfp_sbom_compute_cpe(libclang_cpe
        VENDOR "llvm" PRODUCT "clang" VERSION "${LLVM_PACKAGE_VERSION}")
    list(APPEND libclang_sbom_args
        PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}"
        CPE "${libclang_cpe}"
    )
endif()
qfp_find_package_extend_sbom(TARGETS libclang
    LICENSE_DECLARED_EXPRESSION "Apache-2.0 WITH LLVM-exception"
    ${libclang_sbom_args}
)

if (HAS_LIBXSLT)
    target_compile_definitions(apiextractor PUBLIC HAVE_LIBXSLT)
    target_sources(apiextractor PRIVATE xmlutils_libxslt.cpp)
    target_link_libraries(apiextractor
                          PRIVATE WrapLibXslt::WrapLibXslt WrapLibXml2::WrapLibXml2)
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
