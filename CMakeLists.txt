cmake_minimum_required(VERSION 3.21)

project(simple-glitch-audio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Link the C runtime statically so users need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The static libraries below are linked into the plugin modules. Only the symbols marked for export,
# such as clap_entry, GetPluginFactory, and the VST 3 module entry points, stay visible.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(FetchContent)
# clap-wrapper v0.17.0. It downloads the CLAP and VST 3 SDKs at the versions it pins.
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_AUV2 FALSE CACHE BOOL "" FORCE)
FetchContent_Declare(
  clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG 806bba0a5f792e3966b99c1561ab9952d3316d7c
)
FetchContent_MakeAvailable(clap-wrapper)

set(PRODUCT_NAME "Simple Glitch")

add_library(simple-glitch-impl STATIC src/plugin.cpp)
target_include_directories(simple-glitch-impl PUBLIC src)
# clapwrapper/vst3.h, for the VST 3 factory information returned in src/plugin.cpp.
target_include_directories(simple-glitch-impl PRIVATE "${clap-wrapper_SOURCE_DIR}/include")
target_compile_definitions(simple-glitch-impl PRIVATE SIMPLE_GLITCH_VERSION="${PROJECT_VERSION}")
target_link_libraries(simple-glitch-impl PUBLIC clap)

make_clapfirst_plugins(
  TARGET_NAME simple-glitch
  IMPL_TARGET simple-glitch-impl
  OUTPUT_NAME "${PRODUCT_NAME}"
  ENTRY_SOURCE src/plugin-entry.cpp
  BUNDLE_IDENTIFIER "jp.allegromoltov.simple-glitch"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  # The VST 3 validator and the registry both expect the bundle folder layout on Windows.
  WINDOWS_FOLDER_VST3 TRUE
  PLUGIN_FORMATS CLAP VST3
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/assets
)

if(NOT MSVC)
  foreach(module IN ITEMS simple-glitch_clap simple-glitch_vst3)
    # The statically linked C++ runtime keeps the plugins independent of the libstdc++ version of the
    # distribution, and stays hidden. --exclude-libs,ALL would also hide the VST 3 module entry points
    # ModuleEntry and ModuleExit, which the VST 3 SDK defines in a static library.
    target_link_options(${module} PRIVATE -static-libstdc++ -static-libgcc
                        -Wl,--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a -Wl,-z,relro,-z,now -Wl,--no-undefined)
  endforeach()
endif()

# Release layout: `cmake --install build --config Release --prefix <dir>`, then zip <dir>.
# The archive carries the license texts of everything linked into the plugins.
# clap-wrapper downloads the SDKs to these places; a layout change in a new clap-wrapper fails here.
set(VST3_SDK_LICENSE "${CMAKE_BINARY_DIR}/cpm/vst3sdk/LICENSE.txt")
set(CLAP_SDK_LICENSE "${clap-wrapper_BINARY_DIR}/cpm/clap/LICENSE")
foreach(license IN ITEMS "${VST3_SDK_LICENSE}" "${CLAP_SDK_LICENSE}")
  if(NOT EXISTS "${license}")
    message(FATAL_ERROR "License file not found: ${license}")
  endif()
endforeach()
# fmt keeps its license in the leading comment of format.h.
file(READ "${clap-wrapper_SOURCE_DIR}/libs/fmt/fmt/format.h" FMT_HEADER)
string(FIND "${FMT_HEADER}" "*/" FMT_COMMENT_END)
string(SUBSTRING "${FMT_HEADER}" 0 ${FMT_COMMENT_END} FMT_LICENSE_TEXT)
string(REGEX REPLACE "^/\\*\n" "" FMT_LICENSE_TEXT "${FMT_LICENSE_TEXT}")
file(WRITE "${CMAKE_BINARY_DIR}/fmt-LICENSE.txt" "${FMT_LICENSE_TEXT}")

# clap-wrapper puts the Windows plugins in per-format folders and the Linux plugins directly in the asset folder.
if(WIN32)
  set(VST3_BUNDLE "${CMAKE_BINARY_DIR}/assets/VST3/${PRODUCT_NAME}.vst3")
else()
  set(VST3_BUNDLE "${CMAKE_BINARY_DIR}/assets/${PRODUCT_NAME}.vst3")
endif()
install(DIRECTORY "${VST3_BUNDLE}" DESTINATION VST3)
install(FILES "$<TARGET_FILE:simple-glitch_clap>" DESTINATION CLAP)
install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
install(FILES packaging/README.txt DESTINATION .)
install(FILES "${VST3_SDK_LICENSE}" DESTINATION THIRD_PARTY_LICENSES RENAME vst3sdk.txt)
install(FILES "${CLAP_SDK_LICENSE}" DESTINATION THIRD_PARTY_LICENSES RENAME clap.txt)
install(FILES "${clap-wrapper_SOURCE_DIR}/LICENSE" DESTINATION THIRD_PARTY_LICENSES RENAME clap-wrapper.txt)
install(FILES "${CMAKE_BINARY_DIR}/fmt-LICENSE.txt" DESTINATION THIRD_PARTY_LICENSES RENAME fmt.txt)

include(CTest)
if(BUILD_TESTING)
  add_executable(glitch-test tests/glitch-test.cpp)
  target_include_directories(glitch-test PRIVATE src)
  add_test(NAME glitch COMMAND glitch-test)
endif()
