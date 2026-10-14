#pragma once

#include "CtfState.h"

#include "Cubit/Voxel/VoxLoader.h"

#include <glm/glm.hpp>

//Cubit's own game: what the teams and the intel look like, as voxel models.
namespace CubitGame
{
    inline glm::vec4 TeamColour(Team team)
    {
        return team == Team::Green ? glm::vec4(0.25f, 0.75f, 0.30f, 1.0f)
                                   : glm::vec4(0.25f, 0.45f, 0.95f, 1.0f);
    }

    //The player model with its torso (palette entry 2) in the team's colour.
    inline VoxModel TeamModel(const VoxModel& player, Team team)
    {
        VoxModel model = player;
        model.Colors[2] = TeamColour(team);
        return model;
    }

    //A 6 x 6 x 6 solid cube of palette entry 1, in the team's colour.
    inline VoxModel IntelModel(Team team)
    {
        VoxModel model;
        model.Size = glm::ivec3(6);
        model.Voxels.assign(6 * 6 * 6, 1);
        model.Colors[1] = TeamColour(team);
        return model;
    }
}
