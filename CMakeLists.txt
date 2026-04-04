cmake_minimum_required(VERSION 3.25)
project(Saddle VERSION 0.3.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(SOUP REQUIRED IMPORTED_TARGET libsoup-3.0)
find_package(nlohmann_json 3.2 REQUIRED)

# Compile GLib resources into a generated C file
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(RESOURCE_XML "${CMAKE_SOURCE_DIR}/data/saddle.gresource.xml")
set(RESOURCE_C  "${CMAKE_BINARY_DIR}/saddle_resources.c")
add_custom_command(
    OUTPUT "${RESOURCE_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
        "--target=${RESOURCE_C}"
        "--generate-source"
        "--sourcedir=${CMAKE_SOURCE_DIR}/assets/icons/application"
        "${RESOURCE_XML}"
    DEPENDS
        "${RESOURCE_XML}"
        "${CMAKE_SOURCE_DIR}/assets/icons/application/Saddle App Icon 1.png"
    COMMENT "Compiling GLib resources"
)

# Collect sources and include the generated resource file
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(saddle ${SOURCES} "${RESOURCE_C}")

target_include_directories(saddle PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(saddle PRIVATE
    PkgConfig::GTKMM
    PkgConfig::ADWAITA
    PkgConfig::SOUP
    nlohmann_json::nlohmann_json
)

install(TARGETS saddle DESTINATION bin)
install(FILES
    "${CMAKE_SOURCE_DIR}/assets/icons/application/Saddle App Icon 1.png"
    DESTINATION share/icons/hicolor/256x256/apps
    RENAME com.saddle.Saddle.png
)
install(FILES
    "${CMAKE_SOURCE_DIR}/data/com.saddle.Saddle.desktop"
    DESTINATION share/applications
)
