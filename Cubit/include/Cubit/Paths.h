#pragma once

#include "Cubit/Core.h"

#include <filesystem>

//Where files are found.
//
//Every file a program ships with - maps, models, fonts, settings, and the logs and
//crash dumps it writes - is named relative to the executable, because that is
//where the build puts them. A program started from anywhere else used to look in
//the wrong place, and a missing map aborts in a way that looks like a render bug.
//So each program's main calls EnterExecutableDirectory first, and every relative
//path after that means "beside the executable".
//
//A path the USER types is different: they meant it relative to where they were
//when they typed it. FromLaunchDirectory keeps that meaning.

//The directory holding the running executable. Absolute.
CB_API std::filesystem::path ExecutableDirectory();

//Makes the executable's directory the working directory, and returns the one the
//program was launched from, for FromLaunchDirectory. Call it first in main,
//before anything opens a file - the logger and the crash handler included.
CB_API std::filesystem::path EnterExecutableDirectory();

//A path the user typed, resolved as they meant it: a relative path that names a
//file in the launch directory is taken from there. Anything else - an absolute
//path, or a relative one not found there - comes back unchanged, which after
//EnterExecutableDirectory means relative to the executable, where the shipped
//assets are.
CB_API std::filesystem::path FromLaunchDirectory(const std::filesystem::path& given,
    const std::filesystem::path& launchDirectory);
