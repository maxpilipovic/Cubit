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

group "Dependencies"

project "GLFW"
    location "vendor/GLFW"
    kind "StaticLib"
    language "C"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "vendor/GLFW/include/GLFW/glfw3.h",
        "vendor/GLFW/include/GLFW/glfw3native.h",
        "vendor/GLFW/src/context.c",
        "vendor/GLFW/src/egl_context.c",
        "vendor/GLFW/src/init.c",
        "vendor/GLFW/src/input.c",
        "vendor/GLFW/src/monitor.c",
        "vendor/GLFW/src/null_init.c",
        "vendor/GLFW/src/null_joystick.c",
        "vendor/GLFW/src/null_monitor.c",
        "vendor/GLFW/src/null_window.c",
        "vendor/GLFW/src/osmesa_context.c",
        "vendor/GLFW/src/platform.c",
        "vendor/GLFW/src/vulkan.c",
        "vendor/GLFW/src/wgl_context.c",
        "vendor/GLFW/src/win32_init.c",
        "vendor/GLFW/src/win32_joystick.c",
        "vendor/GLFW/src/win32_module.c",
        "vendor/GLFW/src/win32_monitor.c",
        "vendor/GLFW/src/win32_thread.c",
        "vendor/GLFW/src/win32_time.c",
        "vendor/GLFW/src/win32_window.c",
        "vendor/GLFW/src/window.c"
    }

    includedirs
    {
        "vendor/GLFW/include",
        "vendor/GLFW/src"
    }

    defines
    {
        "_GLFW_WIN32",
        "_CRT_SECURE_NO_WARNINGS"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        runtime "Release"
        optimize "On"

group "Core"

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
        "Cubit/src",
        "vendor/GLFW/include"
    }

    links
    {
        "GLFW"
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
