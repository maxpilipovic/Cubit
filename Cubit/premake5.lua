-- The engine. It knows nothing of the harness or the game: no path below
-- names Sandbox/ or game/, which is what lets the game leave for its own
-- repository without the engine noticing. See docs/engine-roadmap.md, item B8.
group "Core"

project "Cubit"
    location "."
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    pchheader "cub.h"
    pchsource "src/cub.cpp"

    files
    {
        "include/**.h",
        "src/**.h",
        "src/**.cpp"
    }

    includedirs
    {
        "include",
        "src",
        "../vendor/GLFW/include",
        "../vendor/GLAD/include",
        "../vendor/GLM",
        "../vendor/ENet/include"
    }

    links
    {
        "GLFW",
        "GLAD",
        "opengl32",
        "ENet",
        "ws2_32",
        "winmm",

        -- MiniDumpWriteDump and the stack walk in CrashHandler.
        "dbghelp"
    }

    defines
    {
        "CB_PLATFORM_WINDOWS",
        "CB_BUILD_DLL",
        "GLFW_INCLUDE_NONE"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        defines "CB_DEBUG"
        symbols "On"

    filter "configurations:Release"
        defines "CB_RELEASE"
        optimize "On"

    filter "configurations:Dist"
        defines "CB_DIST"
        optimize "On"
