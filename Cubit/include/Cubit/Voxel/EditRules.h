#pragma once

#include "Cubit/Core.h"
#include "Cubit/MatchRules.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

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

//True when the nearest point of the unit cell is within `reach` of the eye.
//Pass `MatchRules::ReachDistance`; the caller holds the rules, not this.
CB_API bool IsCellWithinReach(const glm::vec3& eye, const glm::ivec3& cell, float reach);

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
//
//`rules` is where reach comes from, and both ends must pass the same one or a
//client predicts edits the server refuses. It defaults so the engine's own tests
//can ask about state rather than tuning; a game always passes its own.
CB_API bool IsEditLegal(const MatchState& match, PlayerId editor, const BlockEdit& edit,
    OtherPlayers others, const MatchRules& rules = MatchRules{});
