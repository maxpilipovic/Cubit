#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

class CB_API Camera
{
public:
    virtual ~Camera() = default;

    //Returns the combined matrix that transforms world space into clip space.
    virtual const glm::mat4& GetViewProjectionMatrix() const = 0;
};
