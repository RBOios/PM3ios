cmake_minimum_required(VERSION 3.21)
project(pm3 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PM3_BUILD_TOOLS "Build pm3_dump and tests (desktop only)" ON)
option(PM3_BUILD_APP   "Build the SDL2 viewer app" ON)

# ---------------------------------------------------------------- core (no external deps)
add_library(pm3core STATIC core/pm3/abp.cpp core/pm3/fpa.cpp core/pm3/image.cpp)
target_include_directories(pm3core PUBLIC core)
if(MSVC)
  target_compile_options(pm3core PRIVATE /W4)
else()
  target_compile_options(pm3core PRIVATE -Wall -Wextra)
endif()

if(PM3_BUILD_TOOLS AND NOT IOS)
  add_executable(pm3_dump tools/pm3_dump.cpp)
  target_link_libraries(pm3_dump PRIVATE pm3core)
  enable_testing()
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE pm3core)
  add_test(NAME core COMMAND test_core)
endif()

# ---------------------------------------------------------------- SDL2 viewer app
if(PM3_BUILD_APP)
  if(IOS)
    include(FetchContent)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.30.9
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL2)
    set(PM3_SDL_LIBS SDL2main SDL2-static)
  else()
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
      set(PM3_SDL_LIBS SDL2::SDL2)
      if(TARGET SDL2::SDL2main)
        list(PREPEND PM3_SDL_LIBS SDL2::SDL2main)
      endif()
    else()
      set(PM3_SDL_LIBS ${SDL2_LIBRARIES})
      include_directories(${SDL2_INCLUDE_DIRS})
    endif()
  endif()

  set(PM3_APP_SRC app/main.cpp)
  if(IOS)
    list(APPEND PM3_APP_SRC app/platform_ios.mm)
    set(PM3_ICONS ${CMAKE_SOURCE_DIR}/ios/Assets.xcassets)
    add_executable(pm3viewer MACOSX_BUNDLE ${PM3_APP_SRC} ${PM3_ICONS})
    set_source_files_properties(${PM3_ICONS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(pm3viewer PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/ios/Info.plist.in
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.PM3ios"
      MACOSX_BUNDLE_BUNDLE_NAME "PM3 Viewer"
      MACOSX_BUNDLE_BUNDLE_VERSION "1"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
      XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
      XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
      XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
      XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
      XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
    target_link_libraries(pm3viewer PRIVATE "-framework Foundation" "-framework UIKit")
  else()
    list(APPEND PM3_APP_SRC app/platform_other.cpp)
    add_executable(pm3viewer ${PM3_APP_SRC})
  endif()
  target_link_libraries(pm3viewer PRIVATE pm3core ${PM3_SDL_LIBS})
endif()
