#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <span>
#include <vector>

class World;

//How many solid cells one search may walk before it gives up and leaves the
//piece where it is.
//
//The guard for the one case the rule below cannot answer cheaply: something
//enormous and genuinely unanchored, such as a platform a player built out over
//nothing. Proving a piece is unanchored means walking all of it, so past this
//many cells the answer becomes "it stays". Anchored pieces are not what this
//costs: a search into terrain reaches the bottom layer and stops long before
//here.
constexpr std::size_t MaxSupportSearch = 4096;

//Every solid cell that has nothing holding it up any more, given the cells a
//change has just emptied.
//
//A cell is held up when touching solid cells lead down to the world's bottom
//layer, y = 0. Terrain is built up from there, so a hill dug into is still
//anchored through the ground under it, however much is taken out around it -
//which is what keeps a dig from bringing the world down.
//
//Only the up-to-six solid neighbours of each emptied cell are examined:
//emptying a cell cannot unsettle anything it was not touching, and filling one
//cannot unsettle anything at all. Each search walks a whole connected piece, so
//anything touching a piece that comes loose is part of that piece - which is why
//removing what this returns cannot leave something else hanging, and needs no
//second pass.
//
//Water holds nothing up and never comes loose: it is not solid, so it is already
//outside the rule.
//
//`maxSearch` is for tests and measurement; callers want the default.
CB_API std::vector<glm::ivec3> FindUnsupported(const World& world,
    std::span<const glm::ivec3> emptied, std::size_t maxSearch = MaxSupportSearch);
