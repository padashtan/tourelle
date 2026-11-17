cmake_minimum_required(VERSION 3.20)
project(TourelleNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the media source loads inside the Frame Server service, which
# must not depend on a VC++ redistributable being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C)
add_compile_options(/W4 /permissive- /utf-8 /EHsc)

add_library(uvc STATIC uvc/UvcDevice.cpp)
target_include_directories(uvc PUBLIC uvc include)
target_link_libraries(uvc PUBLIC winusb cfgmgr32)

add_executable(uvcgrab tools/uvcgrab.cpp)
target_link_libraries(uvcgrab PRIVATE uvc)

add_library(TourelleCamera SHARED
    vcam/dllmain.cpp vcam/TourelleActivate.cpp vcam/TourelleSource.cpp vcam/TourelleStream.cpp
    vcam/TourelleCamera.def)
target_link_libraries(TourelleCamera PRIVATE uvc mfplat mf mfuuid mfsensorgroup runtimeobject)

add_executable(tourelle tools/tourelle.cpp)
target_include_directories(tourelle PRIVATE include)
target_link_libraries(tourelle PRIVATE mfplat mf mfuuid mfsensorgroup ole32)
