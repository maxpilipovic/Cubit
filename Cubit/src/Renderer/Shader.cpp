#include "cub.h"

#include "Cubit/Renderer/Shader.h"

#include <glad/glad.h>
#include <stdexcept>

Shader::Shader(std::string_view vertexSource, std::string_view fragmentSource)
{
    const std::uint32_t vertexShader = Compile(GL_VERTEX_SHADER, vertexSource);
    std::uint32_t fragmentShader = 0;
    try
    {
        fragmentShader = Compile(GL_FRAGMENT_SHADER, fragmentSource);
    }
    catch (...)
    {
        glDeleteShader(vertexShader);
        throw;
    }

    m_RendererId = glCreateProgram();
    glAttachShader(m_RendererId, vertexShader);
    glAttachShader(m_RendererId, fragmentShader);
    glLinkProgram(m_RendererId);

    GLint linked = GL_FALSE;
    glGetProgramiv(m_RendererId, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE)
    {
        GLint length = 0;
        glGetProgramiv(m_RendererId, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> message(static_cast<std::size_t>(length));
        glGetProgramInfoLog(m_RendererId, length, nullptr, message.data());

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(m_RendererId);
        m_RendererId = 0;
        throw std::runtime_error(std::string("Shader link failed: ") + message.data());
    }

    glDetachShader(m_RendererId, vertexShader);
    glDetachShader(m_RendererId, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

Shader::~Shader()
{
    if (m_RendererId != 0)
        glDeleteProgram(m_RendererId);
}

Shader::Shader(Shader&& other) noexcept
    : m_RendererId(other.m_RendererId)
{
    other.m_RendererId = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this == &other)
        return *this;

    if (m_RendererId != 0)
        glDeleteProgram(m_RendererId);

    m_RendererId = other.m_RendererId;
    other.m_RendererId = 0;
    return *this;
}

void Shader::Bind() const
{
    glUseProgram(m_RendererId);
}

void Shader::Unbind() const
{
    glUseProgram(0);
}

std::uint32_t Shader::Compile(std::uint32_t type, std::string_view source)
{
    const std::uint32_t shader = glCreateShader(type);
    const char* sourceData = source.data();
    const GLint sourceLength = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &sourceData, &sourceLength);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> message(static_cast<std::size_t>(length));
        glGetShaderInfoLog(shader, length, nullptr, message.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader compilation failed: ") + message.data());
    }

    return shader;
}
