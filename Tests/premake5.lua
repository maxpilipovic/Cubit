-- The engine's suite. It covers the engine and, because the harness has no
-- suite of its own, the one rule the harness can be tested on without a
-- window. The game's suite is game/GameTests.
group "Tests"

project "Tests"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/**.h",
        "src/**.cpp"
    }

    includedirs
    {
        "../Cubit/include",
        "../vendor/GLM",
        "../vendor/doctest/doctest",

        -- The harness ships no suite of its own; its HUD labels are checked
        -- here. Header-only and one way: the Sandbox never includes a test.
        "../Sandbox/src"
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

        --These run with the project directory as the working directory, so
        --they are relative to Tests/ rather than to this script.
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
