cmake_minimum_required(VERSION 3.21)
project(foo_js_panel CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDK_ROOT "${CMAKE_SOURCE_DIR}/.." CACHE PATH "Directory containing foobar2000/ and pfc/")
get_filename_component(SDK "${SDK_ROOT}" ABSOLUTE)
foreach(_req pfc foobar2000/SDK foobar2000/foobar2000_component_client foobar2000/shared)
  if(NOT EXISTS "${SDK}/${_req}")
    message(FATAL_ERROR "foobar2000 SDK not found: ${SDK}/${_req} is missing (run ./build.sh, or set -DSDK_ROOT)")
  endif()
endforeach()

set(PANELS_UI_SRC "${CMAKE_SOURCE_DIR}/../foo_ui_panels/src" CACHE PATH "foo_ui_panels' src/ (shared gfx::Canvas and its backends)")
get_filename_component(PUI_SRC "${PANELS_UI_SRC}" ABSOLUTE)
if(NOT EXISTS "${PUI_SRC}/gfx/canvas.h")
  message(FATAL_ERROR "foo_ui_panels not found: ${PUI_SRC}/gfx/canvas.h is missing (clone it next to this repo, or set -DPANELS_UI_SRC)")
endif()

set(JSP_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${JSP_GENERATED_DIR}")
if(DEFINED ENV{JSP_VERSION})
  set(JSP_BUNDLE_VERSION "$ENV{JSP_VERSION}")
else()
  execute_process(COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/version.sh" --base
    OUTPUT_VARIABLE JSP_BUNDLE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(WIN32)
  add_compile_definitions(
    UNICODE _UNICODE
    WIN32 _WINDOWS
    _CRT_SECURE_NO_WARNINGS
    NDEBUG
    NOMINMAX)
  add_compile_options(/W2 /wd4996 /GR)
  if(WIN_ARCH STREQUAL "arm64ec")
    add_compile_options(/U__x86_64__ "/FI${CMAKE_SOURCE_DIR}/src/platform/win/arm64ec_compat.h"
      /clang:-fno-builtin)
  endif()

  file(GLOB PFC_SRC "${SDK}/pfc/*.cpp")
  file(GLOB SDK_SRC "${SDK}/foobar2000/SDK/*.cpp")
  list(APPEND SDK_SRC "${SDK}/foobar2000/foobar2000_component_client/component_client.cpp")
  set(PLATFORM_DIR win)
else()
  enable_language(OBJC OBJCXX)
  add_compile_definitions(NDEBUG)
  add_compile_options(-Wno-deprecated-declarations -Wno-unused-parameter -fvisibility=hidden)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(PFC_NAMES SmartStrStr audio_math audio_sample base64 bigmem bit_array bsearch charDownConvert
    cpuid crashWithMessage filehandle filetimetools guid nix-objects other pathUtils printf selftest
    sort splitString2 string-compare string-conv-lite string-lite string_base string_conv
    synchro_nix threads timers unicode-normalize utf8 wildcard)
  set(PFC_SRC "${SDK}/pfc/obj-c.mm")
  foreach(n ${PFC_NAMES})
    list(APPEND PFC_SRC "${SDK}/pfc/${n}.cpp")
  endforeach()
  set(SDK_NAMES abort_callback advconfig album_art app_close_blocker audio_chunk
    audio_chunk_channel_config cfg_var chapterizer commonObjects completion_notify configStore
    config_io_callback config_object console dsp dsp_manager file_cached_impl file_info
    file_info_const_impl file_info_impl file_info_merge file_operation_callback filesystem
    filesystem_helper foosort fsItem guids hasher_md5 image input input_file_type link_resolver
    main_thread_callback mainmenu mem_block_container menu_helpers menu_item menu_manager metadb
    metadb_handle metadb_handle_list output packet_decoder playable_location playback_control
    playlist playlist_loader popup_message preferences_page replaygain replaygain_info service
    tag_processor tag_processor_id3v2 threaded_process titleformat track_property ui ui_element
    utility)
  set(SDK_SRC "${SDK}/foobar2000/SDK/apple-tools.mm" "${SDK}/foobar2000/SDK/commonObjects-Apple.mm"
    "${SDK}/foobar2000/foobar2000_component_client/component_client.cpp"
    "${SDK}/foobar2000/shared/audio_math.cpp" "${SDK}/foobar2000/shared/shared-apple.mm"
    "${SDK}/foobar2000/shared/shared-nix.cpp" "${SDK}/foobar2000/shared/utf8.cpp")
  foreach(n ${SDK_NAMES})
    list(APPEND SDK_SRC "${SDK}/foobar2000/SDK/${n}.cpp")
  endforeach()
  set(PLATFORM_DIR mac)
endif()

add_library(pfc STATIC ${PFC_SRC})
target_include_directories(pfc SYSTEM PUBLIC "${SDK}")
add_library(fb2ksdk STATIC ${SDK_SRC})
target_include_directories(fb2ksdk SYSTEM PUBLIC "${SDK}" "${SDK}/foobar2000")
target_link_libraries(fb2ksdk PUBLIC pfc)
foreach(t pfc fb2ksdk)
  target_compile_options(${t} PRIVATE $<IF:$<BOOL:${WIN32}>,/w,-w>)
endforeach()

if(APPLE)
  foreach(t pfc fb2ksdk)
    target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>)
  endforeach()
endif()

set(QJS_DIR "${CMAKE_SOURCE_DIR}/third_party/quickjs")
add_library(qjs STATIC "${QJS_DIR}/dtoa.c" "${QJS_DIR}/libregexp.c" "${QJS_DIR}/libunicode.c" "${QJS_DIR}/quickjs.c")
target_include_directories(qjs SYSTEM PUBLIC "${QJS_DIR}")
target_compile_definitions(qjs PRIVATE _GNU_SOURCE $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>)
target_compile_options(qjs PRIVATE $<IF:$<BOOL:${WIN32}>,/w,-w>)

file(GLOB COMP_SRC CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/${PLATFORM_DIR}/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/${PLATFORM_DIR}/*.mm")
if(WIN32)
  list(APPEND COMP_SRC "${PUI_SRC}/platform/win/gdi_canvas.cpp")
else()
  list(APPEND COMP_SRC "${PUI_SRC}/platform/mac/mac_canvas.mm")
endif()

if(WIN32)
  if(WIN_ARCH STREQUAL "arm64ec")
    set(JSP_SHARED_LIB shared-ARM64EC.lib)
  elseif(WIN_ARCH STREQUAL "x86")
    set(JSP_SHARED_LIB shared-Win32.lib)
  else()
    set(JSP_SHARED_LIB shared-x64.lib)
  endif()
  add_library(foo_js_panel SHARED ${COMP_SRC})
  set_target_properties(foo_js_panel PROPERTIES PREFIX "" SUFFIX ".dll")
  target_link_libraries(foo_js_panel PRIVATE
    fb2ksdk pfc qjs
    "${SDK}/foobar2000/shared/${JSP_SHARED_LIB}"
    user32 gdi32 shell32 ole32 oleaut32 uuid comctl32 comdlg32 advapi32 shlwapi winmm msimg32 gdiplus uxtheme)
else()
  add_library(foo_js_panel MODULE ${COMP_SRC})
  set_target_properties(foo_js_panel PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION component
    MACOSX_BUNDLE_BUNDLE_NAME foo_js_panel
    MACOSX_BUNDLE_GUI_IDENTIFIER com.santirod.foo-js-panel
    MACOSX_BUNDLE_BUNDLE_VERSION "${JSP_BUNDLE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${JSP_BUNDLE_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/mac/Info.plist.in")
  target_compile_options(foo_js_panel PRIVATE -fobjc-arc)
  target_link_libraries(foo_js_panel PRIVATE fb2ksdk pfc qjs
    "-framework Cocoa" "-framework CoreGraphics" "-framework CoreText" "-framework ImageIO"
    "-framework QuartzCore" "-framework CoreFoundation" "-framework Security")
endif()
add_custom_target(jsp_version
  COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/version.sh" --header "${JSP_GENERATED_DIR}/version_generated.h"
  BYPRODUCTS "${JSP_GENERATED_DIR}/version_generated.h"
  COMMENT "Component version"
  VERBATIM)
add_dependencies(foo_js_panel jsp_version)
target_include_directories(foo_js_panel PRIVATE "${JSP_GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/src" "${PUI_SRC}")
set_target_properties(foo_js_panel PROPERTIES EXPORT_COMPILE_COMMANDS ON)
