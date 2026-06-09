workspace "Cubit"
    architecture "x64"
    startproject "Sandbox"

    configurations
    {
        "Debug",
        "Release",
        "Dist"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Cubit"
    location "Cubit"
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    pchheader "cub.h"
    pchsource "Cubit/src/cub.cpp"

    files
    {
        "Cubit/include/**.h",
        "Cubit/src/**.h",
        "Cubit/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include",
        "Cubit/src"
    }

    defines
    {
        "CB_PLATFORM_WINDOWS",
        "CB_BUILD_DLL"
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


project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Sandbox/src/**.h",
        "Sandbox/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include"
    }

    links
    {
        "Cubit"
    }

    defines
    {
        "CB_PLATFORM_WINDOWS"
    }

    filter "system:windows"
        systemversion "latest"

        postbuildcommands
        {
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/Sandbox")
        }

    filter "configurations:Debug"
        defines "CB_DEBUG"
        symbols "On"

    filter "configurations:Release"
        defines "CB_RELEASE"
        optimize "On"

    filter "configurations:Dist"
        defines "CB_DIST"
        optimize "On"
