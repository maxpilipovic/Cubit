#include "cub.h"

#include "Cubit/Paths.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <string>

std::filesystem::path ExecutableDirectory()
{
    //Grown until it fits: a path under a deep build tree can pass MAX_PATH,
    //and a truncated one would name a directory that does not exist.
    std::wstring buffer(MAX_PATH, L'\0');

    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
            static_cast<DWORD>(buffer.size()));

        if (length == 0)
            return std::filesystem::current_path();

        if (length < buffer.size())
        {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path EnterExecutableDirectory()
{
    const std::filesystem::path launched = std::filesystem::current_path();
    std::filesystem::current_path(ExecutableDirectory());
    return launched;
}

std::filesystem::path FromLaunchDirectory(const std::filesystem::path& given,
    const std::filesystem::path& launchDirectory)
{
    if (given.is_absolute())
        return given;

    const std::filesystem::path fromLaunch = launchDirectory / given;
    std::error_code error;
    if (std::filesystem::exists(fromLaunch, error))
        return fromLaunch;

    return given;
}
