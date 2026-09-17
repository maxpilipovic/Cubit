#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"

#include <glm/glm.hpp>

#include <optional>
#include <span>
#include <vector>

class World;

//One block changing at one position.
//
//An intent, not a record: it says what the world should become, not what it was.
//A client sending an edit cannot honestly report the previous block — it only
//believes it knows — so the previous value comes back from applying instead.
struct BlockEdit
{
    glm::ivec3 Position{ 0 };
    BlockId Block = 0;
};

//Applies an edit and returns the edit that would undo it, or nothing if the
//world did not change.
//
//Relights the affected region as part of applying, so an edit is one operation
//that leaves the world consistent rather than a ritual every caller has to know
//the second half of.
//
//An out-of-range position returns nothing rather than throwing, unlike
//World::SetBlock: once an edit is data that can arrive from a file or a socket,
//a bad coordinate is malformed input rather than a bug in the caller.
CB_API std::optional<BlockEdit> ApplyBlockEdit(World& world, const BlockEdit& edit);

//Applies a batch of edits in order and returns the batch that undoes it: apply
//the result, as it is, to put back every block this batch changed.
//
//For changes nobody made by hand - an explosion, terrain giving way - which
//touch many cells at once. Each edit follows ApplyBlockEdit's rules, so an
//out-of-range or no-op edit is skipped and has no undo entry, and a cell named
//twice ends with the later block. The world afterwards, light included, is
//exactly the world the same edits applied one at a time would leave.
CB_API std::vector<BlockEdit> ApplyBlockEdits(World& world, std::span<const BlockEdit> edits);
