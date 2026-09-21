#pragma once

#include <glm/glm.hpp>

//Cubit's own game: the map it plays on, and where on it a player starts.
namespace CubitGame
{
    //The map loaded when nothing names another: by the server unless its
    //command line gives a path, and by single-player unless --map does.
    //Resolved against the working directory - the executable's own, where the
    //build puts a copy of the game's assets.
    constexpr const char* DefaultMapPath = "assets/maps/battlefield512.vox";

    //Roughly where a player starts on the default map. Only a column: the
    //height, and whether this exact column is usable at all, are resolved
    //against the loaded map by FindSpawn. A hint over a hill or the river moves
    //to the nearest spot that can hold the player rather than burying the
    //camera in terrain - which used to render as a black screen and read as a
    //rendering bug.
    //
    //One definition for the client and the server. They each used to hold a
    //copy, and two copies is how a server comes to put a player somewhere the
    //client does not expect.
    inline const glm::ivec2 SpawnHint{ 240, 300 };

    //The spawn hint to use on a world of this width and depth. The hint above
    //was chosen for the default map, and another map - from --map, or a path on
    //the server's command line - may not contain it: on the 256 battlefield,
    //z = 300 is past the edge, and a player dropped in above a column outside
    //the world falls forever. Such a map starts players at its centre.
    inline glm::ivec2 SpawnHintFor(int width, int depth)
    {
        const bool inside = SpawnHint.x >= 0 && SpawnHint.x < width &&
            SpawnHint.y >= 0 && SpawnHint.y < depth;

        return inside ? SpawnHint : glm::ivec2(width / 2, depth / 2);
    }
}
