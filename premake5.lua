-- TrinityCore Studio — a multi-editor toolkit for TrinityCore 3.3.5a world data
-- Build files: run `tools\premake5.exe vs2026` from the repo root, then open build\TrinityCoreStudio.slnx
--
-- The tree is laid out as three build targets, one repo-root folder each (see AGENTS.md "Map"):
--   DataEngine/   (StaticLib) — GPU-free / GLFW-free / ImGui-free reusable lower layers
--                              (util, schema, db, data, clientdata).
--   RenderEngine/ (StaticLib) — Vulkan + GLFW rendering/asset engine, also ImGui-free
--                              (gfx, model, wmo, adt, viewer, platform). Depends on DataEngine.
--   Editor/       (exe)       — the editor application (app, editors, ui, net, main). Owns ImGui;
--                              depends on RenderEngine + DataEngine.
-- StormLib (StaticLib)       — vendored read-only MPQ access, unchanged.
--
-- The include paths are SCOPED per project so the engine/editor boundary is compile-time enforced:
-- a project can only reach the layers it may depend on (e.g. an engine .cpp that #includes "imgui.h"
-- fails to compile — imgui headers are not on the engine's include path).

workspace "TrinityCoreStudio"
    configurations { "Debug", "Release" }
    platforms      { "x64" }
    startproject   "TrinityCoreStudio"
    location       "build"

    filter "platforms:x64"
        architecture "x86_64"
    filter {}

-- Include paths per project (source roots + only the third_party a layer is allowed to see).
-- DataEngine: no Vulkan/GLFW/ImGui. RenderEngine: Vulkan+GLFW but NO ImGui. Editor: everything.
local DATA_INCLUDES = {
    "DataEngine",
    "third_party/StormLib/src", "third_party/mysql/include", "third_party/json", "third_party/glm",
}
local RENDER_INCLUDES = {
    "DataEngine", "RenderEngine",
    "third_party/StormLib/src", "third_party/mysql/include", "third_party/json", "third_party/glm",
    "third_party/glfw/include", "third_party/volk", "third_party/vma", "$(VULKAN_SDK)/Include",
}
local EDITOR_INCLUDES = {
    "DataEngine", "RenderEngine", "Editor",
    "third_party/imgui", "third_party/imgui/backends",
    "third_party/imgui-node-editor", "third_party/imguizmo",
    "third_party/glfw/include", "third_party/mysql/include", "third_party/json",
    "third_party/StormLib/src", "third_party/volk", "third_party/vma", "third_party/glm",
    "$(VULKAN_SDK)/Include",
}

-- GLFW_DLL: link GLFW as a DLL. VK_NO_PROTOTYPES: volk owns all Vulkan entry points. VK_USE_PLATFORM_
-- WIN32_KHR: Win32 surface. IMGUI_IMPL_VULKAN_USE_VOLK: ImGui's Vulkan backend routes through volk.
-- These are inert where the matching headers aren't on a project's include path; the include-dir
-- scoping above is what enforces the layer boundary.
local COMMON_DEFINES = {
    "GLFW_DLL", "VK_NO_PROTOTYPES", "IMGUI_IMPL_VULKAN_USE_VOLK",
    "VK_USE_PLATFORM_WIN32_KHR",
    "GLM_FORCE_DEPTH_ZERO_TO_ONE", "GLM_ENABLE_EXPERIMENTAL",   -- Vulkan clip space + gtx (quat slerp)
}

local WINDOWS_DEFINES = {
    "WIN32", "_CRT_SECURE_NO_WARNINGS", "NOMINMAX", "WIN32_LEAN_AND_MEAN",
    "IMGUI_DISABLE_OBSOLETE_FUNCTIONS", "__STORMLIB_NO_STATIC_LINK__",
}

-- Settings shared by DataEngine / RenderEngine / TrinityCoreStudio (NOT StormLib). Call inside each
-- project block. `includes` is the per-project scoped include-dir list.
local function applyCommon(includes)
    language      "C++"
    cppdialect    "C++17"
    location      "build"
    targetdir     "%{wks.location}/../bin/%{cfg.buildcfg}"
    objdir        "%{wks.location}/obj/%{prj.name}/%{cfg.buildcfg}"
    staticruntime "Off"        -- /MD(d): matches GLFW/libmysql DLLs

    includedirs(includes)
    defines(COMMON_DEFINES)

    filter "system:windows"
        systemversion "latest"
        defines(WINDOWS_DEFINES)
    filter "configurations:Debug"
        defines  { "QE_DEBUG" }
        symbols  "On"
        runtime  "Debug"
    filter "configurations:Release"
        defines  { "NDEBUG" }
        optimize "On"
        runtime  "Release"
    filter {}
end

-- StormLib: read-only MPQ access (optional client-data features). Self-contained
-- (bundles zlib/bzip2/libtomcrypt/etc.). Built as a static lib and linked in.
project "StormLib"
    kind       "StaticLib"
    language   "C++"
    location   "build"
    targetdir  "%{wks.location}/../bin/%{cfg.buildcfg}"
    objdir     "%{wks.location}/obj/%{prj.name}/%{cfg.buildcfg}"
    staticruntime "Off"
    warnings   "Off"

    files {
        "third_party/StormLib/src/**.cpp",
        "third_party/StormLib/src/**.c",
        "third_party/StormLib/src/**.h",
    }
    removefiles {
        "third_party/StormLib/src/wdk/**",   -- Windows Driver Kit variant, not needed
    }
    includedirs { "third_party/StormLib/src" }

    filter "system:windows"
        systemversion "latest"
        defines { "WIN32", "_LIB", "_CRT_SECURE_NO_WARNINGS", "_CRT_NONSTDC_NO_DEPRECATE",
                  "STORMLIB_NO_AUTO_LINK", "_7ZIP_ST" }
    filter "configurations:Debug"
        symbols "On"
        runtime "Debug"
    filter "configurations:Release"
        optimize "On"
        runtime "Release"
    filter {}

-- DataEngine: reusable, GPU-free lower layers. No Vulkan/GLFW/ImGui on its include path.
project "DataEngine"
    kind "StaticLib"
    applyCommon(DATA_INCLUDES)

    files {
        "DataEngine/**.h", "DataEngine/**.hpp", "DataEngine/**.cpp",
    }

    links { "StormLib" }

-- RenderEngine: Vulkan + GLFW rendering/asset engine, ImGui-free. Depends on DataEngine.
project "RenderEngine"
    kind "StaticLib"
    applyCommon(RENDER_INCLUDES)

    files {
        "RenderEngine/**.h", "RenderEngine/**.hpp", "RenderEngine/**.cpp",
        -- volk: dynamic Vulkan entry-point loader (dlopens vulkan-1.dll at runtime)
        "third_party/volk/volk.c",
    }

    links { "DataEngine" }

project "TrinityCoreStudio"
    kind "ConsoleApp"   -- console kept for dev logs; switch to WindowedApp for release later
    applyCommon(EDITOR_INCLUDES)

    files {
        "Editor/**.h", "Editor/**.hpp", "Editor/**.cpp",
        -- Dear ImGui core (the Editor owns the UI toolkit; the engine is ImGui-free)
        "third_party/imgui/imgui.cpp",
        "third_party/imgui/imgui_draw.cpp",
        "third_party/imgui/imgui_tables.cpp",
        "third_party/imgui/imgui_widgets.cpp",
        "third_party/imgui/imgui_demo.cpp",
        -- Dear ImGui backends (GLFW platform + Vulkan renderer), driven by Editor/app/App.cpp
        "third_party/imgui/backends/imgui_impl_glfw.cpp",
        "third_party/imgui/backends/imgui_impl_vulkan.cpp",
        -- imgui-node-editor (thedmd) — draws via ImDrawList, no renderer changes
        "third_party/imgui-node-editor/crude_json.cpp",
        "third_party/imgui-node-editor/imgui_canvas.cpp",
        "third_party/imgui-node-editor/imgui_node_editor.cpp",
        "third_party/imgui-node-editor/imgui_node_editor_api.cpp",
        -- ImGuizmo (CedricGuillemet) — 3D transform gizmo, also ImDrawList-only
        "third_party/imguizmo/ImGuizmo.cpp",
    }

    libdirs {
        "third_party/glfw/lib-vc2022",
        "third_party/mysql/lib",
    }

    -- GLFW linked as a DLL (glfw3dll.lib import lib) => no static-CRT mismatch across Debug/Release
    -- No Vulkan loader lib: volk resolves vulkan-1.dll dynamically at runtime.
    links {
        "RenderEngine",
        "DataEngine",
        "StormLib",  -- MPQ reader (optional client-data features)
        "glfw3dll",
        "libmysql",
        "gdi32",
        "shell32",
        "user32",
        "winhttp",   -- SOAP client (in-game .reload after save)
        "comdlg32",  -- native open/save file dialogs (SQL import/export)
        "ole32",     -- IFileOpenDialog (native folder picker for client-data path)
    }

    -- Copy required runtime DLLs next to the executable after each build
    postbuildcommands {
        '{COPYFILE} "%{wks.location}/../third_party/glfw/lib-vc2022/glfw3.dll" "%{cfg.targetdir}/glfw3.dll"',
        '{COPYFILE} "%{wks.location}/../third_party/mysql/lib/libmysql.dll" "%{cfg.targetdir}/libmysql.dll"',
    }
