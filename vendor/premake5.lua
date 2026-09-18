-- The third-party libraries, built from source alongside the engine.
--
-- Paths here are relative to this file, not to the root: premake resolves a
-- script's paths from the script's own directory, which is what lets each
-- project keep its build file beside its sources.
group "Dependencies"

project "GLAD"
    location "GLAD"
    kind "StaticLib"
    language "C"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "GLAD/include/glad/glad.h",
        "GLAD/include/KHR/khrplatform.h",
        "GLAD/src/glad.c"
    }

    includedirs
    {
        "GLAD/include"
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
    location "GLFW"
    kind "StaticLib"
    language "C"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "GLFW/include/GLFW/glfw3.h",
        "GLFW/include/GLFW/glfw3native.h",
        "GLFW/src/context.c",
        "GLFW/src/egl_context.c",
        "GLFW/src/init.c",
        "GLFW/src/input.c",
        "GLFW/src/monitor.c",
        "GLFW/src/null_init.c",
        "GLFW/src/null_joystick.c",
        "GLFW/src/null_monitor.c",
        "GLFW/src/null_window.c",
        "GLFW/src/osmesa_context.c",
        "GLFW/src/platform.c",
        "GLFW/src/vulkan.c",
        "GLFW/src/wgl_context.c",
        "GLFW/src/win32_init.c",
        "GLFW/src/win32_joystick.c",
        "GLFW/src/win32_module.c",
        "GLFW/src/win32_monitor.c",
        "GLFW/src/win32_thread.c",
        "GLFW/src/win32_time.c",
        "GLFW/src/win32_window.c",
        "GLFW/src/window.c"
    }

    includedirs
    {
        "GLFW/include",
        "GLFW/src"
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
    location "ENet"
    kind "StaticLib"
    language "C"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "ENet/include/**.h",
        "ENet/callbacks.c",
        "ENet/compress.c",
        "ENet/host.c",
        "ENet/list.c",
        "ENet/packet.c",
        "ENet/peer.c",
        "ENet/protocol.c",
        "ENet/win32.c"
    }

    includedirs
    {
        "ENet/include"
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
