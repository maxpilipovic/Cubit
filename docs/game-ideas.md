# Game ideas for a Steam release

Notes from a brainstorm on 2026-10-26 about what game to build on Cubit and ship
on Steam. Nothing here is decided. Come back to this before starting gameplay
work past the capture-the-intel match.

## What Cubit already gives a game

Destructible voxel terrain with networked edits, client-side prediction, .vox
maps, terrain generation, a character controller, and a working team match mode
(capture the intel). The strongest ideas lean on those.

## The constraint that shapes everything

Indie games that only work with lots of players online usually die on Steam. If
a match needs 10 people and 30 are online, the queue stays empty and the reviews
say "dead game". Favour ideas that are fun with 1-4 players.

## Candidate ideas, best fit first

1. **Co-op fortify-and-defend (front-runner).** 1-4 players get a voxel map and
   a build phase to dig trenches, raise walls and set up chokepoints, then
   survive waves that dig, climb and blow through what they built. Uses block
   editing, networking, maps and terrain gen. The big missing piece is enemy AI
   and pathfinding on voxels.
2. **Siege matches with a build phase.** Grows out of capture the intel: small
   teams (2v2 to 4v4) fortify their intel, then attack each other's. Reuses
   nearly everything already shipped, but needs players online, so it would need
   bots or a strong practice mode at launch.
3. **Co-op heist in destructible buildings.** Take the door or drill through the
   floor above. Loud breaches bring guards. Needs guard AI, stealth and
   hand-built levels, so it's content-heavy.
4. **Dig-down roguelite.** Mine downward through generated layers with new
   materials, hazards and creatures, then surface to upgrade. Fully
   single-player, so no player-count risk.
5. **Asymmetric 1v4 burrower.** One player tunnels through the map as a
   creature, four hunt it. Needs heavy balancing and still needs 5 players per
   match.

## What already exists for idea 1

| | First-person | Build and fortify | Enemies dig through | Short runs |
|---|---|---|---|---|
| 7 Days to Die | yes | yes | yes | no (long survival grind) |
| Deep Rock Galactic | yes | no | - | yes |
| Castle Craft | no (strategy/building) | yes | yes | ? |
| Orcs Must Die! | yes | traps only | no | yes |

Weaker matches: Sanctum (FPS tower defense, no destruction), Cataclismo (brick
siege RTS), Voxel Siege (turn-based artillery), Tower Craft (voxel co-op tower
defense, no building).

The genre is proven and not crowded. The open slot is **7 Days to Die's horde
night as the whole game**: a 20-40 minute co-op first-person run where you
fortify a destructible map, survive waves that breach it, then start fresh with
roguelite unlocks. You keep the best moment and drop the survival chores around
it.

## Inspired by 7 Days to Die, not a spin-off

A real spin-off is impossible because 7 Days to Die is The Fun Pimps' IP. The
genre idea is free, but the setting, enemies, loop and hook have to be our own.

| | 7 Days to Die | This game |
|---|---|---|
| Structure | Open-world survival, horde every 7 days | Each run is a horde night, 20-40 min |
| Between hordes | Scavenging, hunger, crafting | Short build phase, then the fight |
| Progress | One long save | Roguelite unlocks, fresh map each run |
| Enemies | Zombies | Our own (see below) |
| Players | Big sandbox | 1-4, drop-in sessions |

Setting directions, each with a mechanic built in:

1. **Something underground (front-runner).** Creatures tunnel up from below, so
   walls help less and you dig moats or pour stone floors. The threat comes from
   under you, which plays to Cubit's terrain editing in every direction and is
   the clearest answer to "why not just play 7 Days to Die?".
2. **A rising tide.** Corruption spreads across the map each wave and eats or
   converts blocks. You defend ground, not just walls, and the map shrinks.
3. **Night-only sieges.** Build in real time while the sun sets. At night, the
   lights you place are your defence, and enemies go for them first.
4. **Machines.** Siege robots that drill, lay charges and send flyers over
   walls. Each enemy type attacks the fort differently, so you design against a
   known mix.

## Next steps when we come back

1. Play 7 Days to Die horde nights and the Castle Craft demo, noting what feels
   good and what to do differently.
2. Pick a setting and write the core loop: what the player does minute by minute
   in one run.
3. From the loop, list the engine gaps (enemy AI and voxel pathfinding at
   minimum).
4. Build a 10-minute vertical slice: one map, one enemy type, one wave loop.
5. Put up a Steam page early and use wishlists to judge whether the idea has
   legs.
