cmake_minimum_required(VERSION 3.25)
project(Saddle VERSION 0.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(SOUP REQUIRED IMPORTED_TARGET libsoup-3.0)
find_package(nlohmann_json 3.2 REQUIRED)

# Collect sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(saddle ${SOURCES})

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
