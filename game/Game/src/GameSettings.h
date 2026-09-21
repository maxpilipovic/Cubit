#pragma once

#include "Cubit/SettingsFile.h"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

//Cubit's own game: what a player can set without rebuilding it.
namespace CubitGame
{
    //The file the settings live in, resolved against the working directory -
    //the executable's own, like the maps.
    constexpr const char* SettingsFileName = "settings.cfg";

    //The settings, at the values the game shipped with before they could be
    //changed at all.
    struct GameSettings
    {
        float MouseSensitivity = 0.12f;
        float FieldOfView = 60.0f;
        int WindowWidth = 1280;
        int WindowHeight = 720;

        bool operator==(const GameSettings&) const = default;
    };

    //The range each setting is held to. A value outside it is clamped rather
    //than refused: `field_of_view = 500` plainly means "as wide as allowed".
    struct SettingRange
    {
        float Min;
        float Max;
    };

    constexpr const char* MouseSensitivityKey = "mouse_sensitivity";
    constexpr const char* FieldOfViewKey = "field_of_view";
    constexpr const char* WindowWidthKey = "window_width";
    constexpr const char* WindowHeightKey = "window_height";

    constexpr SettingRange MouseSensitivityRange{ 0.01f, 2.0f };
    constexpr SettingRange FieldOfViewRange{ 30.0f, 120.0f };
    constexpr SettingRange WindowWidthRange{ 640.0f, 7680.0f };
    constexpr SettingRange WindowHeightRange{ 360.0f, 4320.0f };

    //What a missing settings file is written as. GameSettingsTests reads this
    //back through Apply and requires exactly GameSettings{} with no warnings,
    //so it cannot drift from the defaults above.
    inline std::string DefaultFileText()
    {
        return
            "# Cubit settings. Edit this and restart the game.\n"
            "# Delete the file to get the defaults back.\n"
            "\n"
            "# Degrees the view turns per pixel of mouse movement. 0.01 to 2.\n"
            "mouse_sensitivity = 0.12\n"
            "\n"
            "# Vertical field of view in degrees. 30 to 120.\n"
            "field_of_view = 60\n"
            "\n"
            "# Window size in pixels, from 640x360 up to 7680x4320.\n"
            "window_width = 1280\n"
            "window_height = 720\n";
    }

    namespace Detail
    {
        template <typename T>
        std::string Text(T value)
        {
            std::ostringstream out;
            out << value;
            return out.str();
        }

        //Holds value to range, and says so when it had to.
        template <typename T>
        T Clamp(const char* key, T value, SettingRange range, std::vector<std::string>& warnings)
        {
            const T low = static_cast<T>(range.Min);
            const T high = static_cast<T>(range.Max);
            const T held = std::clamp(value, low, high);

            if (held != value)
                warnings.push_back(std::string("settings: ") + key + " = " + Text(value) +
                    " is outside " + Text(low) + " to " + Text(high) + "; using " + Text(held));

            return held;
        }

        //Reads one setting. Missing leaves it alone; present but not a number
        //warns and leaves it alone; a number is clamped into range.
        template <typename T>
        void Read(const SettingsFile& file, const char* key, T& setting, SettingRange range,
            std::vector<std::string>& warnings)
        {
            const std::optional<std::string> text = file.GetString(key);
            if (!text)
                return;

            std::optional<T> value;
            if constexpr (std::is_same_v<T, int>)
                value = file.GetInt(key);
            else
                value = file.GetFloat(key);

            if (!value)
            {
                warnings.push_back(std::string("settings: ") + key + " = " + *text +
                    " is not a number; keeping " + Text(setting));
                return;
            }

            setting = Clamp(key, *value, range, warnings);
        }
    }

    //Applies every setting the file names on top of what settings already
    //holds, and puts what it could not use into warnings. Called once for the
    //file and again for the command line, so a flag wins and passes the same
    //checks as the line it overrides.
    inline void Apply(const SettingsFile& file, GameSettings& settings,
        std::vector<std::string>& warnings)
    {
        for (const SettingsFile::Problem& problem : file.Problems())
            warnings.push_back("settings line " + std::to_string(problem.Line) + ": " +
                problem.Message);

        Detail::Read(file, MouseSensitivityKey, settings.MouseSensitivity, MouseSensitivityRange, warnings);
        Detail::Read(file, FieldOfViewKey, settings.FieldOfView, FieldOfViewRange, warnings);
        Detail::Read(file, WindowWidthKey, settings.WindowWidth, WindowWidthRange, warnings);
        Detail::Read(file, WindowHeightKey, settings.WindowHeight, WindowHeightRange, warnings);

        for (const std::string& key : file.Keys())
            if (key != MouseSensitivityKey && key != FieldOfViewKey &&
                key != WindowWidthKey && key != WindowHeightKey)
                warnings.push_back("settings: unknown key '" + key + "' ignored");
    }

    //One line for the log saying what the game is actually running with,
    //wherever each value came from.
    inline std::string Describe(const GameSettings& settings)
    {
        return "Settings: mouse sensitivity " + Detail::Text(settings.MouseSensitivity) +
            ", field of view " + Detail::Text(settings.FieldOfView) +
            ", window " + Detail::Text(settings.WindowWidth) + "x" +
            Detail::Text(settings.WindowHeight);
    }
}
