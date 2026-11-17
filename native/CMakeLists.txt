cmake_minimum_required(VERSION 3.20)
project(Tourelle LANGUAGES CXX RC)

option(TOURELLE_WERROR "Treat compiler warnings as errors" ON)
option(TOURELLE_ASAN "Build with AddressSanitizer (tests and tools)" OFF)
option(TOURELLE_ANALYZE "Run the MSVC code analysis (/analyze) during the build" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the media source loads inside the Frame Server service, and the
# app should run from a plain folder; neither may need a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                    $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
if(TOURELLE_WERROR)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/WX>)
endif()
# Windows SDK headers are external: neither warnings nor code analysis apply to them.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/external:anglebrackets> $<$<COMPILE_LANGUAGE:CXX>:/external:W0>)
if(TOURELLE_ANALYZE)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/analyze> $<$<COMPILE_LANGUAGE:CXX>:/analyze:external->)
endif()
if(TOURELLE_ASAN)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address> $<$<COMPILE_LANGUAGE:CXX>:/Zi>)
    add_link_options(/INCREMENTAL:NO /DEBUG)
endif()

# Pure logic, no hardware: descriptor parsing, frame assembly, imaging, motor math, KS helpers.
add_library(core STATIC
    core/UvcDescriptors.cpp core/FrameAssembler.cpp core/Imaging.cpp core/Motor.cpp core/Autofocus.cpp
    core/KsControls.cpp core/DemoPattern.cpp)
target_include_directories(core PUBLIC core)

# User-mode UVC driver over WinUSB (video + motors).
add_library(uvc STATIC uvc/UvcDevice.cpp)
target_include_directories(uvc PUBLIC uvc include)
target_link_libraries(uvc PUBLIC core winusb cfgmgr32)

# Virtual camera media source, loaded by the Windows Camera Frame Server.
add_library(TourelleCamera SHARED
    vcam/dllmain.cpp vcam/TourelleActivate.cpp vcam/TourelleSource.cpp vcam/TourelleStream.cpp vcam/MotorService.cpp
    vcam/TourelleCamera.def)
target_link_libraries(TourelleCamera PRIVATE uvc mfplat mf mfuuid mfsensorgroup runtimeobject)

# Client side of the virtual camera: registration, lookup, motor commands.
add_library(client STATIC client/TourelleClient.cpp)
target_include_directories(client PUBLIC client include)
target_link_libraries(client PUBLIC mfplat mf mfuuid mfsensorgroup ole32)

# The application: live preview, pan/tilt controls, one-click setup.
add_executable(Tourelle WIN32 app/App.cpp app/Setup.cpp app/FaceTracker.cpp app/Gamepad.cpp app/Tourelle.rc app/Tourelle.manifest)
target_include_directories(Tourelle PRIVATE app)
target_link_libraries(Tourelle PRIVATE client core windowsapp advapi32)
target_link_options(Tourelle PRIVATE /MANIFESTUAC:NO)  # execution level comes from Tourelle.manifest

add_executable(tourelle-cli tools/tourelle-cli.cpp)
target_link_libraries(tourelle-cli PRIVATE client)

# Hardware test bench, without the virtual camera.
add_executable(uvcgrab tools/uvcgrab.cpp)
target_link_libraries(uvcgrab PRIVATE uvc)

# Unit tests (no hardware needed).
enable_testing()
add_executable(tourelle-tests
    tests/TestMain.cpp tests/UvcDescriptorsTest.cpp tests/FrameAssemblerTest.cpp tests/CoreMathTest.cpp
    tests/KsControlsTest.cpp tests/StringsTest.cpp tests/DemoPatternTest.cpp)
target_include_directories(tourelle-tests PRIVATE tests app)
target_link_libraries(tourelle-tests PRIVATE core)
add_test(NAME unit COMMAND tourelle-tests)
