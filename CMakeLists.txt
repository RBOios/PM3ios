cmake_minimum_required(VERSION 3.21)
project(pm3 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
  set(CMAKE_OBJCXX_STANDARD 17)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

option(PM3_BUILD_TOOLS "Build pm3_dump and tests (desktop only)" ON)
option(PM3_BUILD_NATIVE_APP "Build the native iOS app (UIKit + Metal, runs PM3WIN.EXE through the x86 host)" ON)

# ---------------------------------------------------------------- core (no external deps)
add_library(pm3core STATIC core/pm3/abp.cpp core/pm3/fpa.cpp core/pm3/image.cpp core/pm3/wpc.cpp core/pm3/wmp.cpp core/pm3/evx.cpp core/pm3/scene.cpp core/pm3/wbd.cpp core/pm3/tfn.cpp core/pm3/wav.cpp core/pm3/x86.cpp core/pm3/pe.cpp core/pm3/x86host.cpp core/pm3/vfs.cpp core/pm3/display.cpp core/pm3/touch.cpp core/pm3/bridge.cpp)
target_include_directories(pm3core PUBLIC core)
find_package(Threads REQUIRED)
target_link_libraries(pm3core PUBLIC Threads::Threads)   # bridge game worker thread
if(MSVC)
  target_compile_options(pm3core PRIVATE /W4)
else()
  target_compile_options(pm3core PRIVATE -Wall -Wextra)
endif()

if(PM3_BUILD_TOOLS AND NOT IOS)
  add_executable(pm3_dump tools/pm3_dump.cpp)
  target_link_libraries(pm3_dump PRIVATE pm3core)
  add_executable(evx_dump tools/evx_dump.cpp)
  target_link_libraries(evx_dump PRIVATE pm3core)
  add_executable(wmp_dump tools/wmp_dump.cpp)
  target_link_libraries(wmp_dump PRIVATE pm3core)
  add_executable(x86_test tools/x86_test.cpp)
  target_link_libraries(x86_test PRIVATE pm3core)
  add_executable(x86_boot tools/x86_boot.cpp)
  target_link_libraries(x86_boot PRIVATE pm3core)
  add_executable(pm3_headless tools/pm3_headless.cpp)
  target_link_libraries(pm3_headless PRIVATE pm3core)
  add_executable(pm3_tapdrive tools/pm3_tapdrive.cpp)
  target_link_libraries(pm3_tapdrive PRIVATE pm3core)
  enable_testing()
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE pm3core)
  add_test(NAME core COMMAND test_core)
endif()

# ---------------------------------------------------------------- native iOS app (UIKit + Metal, no SDL)
if(IOS AND PM3_BUILD_NATIVE_APP)
  set(PM3_NATIVE_SRC ios_native/AppDelegate.mm ios_native/PM3MetalView.mm ios_native/PM3Audio.mm)
  set(PM3_NATIVE_ICONS ${CMAKE_SOURCE_DIR}/ios/Assets.xcassets)
  add_executable(pm3native MACOSX_BUNDLE ${PM3_NATIVE_SRC} ${PM3_NATIVE_ICONS})
  set_source_files_properties(${PM3_NATIVE_ICONS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_include_directories(pm3native PRIVATE ios_native)
  target_compile_options(pm3native PRIVATE -fobjc-arc)
  set_target_properties(pm3native PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/ios/Info_native.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.PM3ios.native"
    MACOSX_BUNDLE_BUNDLE_NAME "PM3"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
  # CoreGraphics must be linked explicitly: CGRectInset (used by AppDelegate.mm) is an out-of-line function and UIKit no longer pulls it in
  # (first CI link failed with: Undefined symbols "_CGRectInset" referenced from -[PM3ViewController showMessage:]).
  target_link_libraries(pm3native PRIVATE pm3core "-framework Foundation" "-framework UIKit" "-framework CoreGraphics" "-framework AVFoundation" "-framework AudioToolbox" "-framework Metal" "-framework MetalKit" "-framework QuartzCore")
endif()
