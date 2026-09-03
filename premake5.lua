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

project "GLAD"
    location "vendor/GLAD"
    kind "StaticLib"
    language "C"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "vendor/GLAD/include/glad/glad.h",
        "vendor/GLAD/include/KHR/khrplatform.h",
        "vendor/GLAD/src/glad.c"
    }

    includedirs
    {
        "vendor/GLAD/include"
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

project "ENet"
    location "vendor/ENet"
    kind "StaticLib"
    language "C"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "vendor/ENet/include/**.h",
        "vendor/ENet/callbacks.c",
        "vendor/ENet/compress.c",
        "vendor/ENet/host.c",
        "vendor/ENet/list.c",
        "vendor/ENet/packet.c",
        "vendor/ENet/peer.c",
        "vendor/ENet/protocol.c",
        "vendor/ENet/win32.c"
    }

    includedirs
    {
        "vendor/ENet/include"
    }

    defines
    {
        --ENet's Windows backend. Without it the unix.c path is selected and
        --nothing links.
        "WIN32",
        "_CRT_SECURE_NO_WARNINGS",
        "_WINSOCK_DEPRECATED_NO_WARNINGS"
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
        "vendor/GLFW/include",
        "vendor/GLAD/include",
        "vendor/GLM",
        "vendor/ENet/include"
    }

    links
    {
        "GLFW",
        "GLAD",
        "opengl32",
        "ENet",
        "ws2_32",
        "winmm"
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


project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    --Run with the target dir as the working directory so the app resolves
    --"assets/..." next to the exe, the same layout a shipped build would have.
    debugdir ("bin/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Sandbox/src/**.h",
        "Sandbox/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include",
        "vendor/GLM"
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
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/Sandbox"),
            ('{COPYDIR} "assets" "%{cfg.targetdir}/assets"')
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

project "MapGen"
    location "MapGen"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "MapGen/src/**.h",
        "MapGen/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include",
        "vendor/GLM"
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
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/MapGen")
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

project "Server"
    location "Server"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    --Run with the target dir as the working directory so the server resolves
    --"assets/..." next to the exe, exactly as the Sandbox does.
    debugdir ("bin/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Server/src/**.h",
        "Server/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include",
        "vendor/GLM"
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
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/Server"),

            -- The Sandbox's assets, not a second copy of a 23.8 MB map. Both
            -- ends must read byte-identical files or the hash check refuses the
            -- join - which is the check working, but a confusing first run.
            ('{COPYDIR} "../Sandbox/assets" "%{cfg.targetdir}/assets"')
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

group "Tests"

project "Tests"
    location "Tests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Tests/src/**.h",
        "Tests/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include",
        "vendor/GLM",
        "vendor/doctest/doctest"
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
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/Tests"),

            -- Run the suite as part of the build. Tests.exe returns non-zero on
            -- failure, so a broken test breaks the build instead of waiting to
            -- be noticed.
            "\"$(TargetDir)Tests.exe\""
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
