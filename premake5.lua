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

-- Each project describes itself, in a file beside its own sources. A project's
-- paths are relative to its own build file, because premake resolves a script's
-- paths from that script's directory.
--
-- The order matters only for readability: dependencies, then the engine, then
-- the two applications built on it.
include "vendor"
include "Cubit"
include "Sandbox"
include "Tests"

-- The game, in its own directory so it can become its own repository.
include "game"
