cmake_minimum_required(VERSION 3.21)
project(QuirewiseEbook VERSION 0.1.0 LANGUAGES C)
option(QUIREWISE_BUILD_SHARED "Build Quirewise libraries as shared libraries" OFF)
if(QUIREWISE_BUILD_SHARED)
    set(QW_LIBRARY_TYPE SHARED)
else()
    set(QW_LIBRARY_TYPE STATIC)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(NOT TARGET Quirewise::Archive)
    find_package(QuirewiseArchive CONFIG REQUIRED)
endif()
if(NOT TARGET Quirewise::Data)
    find_package(QuirewiseData CONFIG REQUIRED)
endif()
if(NOT TARGET Quirewise::Text)
    find_package(QuirewiseText CONFIG REQUIRED)
endif()
file(GLOB_RECURSE QW_SOURCES CONFIGURE_DEPENDS src/*.c src/*.cpp)
add_library(quirewise_ebook ${QW_LIBRARY_TYPE} ${QW_SOURCES})
add_library(Quirewise::Ebook ALIAS quirewise_ebook)
set_target_properties(quirewise_ebook PROPERTIES EXPORT_NAME Ebook POSITION_INDEPENDENT_CODE ON)
target_compile_features(quirewise_ebook PUBLIC c_std_11)
target_include_directories(quirewise_ebook PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/formats ${CMAKE_CURRENT_SOURCE_DIR}/src/backends)
target_link_libraries(quirewise_ebook PRIVATE Quirewise::Archive Quirewise::Data Quirewise::Text)
install(TARGETS quirewise_ebook EXPORT QuirewiseEbookTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT QuirewiseEbookTargets FILE QuirewiseEbookTargets.cmake NAMESPACE Quirewise:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseEbook")
configure_package_config_file(cmake/QuirewiseEbookConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseEbookConfig.cmake" INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseEbook")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QuirewiseEbookConfigVersion.cmake" VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseEbookConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseEbookConfigVersion.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseEbook")

if(QUIREWISE_BUILD_SHARED AND WIN32)
    set_target_properties(
        quirewise_ebook
        PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(quirewise_ebook_fuzz_smoke tests/ebook_fuzz_smoke.c)
    target_compile_features(quirewise_ebook_fuzz_smoke PRIVATE c_std_11)
    target_include_directories(quirewise_ebook_fuzz_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(quirewise_ebook_fuzz_smoke PRIVATE Quirewise::Ebook)
    add_test(NAME quirewise_ebook_fuzz_smoke
        COMMAND quirewise_ebook_fuzz_smoke)
endif()
