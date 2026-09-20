cmake_minimum_required(VERSION 3.20)
project(AlwaysOnTop VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "Always On Top is a Windows-only application.")
endif()

add_executable(AlwaysOnTop WIN32
  AlwaysOnTop.cpp
  AlwaysOnTop.rc
  resource.h
)

target_compile_features(AlwaysOnTop PRIVATE cxx_std_17)
target_compile_definitions(AlwaysOnTop PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(AlwaysOnTop PRIVATE
  user32 shell32 kernel32 gdi32 gdiplus dwmapi uxtheme advapi32 comdlg32
)

if(MSVC)
  target_compile_options(AlwaysOnTop PRIVATE /W4 /permissive- /EHsc)
endif()
