#include "cub.h"

#include "Cubit/SettingsFile.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace
{
    //Spaces, tabs, and the \r a Windows line ending leaves behind.
    constexpr std::string_view Blank = " \t\r";

    std::string_view Trim(std::string_view text)
    {
        const std::size_t first = text.find_first_not_of(Blank);
        if (first == std::string_view::npos)
            return {};

        const std::size_t last = text.find_last_not_of(Blank);
        return text.substr(first, last - first + 1);
    }

    //Converts the whole of text, or nothing. from_chars stops at the first
    //character it cannot use and reports success, so the end pointer is what
    //tells "12" from "12abc".
    template <typename T>
    std::optional<T> Convert(const std::string& text)
    {
        T value{};
        const char* begin = text.data();
        const char* end = text.data() + text.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);

        if (result.ec != std::errc() || result.ptr != end || text.empty())
            return std::nullopt;

        return value;
    }
}

SettingsFile SettingsFile::Parse(std::string_view text)
{
    SettingsFile file;
    int lineNumber = 0;
    std::size_t start = 0;

    while (start <= text.size())
    {
        const std::size_t end = text.find('\n', start);
        std::string_view line = end == std::string_view::npos
            ? text.substr(start)
            : text.substr(start, end - start);
        ++lineNumber;
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;

        const std::size_t hash = line.find('#');
        if (hash != std::string_view::npos)
            line = line.substr(0, hash);

        line = Trim(line);
        if (line.empty())
            continue;

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            file.m_Problems.push_back({ lineNumber, "expected key = value" });
            continue;
        }

        const std::string key(Trim(line.substr(0, equals)));
        if (key.empty())
        {
            file.m_Problems.push_back({ lineNumber, "nothing before the =" });
            continue;
        }

        if (file.m_Values.find(key) == file.m_Values.end())
            file.m_Keys.push_back(key);

        //Assigned rather than inserted, so a repeated key takes its last value.
        file.m_Values[key] = std::string(Trim(line.substr(equals + 1)));
    }

    return file;
}

std::optional<SettingsFile> SettingsFile::Load(const std::string& path)
{
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return std::nullopt;

    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read settings file: " + path);

    std::ostringstream contents;
    contents << in.rdbuf();

    return Parse(contents.str());
}

std::optional<std::string> SettingsFile::GetString(const std::string& key) const
{
    const auto found = m_Values.find(key);
    if (found == m_Values.end())
        return std::nullopt;

    return found->second;
}

std::optional<int> SettingsFile::GetInt(const std::string& key) const
{
    const std::optional<std::string> text = GetString(key);
    return text ? Convert<int>(*text) : std::nullopt;
}

std::optional<float> SettingsFile::GetFloat(const std::string& key) const
{
    const std::optional<std::string> text = GetString(key);
    return text ? Convert<float>(*text) : std::nullopt;
}
