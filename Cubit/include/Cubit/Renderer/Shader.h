#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <string_view>

class CB_API Shader
{
public:
    //Compiles and links an OpenGL vertex and fragment shader program.
    Shader(std::string_view vertexSource, std::string_view fragmentSource);

    //Releases the owned OpenGL shader program.
    ~Shader();

    //Prevents copying ownership of an OpenGL shader program.
    Shader(const Shader&) = delete;

    //Prevents assigning duplicate ownership of an OpenGL shader program.
    Shader& operator=(const Shader&) = delete;

    //Transfers ownership of an OpenGL shader program.
    Shader(Shader&& other) noexcept;

    //Releases the current program before taking another program.
    Shader& operator=(Shader&& other) noexcept;

    //Binds this program for subsequent draw calls.
    void Bind() const;

    //Removes the current OpenGL shader-program binding.
    void Unbind() const;

private:
    //Compiles one OpenGL shader stage and reports failures.
    static std::uint32_t Compile(std::uint32_t type, std::string_view source);

    std::uint32_t m_RendererId = 0;
};
