
cmake_minimum_required(VERSION 3.17)

project(Hastables VERSION 1.0 DESCRIPTION "Hashtables" LANGUAGES CXX)

add_executable(Hashtables "code/main.cpp")
set_target_properties(Hashtables PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_definitions(Hashtables PRIVATE _HAS_EXCEPTIONS=0 WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(Hashtables PRIVATE /MP /W4 /WX /GR- /GS- /EHa- /wd4201 /wd4840 /wd4100 /fp:fast /arch:AVX2)
else()
    target_compile_options(Hashtables PRIVATE -mavx2 -Wall -Wextra -Werror -Wno-missing-braces -Wno-reorder -ffast-math -fno-finite-math-only -fno-exceptions -fno-rtti -Wno-unused-parameter)
endif()
