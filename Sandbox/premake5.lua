-- The engine's harness: the app that exercises the engine and nothing else.
-- It loads a map, flies a camera around it, edits, saves and reloads. The
-- game is a separate application; this one has no player and no match.
group "Core"

project "Sandbox"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    --Run with the target dir as the working directory so the app resolves
    --"assets/..." next to the exe, the same layout a shipped build would have.
    debugdir ("../bin/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/**.h",
        "src/**.cpp"
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

        --These run with the project directory as the working directory, so
        --they are relative to Sandbox/ rather than to this script.
        postbuildcommands
        {
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/Sandbox")

            --No assets are copied. The harness used to borrow the game's maps,
            --the one path here that reached across the split; it now writes
            --its own map on first run with the engine's TerrainGen, so nothing
            --under game/ is needed to build or run it.
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
