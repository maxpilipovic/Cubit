-- Cubit's own game: everything that is not the engine or its harness.
--
-- One directory so it can become its own repository with a single
-- `git subtree split -P game`, at which point the engine is consumed rather
-- than built alongside. Nothing in here is named by the engine's, the
-- harness's or the engine tests' build files, which is what keeps that move a
-- move. See docs/engine-roadmap.md, item B8.
group "Game"

project "Game"
    location "Game"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Game/src/**.h",
        "Game/src/**.cpp"
    }

    includedirs
    {
        "../Cubit/include",
        "../vendor/GLM",
        "Game/src"
    }

    defines
    {
        "CB_PLATFORM_WINDOWS"
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

project "GameApp"
    location "GameApp"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    --Run with the target dir as the working directory so the game resolves
    --"assets/..." next to the exe, the same layout a shipped build would have.
    debugdir ("../bin/" .. outputdir .. "/%{prj.name}")

    files
    {
        "GameApp/src/**.h",
        "GameApp/src/**.cpp"
    }

    includedirs
    {
        "../Cubit/include",
        "../vendor/GLM",
        "Game/src"
    }

    links
    {
        "Game",
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
            ("{COPY} ../../bin/" .. outputdir .. "/Cubit/Cubit.dll ../../bin/" .. outputdir .. "/GameApp"),
            ('{COPYDIR} "../assets" "%{cfg.targetdir}/assets"')
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

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    --Run with the target dir as the working directory so the server resolves
    --"assets/..." next to the exe, exactly as the game does.
    debugdir ("../bin/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Server/src/**.h",
        "Server/src/**.cpp"
    }

    includedirs
    {
        "../Cubit/include",
        "../vendor/GLM",
        "Game/src"
    }

    links
    {
        "Game",
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
            ("{COPY} ../../bin/" .. outputdir .. "/Cubit/Cubit.dll ../../bin/" .. outputdir .. "/Server"),

            -- The game's assets, not a second copy of a 23.8 MB map. Both ends
            -- must read byte-identical files or the hash check refuses the join
            -- - which is the check working, but a confusing first run.
            ('{COPYDIR} "../assets" "%{cfg.targetdir}/assets"')
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

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    debugdir ("../bin/" .. outputdir .. "/%{prj.name}")

    files
    {
        "MapGen/src/**.h",
        "MapGen/src/**.cpp"
    }

    includedirs
    {
        "../Cubit/include",
        "../vendor/GLM"
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
            ("{COPY} ../../bin/" .. outputdir .. "/Cubit/Cubit.dll ../../bin/" .. outputdir .. "/MapGen")
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

project "GameTests"
    location "GameTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "GameTests/src/**.h",
        "GameTests/src/**.cpp"
    }

    includedirs
    {
        "../Cubit/include",
        "../vendor/GLM",
        "../vendor/doctest/doctest",
        "Game/src"
    }

    links
    {
        "Game",
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
            ("{COPY} ../../bin/" .. outputdir .. "/Cubit/Cubit.dll ../../bin/" .. outputdir .. "/GameTests"),

            -- The game's suite runs as part of the build, so a failing game
            -- test fails the build exactly as an engine one does.
            ("\"%{cfg.targetdir}/GameTests.exe\"")
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
