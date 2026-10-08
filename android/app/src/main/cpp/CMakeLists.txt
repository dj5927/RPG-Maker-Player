cmake_minimum_required(VERSION 3.22.1)
project(rpgmp_libretro_host LANGUAGES CXX)

add_library(rpgmp_libretro_host SHARED libretro_host.cpp)

target_compile_features(rpgmp_libretro_host PRIVATE cxx_std_17)
target_include_directories(rpgmp_libretro_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_library(android-lib android)
find_library(log-lib log)
find_library(dl-lib dl)
find_library(aaudio-lib aaudio)

target_link_libraries(rpgmp_libretro_host
        ${android-lib}
        ${log-lib}
        ${dl-lib}
        ${aaudio-lib})

