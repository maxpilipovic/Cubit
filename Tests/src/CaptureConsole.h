#pragma once

#include <iostream>
#include <sstream>
#include <string>

//Everything written to std::cout while alive, and std::cout back as it was
//afterwards. Catches the engine's log lines too: Cubit.dll and the tests share
//one C++ runtime, and so one std::cout.
class CaptureConsole
{
public:
    CaptureConsole() : m_Previous(std::cout.rdbuf(m_Captured.rdbuf())) {}
    ~CaptureConsole() { std::cout.rdbuf(m_Previous); }

    CaptureConsole(const CaptureConsole&) = delete;
    CaptureConsole& operator=(const CaptureConsole&) = delete;

    std::string Text() const { return m_Captured.str(); }

private:
    std::stringstream m_Captured;
    std::streambuf* m_Previous;
};
