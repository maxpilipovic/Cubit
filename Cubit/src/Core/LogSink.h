#pragma once

#include <string>
#include <string_view>

//Where both logging channels write: the console, and a file once one is open.
//
//One sink for Logger and CoreLogger, so the two channels share a clock, a lock
//and a file, and their lines interleave in the order they happened.
namespace LogSink
{
    //Writes `HH:MM:SS.mmm [channel] [level] message` to the console and, if
    //open, the file, flushing both - an unflushed line is exactly the one a
    //crash loses.
    void Write(std::string_view channel, std::string_view level, std::string_view message);

    //Starts copying every line to `path`, replacing any file already open.
    //False if the file could not be created; the console carries on regardless.
    bool OpenFile(const std::string& path);

    void CloseFile();

    //The open file's path, or empty.
    std::string FilePath();
}
