-- TrinityCore Studio — a multi-editor toolkit for TrinityCore 3.3.5a world data
-- Build files: run `tools\premake5.exe vs2026` from the repo root, then open build\TrinityCoreStudio.slnx

workspace "TrinityCoreStudio"
    configurations { "Debug", "Release" }
    platforms      { "x64" }
    startproject   "TrinityCoreStudio"
    location       "build"

    filter "platforms:x64"
        architecture "x86_64"
    filter {}

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

project "TrinityCoreStudio"
    kind       "ConsoleApp"   -- console kept for dev logs; switch to WindowedApp for release later
    language   "C++"
    cppdialect "C++17"
    location   "build"
    targetdir  "%{wks.location}/../bin/%{cfg.buildcfg}"
    objdir     "%{wks.location}/obj/%{prj.name}/%{cfg.buildcfg}"
    staticruntime "Off"        -- /MD(d): matches GLFW/libmysql DLLs

    files {
        "src/**.h",
        "src/**.hpp",
        "src/**.cpp",
        -- Dear ImGui core
        "third_party/imgui/imgui.cpp",
        "third_party/imgui/imgui_draw.cpp",
        "third_party/imgui/imgui_tables.cpp",
        "third_party/imgui/imgui_widgets.cpp",
        "third_party/imgui/imgui_demo.cpp",
        -- Dear ImGui backends (GLFW + Vulkan)
        "third_party/imgui/backends/imgui_impl_glfw.cpp",
        "third_party/imgui/backends/imgui_impl_vulkan.cpp",
        -- volk: dynamic Vulkan entry-point loader (dlopens vulkan-1.dll at runtime)
        "third_party/volk/volk.c",
    }

    includedirs {
        "src",
        "third_party/imgui",
        "third_party/imgui/backends",
        "third_party/glfw/include",
        "third_party/mysql/include",
        "third_party/json",
        "third_party/StormLib/src",
        "third_party/volk",
        "third_party/vma",
        "third_party/glm",         -- header-only math (glm 1.0.3), for the model viewer
        "$(VULKAN_SDK)/Include",   -- Vulkan headers only; the loader is dlopen'd via volk
    }

    libdirs {
        "third_party/glfw/lib-vc2022",
        "third_party/mysql/lib",
    }

    -- GLFW linked as a DLL (glfw3dll.lib import lib) => no static-CRT mismatch across Debug/Release
    -- No Vulkan loader lib: volk resolves vulkan-1.dll dynamically at runtime.
    links {
        "glfw3dll",
        "libmysql",
        "gdi32",
        "shell32",
        "user32",
        "winhttp",   -- SOAP client (in-game .reload after save)
        "comdlg32",  -- native open/save file dialogs (SQL import/export)
        "ole32",     -- IFileOpenDialog (native folder picker for client-data path)
        "StormLib",  -- MPQ reader (optional client-data features)
    }

    -- GLFW_DLL: link GLFW as a DLL. VK_NO_PROTOTYPES: volk owns all Vulkan entry points
    -- (nothing statically linked). IMGUI_IMPL_VULKAN_USE_VOLK: ImGui's Vulkan backend
    -- routes its calls through volk too. VK_USE_PLATFORM_WIN32_KHR: expose Win32 surface.
    defines { "GLFW_DLL", "VK_NO_PROTOTYPES", "IMGUI_IMPL_VULKAN_USE_VOLK",
              "VK_USE_PLATFORM_WIN32_KHR",
              -- glm: Vulkan clip space (depth 0..1) + allow gtx (quaternion slerp).
              "GLM_FORCE_DEPTH_ZERO_TO_ONE", "GLM_ENABLE_EXPERIMENTAL" }

    -- Copy required runtime DLLs next to the executable after each build
    postbuildcommands {
        '{COPYFILE} "%{wks.location}/../third_party/glfw/lib-vc2022/glfw3.dll" "%{cfg.targetdir}/glfw3.dll"',
        '{COPYFILE} "%{wks.location}/../third_party/mysql/lib/libmysql.dll" "%{cfg.targetdir}/libmysql.dll"',
    }

    filter "system:windows"
        systemversion "latest"
        defines { "WIN32", "_CRT_SECURE_NO_WARNINGS", "NOMINMAX", "WIN32_LEAN_AND_MEAN",
                  "IMGUI_DISABLE_OBSOLETE_FUNCTIONS", "__STORMLIB_NO_STATIC_LINK__" }

    filter "configurations:Debug"
        defines  { "QE_DEBUG" }
        symbols  "On"
        runtime  "Debug"

    filter "configurations:Release"
        defines  { "NDEBUG" }
        optimize "On"
        runtime  "Release"
    filter {}
