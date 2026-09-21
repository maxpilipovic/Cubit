#include "Cubit/Cubit.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxWriter.h"

#include "HudLayer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

//The engine's harness: a map, a free camera, and the voxel systems put through
//their paces.
//
//No player, no shooting and no socket - those are a game's, and Cubit's lives
//under game/. What is here is what the engine can be judged by on screen:
//loading a map, meshing and lighting it, digging and building in it, watching
//unsupported blocks fall, and reading what all of that costs.
namespace
{
    //Centre the map roughly on the origin for the view.
    const glm::vec3 WorldOffset{ -64.0f, -24.0f, -64.0f };

    //Near-black, so the outline reads against both lit terrain and sky.
    const glm::vec4 OutlineColor{ 0.05f, 0.05f, 0.05f, 1.0f };

    //Underwater haze. Roughly half strength at the 12-block reach distance and
    //83% at 30, which reads as murk without hiding what you are aiming at.
    const glm::vec3 FogColor{ 0.10f, 0.30f, 0.55f };
    constexpr float FogDensity = 0.06f;

    //The map the harness loads, resolved against the working directory - the
    //executable's own. The harness writes it there itself the first time it
    //runs (see EnsureMap) rather than borrowing the game's copy: the engine
    //has to run without the game beside it, and the game's suite checks that
    //its shipped battlefield512.vox is this exact file.
    constexpr const char* MapPath = "assets/maps/battlefield512.vox";

    //The size of that map. MapGen writes the game's copy with default
    //TerrainConfig at this size and nothing else, so this is all it takes to
    //reproduce it byte for byte.
    const glm::ivec3 MapSize{ 512, 64, 512 };

    //Where F5 writes the edited world. Deliberately not the map that was
    //loaded: EnsureMap only writes the map when it is missing, so a save over
    //battlefield512.vox would be loaded on every run after it - an edited
    //world quietly standing in for the one docs/performance.md measures.
    constexpr const char* SavePath = "assets/maps/saved.vox";

    //Writes the harness's map if it is not already beside the executable.
    //
    //Generated and then LOADED, rather than generated straight into a World:
    //the harness's load session is how docs/performance.md's figures are
    //reproduced, and they are figures for reading a real 24 MB .vox. Skipping
    //the file would make the capture measure something else under the same
    //name.
    void EnsureMap(const char* path)
    {
        if (std::filesystem::exists(path))
            return;

        TerrainConfig config;
        config.Size = MapSize;

        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        VoxWriter::WriteFile(TerrainGen::Generate(config), path);
        CB_INFO(std::string("Generated the harness map at ") +
            std::filesystem::absolute(path).string());
    }

    //Where the camera starts, over the same column the game spawns on, so a
    //screenshot of the harness frames the same ground.
    const glm::vec3 CameraStart{ 240.5f, 26.9f, 300.5f };

    //Palette indices selectable with the number keys, in order. Water (7) is
    //deliberately absent: it cannot be broken, so being able to place it would
    //hand the user a block they can create and never remove.
    constexpr BlockId PlaceableBlocks[] = { 1, 2, 3, 4, 5, 6, 8 };

    constexpr int PlaceableBlockCount =
        static_cast<int>(sizeof(PlaceableBlocks) / sizeof(PlaceableBlocks[0]));

    //How far the harness reaches to edit a block. The engine holds no such
    //number any more - see MatchRules - and the harness answers to nobody's
    //balance, so it takes the placeholder.
    const MatchRules HarnessRules{};

    //The debug blast's radius: 123 cells, about what a grenade would take.
    constexpr int BlastRadius = 3;

    //Operations the undo stack keeps. Capped so a long session cannot creep.
    constexpr std::size_t MaxUndoDepth = 256;
}

class SandboxLayer final : public Layer
{
public:
    explicit SandboxLayer(std::shared_ptr<HudState> hudState)
        : m_HudState(std::move(hudState)),
          m_CameraController(16.0f / 9.0f)
    {
        Input::SetCursorCaptured(m_Cursor.Captured());

        //A harness that cannot load its map has nothing to do, so unlike F9
        //this does not catch - the failure propagates out of the constructor.
        //Load is the phase worth a capture: it is one-shot, it is the largest
        //remaining cost in the engine, and it is what docs/performance.md
        //tabulates. Written beside the executable, like the assets it loads.
        //
        //Guarded on CB_DIST even though the macros already compile out under
        //it: BeginSession/EndSession themselves are not macros, so left
        //unguarded they would still open a session, record nothing, and write
        //an empty profile-load.json beside a shipped executable every launch.
        //
        //EnsureMap runs first and outside the session, so the one run that has
        //to generate the map does not put generation into a capture of load.
        EnsureMap(MapPath);
#ifndef CB_DIST
        Profiler::BeginSession("load", "profile-load.json");
#endif
        LoadWorld(MapPath);
#ifndef CB_DIST
        Profiler::EndSession();
#endif

        m_CameraController.SetPosition(CameraStart + WorldOffset);
        AimAtMapCentre();
    }

    //Counts the steps the frame ran. Nothing is simulated here: the harness has
    //no character, and the camera moves on frame time rather than in steps, so
    //this is the readout's own number.
    void OnFixedUpdate(Timestep step) override
    {
        (void)step;
        ++m_StepsThisFrame;
    }

    void OnFrameUpdate(Timestep timestep) override
    {
        //The free camera reads the movement keys itself.
        m_CameraController.OnUpdate(timestep);

        const glm::vec3 eye = m_CameraController.GetCamera().GetPosition() - WorldOffset;

        m_HudState->CameraPosition = eye;
        m_HudState->EyeInFluid = World_().IsBlockFluid(
            static_cast<int>(std::floor(eye.x)),
            static_cast<int>(std::floor(eye.y)),
            static_cast<int>(std::floor(eye.z)));
        m_HudState->StepsPerFrame = m_StepsThisFrame;
        m_HudState->UndoDepth = m_Undo.size();
        m_StepsThisFrame = 0;
    }

    //Draws the meshed voxel world through the engine's scene.
    void OnRender(float alpha) override
    {
        (void)alpha;

        m_Scene.Update(World_());
        m_Scene.Render(
            m_CameraController.GetCamera(),
            WorldOffset,
            FogColor,
            m_HudState->EyeInFluid ? FogDensity : 0.0f);

        //Flushed here, while the world camera is current. The HUD overlay
        //renders after this layer and leaves an orthographic matrix behind, so
        //a later flush would draw these lines in screen space.
        DrawTargetedBlockOutline();
        DebugDraw::Flush(
            m_CameraController.GetCamera(),
            glm::translate(glm::mat4(1.0f), WorldOffset));

        m_HudState->MeshFaceCount = m_Scene.TotalFaceCount();
        m_HudState->DrawnChunks = m_Scene.DrawnChunkCount();
        m_HudState->TotalChunks = m_Scene.TotalChunkCount();
        m_HudState->PendingChunks = m_Scene.PendingCount();
    }

    void OnEvent(Event& event) override
    {
        //Mouse-look only while the harness has the mouse: a released cursor is
        //the user pointing at something else.
        if (m_Cursor.Captured() || event.GetEventType() != EventType::MouseMoved)
            m_CameraController.OnEvent(event);

        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<KeyPressedEvent>(
            [this](KeyPressedEvent& keyEvent)
            {
                return OnKeyPressed(keyEvent);
            });
        dispatcher.Dispatch<MouseButtonPressedEvent>(
            [this](MouseButtonPressedEvent& mouseEvent)
            {
                //A click that takes the cursor back is spent doing that, and
                //never also edits.
                if (!m_Cursor.OnClick())
                {
                    ApplyCursor();
                    return true;
                }

                return OnMouseButtonPressed(mouseEvent);
            });
        dispatcher.Dispatch<WindowLostFocusEvent>(
            [this](WindowLostFocusEvent&)
            {
                if (m_Cursor.OnFocusLost())
                    ApplyCursor();

                return false;
            });
    }

private:
    World& World_() { return *m_World; }
    const World& World_() const { return *m_World; }

    //Faces the middle of the map, level with the camera. Aiming at the literal
    //centre of the world box would tilt the view into the ground.
    //
    //Goes through the controller rather than the camera because both hold a
    //copy of yaw and pitch - the reason SetRotation exists at all.
    void AimAtMapCentre()
    {
        const glm::vec3 eye = m_CameraController.GetCamera().GetPosition() - WorldOffset;
        const glm::vec3 target(
            static_cast<float>(World_().GetWidth()) * 0.5f,
            eye.y,
            static_cast<float>(World_().GetDepth()) * 0.5f);

        const glm::vec2 rotation = PerspectiveCamera::YawPitchToward(eye, target);
        m_CameraController.SetRotation(rotation.x, rotation.y);
    }

    //Outlines the block a click would break, using the same ray the edit uses so
    //a disagreement between what is highlighted and what an edit hits is itself
    //visible.
    void DrawTargetedBlockOutline()
    {
        const std::optional<VoxelRayHit> hit = AimedBlock();
        if (!hit)
            return;

        // Nudged outward a hair so the outline is not z-fighting with the block
        // face it traces.
        constexpr float Swell = 0.002f;
        const glm::vec3 min = glm::vec3(hit->Block) - glm::vec3(Swell);
        const glm::vec3 max = min + glm::vec3(1.0f + Swell * 2.0f);

        DebugDraw::Box(min, max, OutlineColor);
    }

    //What the camera is pointing at, within reach. Solid only: water is scenery,
    //so the ray passes through the river to the bed rather than targeting the
    //surface.
    std::optional<VoxelRayHit> AimedBlock() const
    {
        const PerspectiveCamera& camera = m_CameraController.GetCamera();
        const VoxelRayHit hit = VoxelRaycast::Cast(
            World_(),
            camera.GetPosition() - WorldOffset,
            camera.GetForwardDirection(),
            HarnessRules.ReachDistance,
            true);

        if (!hit.Hit)
            return std::nullopt;

        return hit;
    }

    //Breaks or places one block, and brings down whatever the break left
    //hanging, as one undoable step.
    bool OnMouseButtonPressed(MouseButtonPressedEvent& event)
    {
        const MouseCode button = event.GetMouseButton();
        if (button != MouseCode::Left && button != MouseCode::Right)
            return false;

        const std::optional<VoxelRayHit> hit = AimedBlock();
        if (!hit)
            return false;

        // A ray starting inside a block has no entry face, so there is nowhere
        // to place against.
        if (button == MouseCode::Right && hit->Normal == glm::ivec3(0))
            return false;

        const glm::ivec3 target = button == MouseCode::Left
            ? hit->Block
            : hit->Block + hit->Normal;

        // Bounds and relighting both belong to ApplyBlockEdit: an edit is one
        // operation, not a sequence a caller has to remember the rest of.
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(
            World_(),
            BlockEdit{ target, button == MouseCode::Left ? BlockId{0} : m_PlaceBlock });

        if (!inverse)
            return false;

        // Only a dig: placing a block cannot take anything's support away.
        std::vector<BlockEdit> step = button == MouseCode::Left
            ? CollapseAfter({ target })
            : std::vector<BlockEdit>{};
        step.push_back(*inverse);
        PushUndo(std::move(step));

        CB_INFO(
            std::string(button == MouseCode::Left ? "Broke" : "Placed") +
            " block at " + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z));

        return true;
    }

    //Clears whatever the cells just emptied have left with nothing holding it
    //up, and returns how to put that back.
    std::vector<BlockEdit> CollapseAfter(const std::vector<glm::ivec3>& emptied)
    {
        const std::vector<glm::ivec3> loose = FindUnsupported(World_(), emptied);
        if (loose.empty())
            return {};

        std::vector<BlockEdit> falls;
        falls.reserve(loose.size());
        for (const glm::ivec3& cell : loose)
            falls.push_back(BlockEdit{ cell, BlockId{0} });

        CB_INFO(std::to_string(loose.size()) + " blocks came loose and fell");
        return ApplyBlockEdits(World_(), falls);
    }

    //Clears a ball of air where the camera is aiming, as one batch.
    //
    //A stand-in for the explosions and falling terrain that batches exist for,
    //so a batch can be seen, undone and timed in the running engine.
    void BlastAtAim()
    {
        const std::optional<VoxelRayHit> hit = AimedBlock();
        if (!hit)
            return;

        // Water is left alone, as it is for the edit ray: nothing makes it flow,
        // so a hole blown in the river would stay a hole in the river.
        std::vector<BlockEdit> ball;
        for (int dz = -BlastRadius; dz <= BlastRadius; ++dz)
            for (int dy = -BlastRadius; dy <= BlastRadius; ++dy)
                for (int dx = -BlastRadius; dx <= BlastRadius; ++dx)
                {
                    if (dx * dx + dy * dy + dz * dz > BlastRadius * BlastRadius)
                        continue;

                    const glm::ivec3 cell = hit->Block + glm::ivec3(dx, dy, dz);
                    if (World_().IsInBounds(cell.x, cell.y, cell.z)
                        && World_().IsBlockFluid(cell.x, cell.y, cell.z))
                        continue;

                    ball.push_back(BlockEdit{ cell, BlockId{0} });
                }

        const auto start = std::chrono::steady_clock::now();
        std::vector<BlockEdit> undo = ApplyBlockEdits(World_(), ball);
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        if (undo.empty())
            return;

        const std::size_t blasted = undo.size();

        // Whatever the crater left hanging comes down with it, undone as one step.
        std::vector<glm::ivec3> emptied;
        emptied.reserve(undo.size());
        for (const BlockEdit& inverse : undo)
            emptied.push_back(inverse.Position);

        const std::vector<BlockEdit> fell = CollapseAfter(emptied);
        undo.insert(undo.begin(), fell.begin(), fell.end());

        CB_INFO("Blasted " + std::to_string(blasted) + " blocks at " +
            std::to_string(hit->Block.x) + "," + std::to_string(hit->Block.y) + "," +
            std::to_string(hit->Block.z) + " in " + std::to_string(milliseconds) + " ms");

        PushUndo(std::move(undo));
    }

    //Remembers how to undo one operation - a single edit or a whole batch.
    void PushUndo(std::vector<BlockEdit> inverse)
    {
        m_Undo.push_back(std::move(inverse));
        if (m_Undo.size() > MaxUndoDepth)
            m_Undo.erase(m_Undo.begin());
    }

    //Reverses the most recent edit, or the whole of the most recent blast.
    //
    //The entry is popped whether or not applying it changes anything: an
    //inverse that comes back empty describes a cell some later edit has already
    //overwritten, so keeping it would stall the stack on the same dead entry
    //every press. Applying an inverse is itself an edit, but its own inverse is
    //deliberately not pushed - that would make U alternate between two states
    //instead of walking back through history.
    void UndoLastEdit()
    {
        if (m_Undo.empty())
            return;

        const std::vector<BlockEdit> inverse = std::move(m_Undo.back());
        m_Undo.pop_back();
        ApplyBlockEdits(World_(), inverse);
    }

    //Loads a map, lights it, and leaves every chunk dirty for the next render.
    void LoadWorld(const char* path)
    {
        // Building locally before handing it over means a bad file leaves the
        // current world untouched, rather than half-replaced.
        auto loaded = std::make_unique<World>(BuildWorld(VoxLoader::LoadFile(path)));

        // Light has to exist before anything meshes, or the first frames bake
        // a fully dark world into their vertex colours.
        SkyLight::PropagateAll(*loaded);

        m_World = std::move(loaded);

        // The stack describes a world that no longer exists.
        m_Undo.clear();

        CB_INFO(std::string("Loaded world from ") +
            std::filesystem::absolute(path).string());
    }

    //Writes the edited world beside the executable, for promoting by hand.
    void SaveWorld() const
    {
        // This runs inside a GLFW key callback, which is C code, and throwing
        // across a C frame is undefined.
        try
        {
            std::filesystem::create_directories(
                std::filesystem::path(SavePath).parent_path());
            VoxWriter::WriteFile(ToVoxModel(World_()), SavePath);

            CB_INFO("Saved world to " +
                std::filesystem::absolute(SavePath).string());
        }
        catch (const std::exception& error)
        {
            CB_ERROR(std::string("Could not save world: ") + error.what());
        }
    }

    //Restores the world from the last F5 save, leaving the current one alone if
    //there isn't one.
    void ReloadWorld()
    {
        try
        {
            LoadWorld(SavePath);

            CB_INFO("Reloaded world from " +
                std::filesystem::absolute(SavePath).string());
        }
        catch (const std::exception& error)
        {
            CB_ERROR(std::string("Could not reload world: ") + error.what());
        }
    }

    //Hands the cursor to the desktop or takes it back, to match m_Cursor.
    void ApplyCursor()
    {
        Input::SetCursorCaptured(m_Cursor.Captured());

        //The cursor moved freely while it was released, so the last position
        //mouse-look saw would turn into one large jump of the view.
        if (m_Cursor.Captured())
            m_CameraController.ResetMouseTracking();
    }

    //Selects the colour used when placing blocks, or runs a tool.
    bool OnKeyPressed(KeyPressedEvent& event)
    {
        if (event.IsRepeat())
            return false;

        if (event.GetKeyCode() == KeyCode::Escape)
        {
            if (m_Cursor.Release())
                ApplyCursor();

            return true;
        }

        if (event.GetKeyCode() == KeyCode::F5)
        {
            SaveWorld();
            return true;
        }

        if (event.GetKeyCode() == KeyCode::F9)
        {
            ReloadWorld();
            return true;
        }

        if (event.GetKeyCode() == KeyCode::U)
        {
            UndoLastEdit();
            return true;
        }

        if (event.GetKeyCode() == KeyCode::B)
        {
            BlastAtAim();
            return true;
        }

        const int key = static_cast<int>(event.GetKeyCode());
        const int first = static_cast<int>(KeyCode::D1);
        if (key >= first && key < first + PlaceableBlockCount)
        {
            m_PlaceBlock = PlaceableBlocks[key - first];
            CB_INFO("Place block set to " + std::to_string(m_PlaceBlock));
            return true;
        }

        return false;
    }

    std::shared_ptr<HudState> m_HudState;

    //Held by pointer because World has no empty state and F9 replaces it whole.
    std::unique_ptr<World> m_World;

    WorldScene m_Scene;
    PerspectiveCameraController m_CameraController;
    BlockId m_PlaceBlock = BlockId{2};

    //Counted across the current frame's steps and published by OnFrameUpdate.
    int m_StepsThisFrame = 0;

    //How to undo each applied operation, newest last: one inverse for an edit,
    //a whole batch for a blast.
    std::vector<std::vector<BlockEdit>> m_Undo;

    //Whether the harness has the mouse. Escape and losing focus give it back;
    //a click takes it, and that click does not also edit.
    CursorCapture m_Cursor;
};

//Starts the harness and runs the engine loop.
class SandboxApplication final : public Application
{
public:
    SandboxApplication()
    {
        auto hudState = std::make_shared<HudState>();

        PushLayer(std::make_unique<SandboxLayer>(hudState));
        PushOverlay(std::make_unique<HudLayer>(
            hudState,
            GetWindow().GetFramebufferWidth(),
            GetWindow().GetFramebufferHeight()));
    }
};

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    CrashHandler::Install("Sandbox");
    Logger::OpenFile("Sandbox");

    try
    {
        SandboxApplication app;
        app.Run();
    }
    catch (const std::exception& error)
    {
        CB_CRITICAL(std::string("Fatal: ") + error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
