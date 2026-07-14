#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
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

    //Uploads one integer uniform to this shader program.
    void SetInt(std::string_view name, int value) const;

    //Uploads one floating-point uniform to this shader program.
    void SetFloat(std::string_view name, float value) const;

    //Uploads one two-component vector uniform to this shader program.
    void SetFloat2(std::string_view name, const glm::vec2& value) const;

    //Uploads one three-component vector uniform to this shader program.
    void SetFloat3(std::string_view name, const glm::vec3& value) const;

    //Uploads one four-component vector uniform to this shader program.
    void SetFloat4(std::string_view name, const glm::vec4& value) const;

    //Uploads one four-by-four matrix uniform to this shader program.
    void SetMat4(std::string_view name, const glm::mat4& value) const;

private:
    //Compiles one OpenGL shader stage and reports failures.
    static std::uint32_t Compile(std::uint32_t type, std::string_view source);

    //Finds a named uniform in this shader program.
    int GetUniformLocation(std::string_view name) const;

    std::uint32_t m_RendererId = 0;
};
