#pragma once

#include "Cubit/Core.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A player-editable settings file: `key = value` lines, with `#` comments.
//
//The engine reads the file and nothing more. Which keys exist, what they
//default to and what range each is held to are the application's business;
//see the game's GameSettings.
class CB_API SettingsFile
{
public:
    //Something Parse could not read, and the line it was on, counting from 1.
    struct Problem
    {
        int Line = 0;
        std::string Message;
    };

    //Reads settings from text. Never throws: a line it cannot read is recorded
    //in Problems() and the rest still loads, because a typo in a player's file
    //must not stop the game starting.
    static SettingsFile Parse(std::string_view text);

    //Reads the file at path. Empty when there is no such file; throws only when
    //the file exists and cannot be read.
    static std::optional<SettingsFile> Load(const std::string& path);

    //The value under key, converted. Empty when the key is missing and ALSO when
    //it is present but not wholly a value of the requested type: "12abc" is not
    //12. Saying which it was is the caller's job, since only the caller knows
    //whether the key matters.
    std::optional<std::string> GetString(const std::string& key) const;
    std::optional<int> GetInt(const std::string& key) const;
    std::optional<float> GetFloat(const std::string& key) const;

    //Every key the text set, in the order each first appeared, so a caller can
    //report the ones it does not know.
    const std::vector<std::string>& Keys() const { return m_Keys; }

    const std::vector<Problem>& Problems() const { return m_Problems; }

private:
    std::unordered_map<std::string, std::string> m_Values;
    std::vector<std::string> m_Keys;
    std::vector<Problem> m_Problems;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
