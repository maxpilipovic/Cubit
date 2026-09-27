#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/ChunkMesher.h"

struct VoxModel;

//Meshes a standalone voxel model - a player, a tool, a pickup - into geometry
//the renderer can draw anywhere.
//
//A sibling of ChunkMesher rather than a caller of it: a model has no World, no
//chunk grid and no neighbours across a boundary, and it is lit as an object
//rather than as terrain. What the two share is the face table and the shading,
//which live in VoxelFaces.h so they cannot drift apart.
//
//A vertex's colour is NOT a finished shade, unlike a chunk vertex's: rgb is the
//palette colour and alpha is the face shade times ambient occlusion, raw. The
//light floor is applied by WorldScene::DrawMesh's shader after it multiplies by
//how lit the model's spot is, so a model is floored once, as terrain is.
//
//Vertices come out in model voxel units with the model's minimum corner at the
//origin. Scaling a model to the size its owner should be, and putting it where
//that owner stands, are the caller's business.
class CB_API ModelMesher
{
public:
    ModelMesher() = delete;

    //Builds the model's exposed faces. A voxel outside the model counts as
    //absent, so the outer shell is drawn.
    static MeshGeometry Build(const VoxModel& model);
};
