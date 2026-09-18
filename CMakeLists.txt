cmake_minimum_required(VERSION 3.21)

project(platform_h LANGUAGES CXX VERSION 0.1.0)

# --- platform-core: the OS abstraction --------------------------------------
add_library(platform-core STATIC
    src/core/platform.h
    src/core/platform_common.cpp
)
add_library(platform::core ALIAS platform-core)

# The names every existing application links against. Seven repos pin this
# package and acquire it four different ways, and equity-app links the bare
# name, so both spellings outlive the rename.
add_library(platform::platform ALIAS platform-core)
add_library(platform ALIAS platform-core)

target_compile_features(platform-core PUBLIC cxx_std_20)

# Keeps #include "platform.h" resolving for every consumer after the move.
target_include_directories(platform-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")

if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR "The Win32 backend currently requires MSVC.")
    endif()

    enable_language(RC)

    target_sources(platform-core PRIVATE src/core/platform_win.cpp src/core/platform_win_audio.cpp)

    # WIN32_LEAN_AND_MEAN / NOMINMAX are defined by platform_win.cpp itself;
    # defining them here as well would be a macro redefinition.
    target_compile_definitions(platform-core PUBLIC WIN32 _WINDOWS UNICODE _UNICODE)

    # platform_win.cpp still carries #pragma comment(lib, ...) for these; naming
    # them here is what a non-MSVC backend will rely on once the pragmas go.
    target_link_libraries(platform-core PUBLIC
        wininet shell32 comdlg32 shlwapi windowscodecs winhttp comctl32
        ole32 oleaut32 user32 gdi32 advapi32 xaudio2
    )
else()
    message(FATAL_ERROR "platform-h has a Win32 backend only; add src for this platform first.")
endif()

# --- platform-ui: reusable widgets, text views and the chat panel ------------
# Opt in with platform_add_app(... UI). An app that does not ask for it does not
# link it, so the games and viewers keep their current size.
add_library(platform-ui STATIC
    src/ui/ui.h
    src/ui/text_types.h
    src/ui/theme.h
    src/ui/theme.cpp
    src/ui/view_host.h
    src/ui/widgets.h
    src/ui/table_layout.h
    src/ui/table_layout.cpp
    src/ui/syntax.h
    src/ui/syntax.cpp
    src/ui/test_support.h
)
add_library(platform::ui ALIAS platform-ui)

target_link_libraries(platform-ui PUBLIC platform::core)

# Reached as #include "ui/widgets.h", so generic names like widgets.h or theme.h
# cannot collide with an application's own headers.
target_include_directories(platform-ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform_app.cmake")

# Only when built directly; apps that pull this in with FetchContent do not want
# the test target in their build.
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()

    platform_add_app(platform_tests CONSOLE
        SOURCES     tests/platform_tests.cpp
        DESCRIPTION "platform-h unit tests"
        EMBED       tests/data/sample.css
    )

    add_test(NAME platform COMMAND platform_tests)
    set_tests_properties(platform PROPERTIES LABELS platform)

    platform_add_app(platform_ui_tests CONSOLE UI
        SOURCES     tests/ui_tests.cpp
        DESCRIPTION "platform-ui unit tests"
    )

    add_test(NAME platform-ui COMMAND platform_ui_tests)
    set_tests_properties(platform-ui PROPERTIES LABELS platform-ui)
endif()
