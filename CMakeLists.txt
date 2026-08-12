cmake_minimum_required(VERSION 3.20)

# The portable WowEdit core intentionally has no game-engine dependency. The
# existing TrinityCore Studio Vulkan/ImGui host remains buildable through Premake;
# this CMake target gives plugin authors, CI, Linux, macOS, and Windows a standard
# build path for the editor document, tools, menus, import/export, and tests.
project(WowEdit VERSION 0.5.0 LANGUAGES C CXX)

option(WOWEDIT_BUILD_TESTS "Build WowEdit unit and integration tests" ON)
option(WOWEDIT_BUILD_BENCHMARKS "Build the optional performance benchmark" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE WOWEDIT_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/WowEdit/*.cpp")
list(REMOVE_ITEM WOWEDIT_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/WowEdit/main.cpp")

find_package(Threads REQUIRED)

# A minimal vendored zlib build keeps PNG heightmap import/export self-contained.
# It is the copy already used by StormLib; no system package manager is required.
add_library(WowEditZlib STATIC
    third_party/StormLib/src/zlib/adler32.c
    third_party/StormLib/src/zlib/crc32.c
    third_party/StormLib/src/zlib/deflate.c
    third_party/StormLib/src/zlib/inffast.c
    third_party/StormLib/src/zlib/inflate.c
    third_party/StormLib/src/zlib/inftrees.c
    third_party/StormLib/src/zlib/trees.c
    third_party/StormLib/src/zlib/zutil.c)
target_include_directories(WowEditZlib PUBLIC third_party/StormLib/src/zlib)

add_library(WowEditCore STATIC ${WOWEDIT_CORE_SOURCES})
add_library(WowEdit::Core ALIAS WowEditCore)
target_link_libraries(WowEditCore PUBLIC Threads::Threads WowEditZlib)
target_include_directories(WowEditCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/WowEdit"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/StormLib/src/zlib")
target_compile_features(WowEditCore PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(WowEditCore PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(WowEditCore PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(WowEditDemo "WowEdit/main.cpp")
target_link_libraries(WowEditDemo PRIVATE WowEdit::Core)
set_target_properties(WowEditDemo PROPERTIES OUTPUT_NAME "WowEdit")

include(CTest)
if(BUILD_TESTING AND WOWEDIT_BUILD_TESTS)
    add_executable(WowEditUnitTests tests/unit_tests/wowedit_core_tests.cpp)
    target_link_libraries(WowEditUnitTests PRIVATE WowEdit::Core)
    add_test(NAME WowEditUnitTests COMMAND WowEditUnitTests)

    add_executable(WowEditIntegrationTests tests/integration_tests/world_project_tests.cpp)
    target_link_libraries(WowEditIntegrationTests PRIVATE WowEdit::Core)
    add_test(NAME WowEditIntegrationTests COMMAND WowEditIntegrationTests)

    add_executable(EditorCommandStackTests tests/unit_tests/editor_command_stack_tests.cpp)
    target_include_directories(EditorCommandStackTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Editor")
    add_test(NAME EditorCommandStackTests COMMAND EditorCommandStackTests)

    add_executable(SqlImportConverterTests
        tests/unit_tests/sql_import_converter_tests.cpp
        DataEngine/data/SqlImportConverter.cpp
        DataEngine/data/CoreSupport.cpp
        DataEngine/data/DbIntrospect.cpp)
    target_include_directories(SqlImportConverterTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/DataEngine"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm")
    target_compile_features(SqlImportConverterTests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(SqlImportConverterTests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(SqlImportConverterTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME SqlImportConverterTests COMMAND SqlImportConverterTests)

    add_executable(AdtVertexColorTests
        tests/unit_tests/adt_vertex_color_tests.cpp
        RenderEngine/adt/AdtWriter.cpp)
    target_include_directories(AdtVertexColorTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/DataEngine"
        "${CMAKE_CURRENT_SOURCE_DIR}/RenderEngine"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm")
    target_compile_features(AdtVertexColorTests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(AdtVertexColorTests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(AdtVertexColorTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME AdtVertexColorTests COMMAND AdtVertexColorTests)
endif()

if(WOWEDIT_BUILD_BENCHMARKS)
    add_executable(WowEditPerformanceBenchmark tests/performance_tests/world_benchmark.cpp)
    target_link_libraries(WowEditPerformanceBenchmark PRIVATE WowEdit::Core)
endif()

install(TARGETS WowEditCore WowEditDemo
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/WowEdit/resources)
install(DIRECTORY samples/ DESTINATION share/WowEdit/samples)
