cmake_minimum_required(VERSION 3.23)
project(DglabBorealisPreview LANGUAGES C CXX)
if(NOT APPLE)
    message(FATAL_ERROR "This optional raster preview uses macOS CoreGraphics; normal UI tests are portable.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(BRLS "${ROOT}/third_party/borealis/library")
add_subdirectory("${BRLS}/lib/extern/fmt" fmt EXCLUDE_FROM_ALL)
add_subdirectory("${BRLS}/lib/extern/tweeny" tweeny EXCLUDE_FROM_ALL)
add_subdirectory("${BRLS}/lib/extern/yoga/yoga" yoga EXCLUDE_FROM_ALL)
file(GLOB_RECURSE FRAMEWORK CONFIGURE_DEPENDS "${BRLS}/lib/core/*.cpp" "${BRLS}/lib/views/*.cpp")
list(FILTER FRAMEWORK EXCLUDE REGEX "/(platform|notification_manager)\\.cpp$")
file(GLOB_RECURSE RETRO CONFIGURE_DEPENDS
    "${BRLS}/lib/extern/libretro-common/compat/*.c"
    "${BRLS}/lib/extern/libretro-common/encodings/*.c"
    "${BRLS}/lib/extern/libretro-common/features/*.c")
add_executable(preview preview.cpp ${FRAMEWORK} ${RETRO}
    "${BRLS}/lib/extern/tinyxml2/tinyxml2.cpp" "${BRLS}/lib/extern/glad/glad.c"
    "${BRLS}/lib/extern/nanovg/nanovg.c" "${ROOT}/nro/source/borealis/gameplay.cpp"
    "${ROOT}/nro/source/borealis/about.cpp" "${ROOT}/nro/source/util/json.c")
foreach(source screen canvas page list button text theme strings language qr about menu)
    target_sources(preview PRIVATE "${ROOT}/nro/source/ui/${source}.c")
endforeach()
target_include_directories(preview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../hostshim"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../net/hostshim" "${ROOT}/nro/include" "${ROOT}/common/include"
    "${BRLS}/include" "${BRLS}/include/borealis/extern" "${BRLS}/include/borealis/extern/tinyxml2" "${BRLS}/include/borealis/extern/nanovg")
target_compile_definitions(preview PRIVATE DGLAB_BOREALIS_MINIMAL YG_ENABLE_EVENTS
    BRLS_RESOURCES="${ROOT}/third_party/borealis/resources/")
target_compile_options(preview PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(preview PRIVATE fmt tweeny yogacore "-framework CoreGraphics" "-framework ImageIO" "-framework CoreFoundation")
