cmake_minimum_required(VERSION 3.20)
project(Tourelle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the media source loads inside the Frame Server service, and the
# app should run from a plain folder; neither may need a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C)
add_compile_options(/W4 /permissive- /utf-8 /EHsc)

# User-mode UVC driver over WinUSB (video + motors).
add_library(uvc STATIC uvc/UvcDevice.cpp)
target_include_directories(uvc PUBLIC uvc include)
target_link_libraries(uvc PUBLIC winusb cfgmgr32)

# Virtual camera media source, loaded by the Windows Camera Frame Server.
add_library(TourelleCamera SHARED
    vcam/dllmain.cpp vcam/TourelleActivate.cpp vcam/TourelleSource.cpp vcam/TourelleStream.cpp
    vcam/TourelleCamera.def)
target_link_libraries(TourelleCamera PRIVATE uvc mfplat mf mfuuid mfsensorgroup runtimeobject)

# Client side of the virtual camera: registration, lookup, motor commands.
add_library(client STATIC client/TourelleClient.cpp)
target_include_directories(client PUBLIC client include)
target_link_libraries(client PUBLIC mfplat mf mfuuid mfsensorgroup ole32)

# The application: live preview, pan/tilt controls, one-click setup.
add_executable(Tourelle WIN32 app/App.cpp app/Setup.cpp app/Tourelle.manifest)
target_link_libraries(Tourelle PRIVATE client)
target_link_options(Tourelle PRIVATE /MANIFESTUAC:NO)  # execution level comes from Tourelle.manifest

add_executable(tourelle-cli tools/tourelle-cli.cpp)
target_link_libraries(tourelle-cli PRIVATE client)

# Stand-alone streaming and motor test, without the virtual camera.
add_executable(uvcgrab tools/uvcgrab.cpp)
target_link_libraries(uvcgrab PRIVATE uvc)
