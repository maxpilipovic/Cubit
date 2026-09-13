#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

//How far a player can reach to edit a block, in blocks, measured from the eye
//to the nearest point of the cell.
//
//One number for the Sandbox's aim ray, the client's prediction and the server's
//ruling. Copies would drift, and a client whose reach is a hair longer than the
//server's predicts edits the server refuses - a correction on every click at
//the edge of reach.
constexpr float ReachDistance = 12.0f;

//Whether the overlap rule looks at players other than the editor.
//
//The server always checks them. The client checks them when it predicts,
//against where it last saw them; replay does not re-check them, because a newer
//snapshot could flip an edit the server will accept and flicker it off and on.
enum class OtherPlayers
{
    Check,
    Ignore
};

//True when the nearest point of the unit cell is within ReachDistance of the eye.
CB_API bool IsCellWithinReach(const glm::vec3& eye, const glm::ivec3& cell);

//True when the box and the unit cell share volume. Touching is not overlapping,
//which is what lets a player place the cell their feet rest on top of.
CB_API bool BoxOverlapsCell(const glm::vec3& centre, const glm::vec3& halfExtents,
    const glm::ivec3& cell);

//Whether `editor` may make `edit` in this match, as it stands right now.
//
//Called by the server when it applies an edit and by the client before it
//predicts one - identical code, so identical answers for identical state. The
//match passed in must be in the state it has at the start of the step that
//carries the edit.
CB_API bool IsEditLegal(const MatchState& match, PlayerId editor, const BlockEdit& edit,
    OtherPlayers others);
