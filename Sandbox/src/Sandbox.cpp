#include "Cubit/Cubit.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxWriter.h"

#include "HudLayer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct PlayerDiedEvent
{
    int Player;
    int Killer;
};

namespace
{
    //Centre the 128x48x128 map roughly on the origin for the view.
    const glm::vec3 WorldOffset{ -64.0f, -24.0f, -64.0f };

    //How far the player can reach to edit terrain, in blocks.
    constexpr float ReachDistance = 12.0f;

    //Half extents of the player's 0.6 x 1.8 x 0.6 collision box.
    const glm::vec3 PlayerHalfExtents{ 0.3f, 0.9f, 0.3f };

    //Near-black, so the outline reads against both lit terrain and sky.
    const glm::vec4 OutlineColor{ 0.05f, 0.05f, 0.05f, 1.0f };

    //Roughly where to start. Only a column: the height, and whether this exact
    //column is usable at all, are resolved against the loaded map. A hint over
    //a hill or the river moves to the nearest spot that can hold the player
    //rather than burying the camera in terrain — which used to render as a
    //black screen and read as a rendering bug.
    const glm::ivec2 SpawnHintXZ{ 240, 300 };

    //Eye height above the centre of the player box.
    constexpr float EyeOffset = 0.7f;

    constexpr float WalkSpeed = 5.0f;
    constexpr float JumpSpeed = 9.0f;
    constexpr float Gravity = 24.0f;

    //Water physics. Gravity is weakened rather than cancelled, so doing nothing
    //settles the player onto the riverbed instead of leaving them hanging.
    constexpr float WaterGravity = 6.0f;
    constexpr float SinkSpeed = 1.5f;
    constexpr float SwimUpSpeed = 3.5f;
    constexpr float WaterDrag = 0.6f;

    //Underwater haze. Roughly half strength at the 12-block reach distance and
    //83% at 30, which reads as murk without hiding what you are aiming at.
    const glm::vec3 FogColor{ 0.10f, 0.30f, 0.55f };
    constexpr float FogDensity = 0.06f;

    //Below the map floor: a fallen player is returned to spawn.
    constexpr float FallResetHeight = -8.0f;

    //Palette indices selectable with the number keys, in order. Water (7) is
    //deliberately absent: it cannot be broken, so being able to place it would
    //hand the player a block they can create and never remove. The list is
    //indexed off KeyCode::D1, not off the palette id, so removing water shifts
    //everything after it back one key: 7 now selects Wood and 8 selects
    //nothing. That is intentional, not an off-by-one to "fix".
    constexpr BlockId PlaceableBlocks[] = { 1, 2, 3, 4, 5, 6, 8 };

    constexpr int PlaceableBlockCount =
        static_cast<int>(sizeof(PlaceableBlocks) / sizeof(PlaceableBlocks[0]));

    //The map the sandbox starts on, resolved against the working directory like
    //SavePath below.
    constexpr const char* MapPath = "assets/maps/battlefield512.vox";

    //Where F5 writes the edited world, resolved against the current working
    //directory (the project launches from the target directory, so in practice
    //that's beside the executable). Deliberately not the map that was loaded:
    //the assets directory beside the exe is a build
    //artifact that the next Sandbox build overwrites, so a save written over
    //battlefield.vox there would vanish without warning. Promoting a save into
    //Sandbox/assets stays a deliberate copy.
    constexpr const char* SavePath = "assets/maps/saved.vox";
}

class SandboxLayer final : public Layer
{
public:
    //Subscribes the Sandbox layer to typed gameplay notifications.
    SandboxLayer(EventBus& eventBus, std::shared_ptr<HudState> hudState)
        : m_HudState(std::move(hudState)),
          m_CameraController(16.0f / 9.0f)
    {
        Input::SetCursorCaptured(true);

        eventBus.Subscribe<PlayerDiedEvent>(
            [this](const PlayerDiedEvent& event)
            {
                OnPlayerDied(event);
            });

        //The world starts with every chunk dirty, so the first render meshes it.
        //A sandbox that cannot load its map has nothing to do, so unlike F9 this
        //does not catch — the failure propagates out of the constructor.
        //Load is the phase worth a capture: it is one-shot, it is the largest
        //remaining cost in the engine, and it is what docs/performance.md P8
        //tabulates. Written beside the executable, like the assets it loads.
        //
        //Guarded on CB_DIST even though the macros already compile out under it:
        //BeginSession/EndSession themselves are not macros, so left unguarded
        //they would still open a session, record nothing, and write an empty
        //profile-load.json beside a shipped executable on every launch.
#ifndef CB_DIST
        Profiler::BeginSession("load", "profile-load.json");
#endif
        LoadWorld(MapPath);
#ifndef CB_DIST
        Profiler::EndSession();
#endif

        // LoadWorld resolves the spawn but deliberately does not teleport to
        // it: F9 reloads mid-session and should leave the player where they
        // were working. Starting fresh is the one time it should.
        TeleportPlayer(m_Spawn);

        // Face the middle of the map, level with the eye. Aiming at the literal
        // centre of the world box would tilt the view into the ground.
        const glm::vec3 eye = m_PlayerPosition + glm::vec3(0.0f, EyeOffset, 0.0f);
        const glm::vec3 target(
            static_cast<float>(m_World.GetWidth()) * 0.5f,
            eye.y,
            static_cast<float>(m_World.GetDepth()) * 0.5f);

        const glm::vec2 rotation = PerspectiveCamera::YawPitchToward(eye, target);
        m_CameraController.SetRotation(rotation.x, rotation.y);

        UpdateCameraPosition(1.0f);

        constexpr std::string_view vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;
            out vec4 v_Color;
            out vec3 v_WorldPos;

            void main()
            {
                v_Color = a_Color;
                v_WorldPos = (u_Transform * vec4(a_Position, 1.0)).xyz;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";
        constexpr std::string_view fragmentSource = R"(
            #version 330 core
            layout(location = 0) out vec4 color;
            in vec4 v_Color;
            in vec3 v_WorldPos;
            uniform vec3 u_FogColor;
            uniform float u_FogDensity;
            uniform vec3 u_CameraPos;

            void main()
            {
                // Exponential, so it needs no far-plane constant and never
                // saturates abruptly. Density is zero when dry, which makes
                // this a mix against nothing rather than a branch.
                float d = length(v_WorldPos - u_CameraPos);
                float f = 1.0 - exp(-u_FogDensity * d);
                color = vec4(mix(v_Color.rgb, u_FogColor, f), v_Color.a);
            }
        )";
        m_Shader = std::make_unique<Shader>(vertexSource, fragmentSource);
    }

    //Advances the player through the chunk by one fixed step under gravity.
    //The camera is deliberately NOT moved here — rendering interpolates the
    //eye between steps in OnRender.
    void OnFixedUpdate(Timestep timestep) override
    {
        // The step is about to overwrite the position rendering interpolates
        // from, so keep it first.
        m_PreviousPlayerPosition = m_PlayerPosition;
        ++m_StepsThisFrame;

        const float seconds = static_cast<float>(timestep.GetSeconds());
        const bool inFluid = VoxelCollision::OverlapsFluid(
            m_World, m_PlayerPosition, PlayerHalfExtents);

        glm::vec3 walk = ReadWalkInput() * WalkSpeed;

        // Space has to be tested here rather than after the jump: standing on
        // the riverbed is grounded and submerged at once, so a dry jump would
        // otherwise fire instead of a swim stroke.
        if (inFluid)
        {
            walk *= WaterDrag;

            // Only while the head is under. The box still counts as wet with
            // the whole body clear of the surface, so stroking on that alone
            // thrusts the player out of the river and buzzes them above it.
            if (IsEyeInFluid(m_PlayerPosition) &&
                Input::IsKeyPressed(KeyCode::Space))
                m_VerticalVelocity = SwimUpSpeed;

            m_VerticalVelocity -= WaterGravity * seconds;
            m_VerticalVelocity = glm::max(m_VerticalVelocity, -SinkSpeed);
        }
        else
        {
            if (m_Grounded && Input::IsKeyPressed(KeyCode::Space))
                m_VerticalVelocity = JumpSpeed;

            m_VerticalVelocity -= Gravity * seconds;
        }

        //The player position is in world coordinates, so collision runs against
        //the whole world and the box can cross chunk boundaries.
        const VoxelMoveResult move = VoxelCollision::MoveBox(
            m_World,
            m_PlayerPosition,
            PlayerHalfExtents,
            glm::vec3(walk.x, m_VerticalVelocity, walk.z) * seconds);

        m_PlayerPosition = move.Position;
        m_Grounded = move.Grounded;
        m_HudState->PlayerPosition = m_PlayerPosition;
        m_HudState->Grounded = m_Grounded;
        m_HudState->BodyInFluid = inFluid;

        // The eye, not the box: the tint and the fog should come on when the
        // camera goes under, which happens later than the feet getting wet.
        // Taken after the move so rendering is not a frame stale. Kept on the
        // layer rather than read back from HudState, so the render fog does
        // not depend on the HUD struct.
        m_EyeInFluid = IsEyeInFluid(m_PlayerPosition);
        m_HudState->EyeInFluid = m_EyeInFluid;

        // Landing or hitting a ceiling ends vertical motion.
        if (move.BlockedY)
            m_VerticalVelocity = 0.0f;

        // The chunk is not a closed world, so a player can walk off its edge.
        if (m_PlayerPosition.y < FallResetHeight)
        {
            TeleportPlayer(m_Spawn);
            m_VerticalVelocity = 0.0f;
        }
    }

    //Publishes how many fixed steps ran this frame, then resets for the next.
    void OnFrameUpdate(Timestep timestep) override
    {
        (void)timestep;
        m_HudState->StepsPerFrame = m_StepsThisFrame;
        m_HudState->UndoDepth = m_Undo.size();
        m_StepsThisFrame = 0;
    }

    //Draws the meshed voxel world through Cubit's scene renderer.
    void OnRender(float alpha) override
    {
        UpdateCameraPosition(alpha);

        m_WorldRenderer.Update(m_World);

        Renderer::BeginScene(m_CameraController.GetCamera());
        // u_Transform already carries WorldOffset and the camera position is in
        // that same space — the invariant the transparency sort already relies
        // on — so the two can be subtracted directly.
        m_Shader->SetFloat3("u_FogColor", FogColor);
        m_Shader->SetFloat3("u_CameraPos", m_CameraController.GetCamera().GetPosition());
        m_Shader->SetFloat("u_FogDensity", m_EyeInFluid ? FogDensity : 0.0f);
        m_WorldRenderer.Render(
            *m_Shader,
            m_CameraController.GetCamera().GetViewProjectionMatrix(),
            WorldOffset,
            m_CameraController.GetCamera().GetPosition());
        Renderer::EndScene();

        // Flushed here, while the world camera is current. The HUD overlay
        // renders after this layer and leaves an orthographic matrix behind, so
        // a later flush would draw these lines in screen space.
        DrawTargetedBlockOutline();
        DebugDraw::Flush(m_CameraController.GetCamera(), glm::translate(glm::mat4(1.0f), WorldOffset));

        m_HudState->MeshFaceCount = m_WorldRenderer.TotalFaceCount();
        m_HudState->DrawnChunks = m_WorldRenderer.DrawnChunkCount();
        m_HudState->TotalChunks = m_WorldRenderer.TotalChunkCount();
        m_HudState->PendingChunks = m_WorldRenderer.PendingCount();
    }

    //Routes one-time key presses through the typed platform dispatcher.
    void OnEvent(Event& event) override
    {
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
                return OnMouseButtonPressed(mouseEvent);
            });
    }

private:
    //Reports whether the camera sits in a fluid block for a given player
    //position. Taken at the eye rather than the box because a body that still
    //counts as wet can have its head well clear of the surface.
    bool IsEyeInFluid(const glm::vec3& playerPosition) const
    {
        const glm::vec3 eye =
            playerPosition + glm::vec3(0.0f, EyeOffset, 0.0f);

        return m_World.IsBlockFluid(
            static_cast<int>(std::floor(eye.x)),
            static_cast<int>(std::floor(eye.y)),
            static_cast<int>(std::floor(eye.z)));
    }

    //Returns a unit direction for held movement keys, flattened so that looking
    //up or down does not change walking speed.
    glm::vec3 ReadWalkInput() const
    {
        const PerspectiveCamera& camera = m_CameraController.GetCamera();

        glm::vec3 forward = camera.GetForwardDirection();
        glm::vec3 right = camera.GetRightDirection();
        forward.y = 0.0f;
        right.y = 0.0f;

        if (glm::length(forward) > 0.0f)
            forward = glm::normalize(forward);
        if (glm::length(right) > 0.0f)
            right = glm::normalize(right);

        glm::vec3 direction{ 0.0f };
        if (Input::IsKeyPressed(KeyCode::W))
            direction += forward;
        if (Input::IsKeyPressed(KeyCode::S))
            direction -= forward;
        if (Input::IsKeyPressed(KeyCode::D))
            direction += right;
        if (Input::IsKeyPressed(KeyCode::A))
            direction -= right;

        if (glm::length(direction) > 0.0f)
            direction = glm::normalize(direction);

        return direction;
    }

    //Places the camera at eye height above the player, in world space, at the
    //point the player occupied `alpha` of the way through the current step.
    void UpdateCameraPosition(float alpha)
    {
        const glm::vec3 position =
            glm::mix(m_PreviousPlayerPosition, m_PlayerPosition, alpha);

        m_CameraController.SetPosition(
            position + WorldOffset + glm::vec3(0.0f, EyeOffset, 0.0f));
    }

    //Moves the player without interpolating through the space in between.
    //
    //Interpolating a respawn or a reload would smear the camera across the map
    //for a frame, so both positions are written together and the lerp that
    //follows is a no-op.
    //
    //Deliberately does not reset m_VerticalVelocity — that stays the caller's
    //business, and all current callers zero it themselves. A future teleport
    //site that forgets inherits whatever fall velocity the player had.
    void TeleportPlayer(const glm::vec3& position)
    {
        m_PlayerPosition = position;
        m_PreviousPlayerPosition = position;
    }

    //Outlines the block a click would break, using the same ray the edit uses so
    //a disagreement between what is highlighted and what an edit hits is itself
    //visible.
    void DrawTargetedBlockOutline()
    {
        const PerspectiveCamera& camera = m_CameraController.GetCamera();
        const VoxelRayHit hit = VoxelRaycast::Cast(
            m_World,
            camera.GetPosition() - WorldOffset,
            camera.GetForwardDirection(),
            ReachDistance,
            true);

        if (!hit.Hit)
            return;

        // Nudged outward a hair so the outline is not z-fighting with the block
        // face it traces.
        constexpr float Swell = 0.002f;
        const glm::vec3 min = glm::vec3(hit.Block) - glm::vec3(Swell);
        const glm::vec3 max = min + glm::vec3(1.0f + Swell * 2.0f);

        DebugDraw::Box(min, max, OutlineColor);
    }

    //Breaks or places a block along the camera's view ray.
    bool OnMouseButtonPressed(MouseButtonPressedEvent& event)
    {
        const MouseCode button = event.GetMouseButton();
        if (button != MouseCode::Left && button != MouseCode::Right)
            return false;

        const PerspectiveCamera& camera = m_CameraController.GetCamera();
        //Subtracting WorldOffset turns the camera's view-space position back into
        //world coordinates, the space the world and the ray share.
        // Solid only: water is scenery, so an edit ray passes through the river
        // to the bed rather than targeting the surface — or, when the player is
        // standing in it, the cell their own head occupies.
        const VoxelRayHit hit = VoxelRaycast::Cast(
            m_World,
            camera.GetPosition() - WorldOffset,
            camera.GetForwardDirection(),
            ReachDistance,
            true);

        if (!hit.Hit)
            return false;

        const glm::ivec3 target = button == MouseCode::Left
            ? hit.Block
            : hit.Block + hit.Normal;

        // A ray starting inside a block has no entry face, so there is nowhere
        // to place against.
        if (button == MouseCode::Right && hit.Normal == glm::ivec3(0))
            return false;

        const BlockEdit edit{
            target,
            button == MouseCode::Left ? BlockId{0} : m_PlaceBlock };

        // Bounds and relighting both belong to ApplyBlockEdit now: an edit is
        // one operation, not a sequence a caller has to remember the rest of.
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(m_World, edit);
        if (!inverse)
            return false;

        m_Undo.push_back(*inverse);
        if (m_Undo.size() > MaxUndoDepth)
            m_Undo.erase(m_Undo.begin());

        CB_INFO(
            std::string(button == MouseCode::Left ? "Broke" : "Placed") +
            " block at " + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z));

        return true;
    }

    //Reverses the most recent edit.
    //
    //The entry is popped whether or not applying it changes anything: an
    //inverse that comes back empty describes a cell some later edit has already
    //overwritten, so keeping it would stall the stack on the same dead entry
    //every press. Applying an inverse is itself an edit, but its own inverse is
    //deliberately not pushed — that would make U alternate between two states
    //instead of walking back through history.
    void UndoLastEdit()
    {
        if (m_Undo.empty())
            return;

        const BlockEdit inverse = m_Undo.back();
        m_Undo.pop_back();
        ApplyBlockEdit(m_World, inverse);
    }

    //Logs a player-death notification received from the gameplay event bus.
    void OnPlayerDied(const PlayerDiedEvent& event)
    {
        CB_INFO(
            std::string("Player ") + std::to_string(event.Player) +
            " was defeated by player " + std::to_string(event.Killer));
    }

    //Replaces the world with the map at this path and settles the player into it.
    //Throws when the file cannot be read or parsed.
    void LoadWorld(const char* path)
    {
        // Assigning only after LoadFile returns means a bad file leaves the
        // current world untouched, rather than half-replaced.
        m_World = BuildWorld(VoxLoader::LoadFile(path));

        // The stack describes a world that no longer exists.
        m_Undo.clear();

        // Light has to exist before anything meshes, or the first frames bake
        // a fully dark world into their vertex colours.
        SkyLight::PropagateAll(m_World);

        // Before the lift, not after: the lift's last-resort fallback is the
        // spawn, so it has to be valid for the world just loaded.
        ResolveSpawn();

        LiftPlayerClearOfTerrain();
        m_VerticalVelocity = 0.0f;
        UpdateCameraPosition(1.0f);
    }

    //Steps the player up until their box is clear of solid blocks.
    //
    //A reload can restore terrain where the player was standing, and
    //VoxelCollision only pushes a box out of a block on a move it detects, so a
    //player who starts embedded stays embedded with no escape but falling out of
    //the world. Keeping x and z preserves the part of the map being worked on,
    //which is the point of reloading quickly.
    //
    //Only solid blocks count, so reloading while standing in the river leaves
    //the player in the water rather than lifting them onto its surface.
    void LiftPlayerClearOfTerrain()
    {
        const float top = static_cast<float>(m_World.GetHeight());

        glm::vec3 lifted = m_PlayerPosition;
        while (lifted.y < top &&
            VoxelCollision::Overlaps(m_World, lifted, PlayerHalfExtents))
            lifted.y += 1.0f;

        // A column solid to the sky has nowhere to stand.
        if (VoxelCollision::Overlaps(m_World, lifted, PlayerHalfExtents))
            lifted = m_Spawn;

        // A lift is a discontinuity, so it must not be interpolated through.
        TeleportPlayer(lifted);
    }

    //Resolves the spawn hint against the loaded map.
    void ResolveSpawn()
    {
        const std::optional<glm::vec3> found =
            FindSpawn(m_World, SpawnHintXZ, PlayerHalfExtents);

        if (found)
        {
            m_Spawn = *found;
            return;
        }

        // Nothing standable within the search radius. Drop in from above the
        // hint and say so: the whole point is that a bad spawn stops being a
        // silent black screen.
        CB_ERROR(
            "No spawn found within " + std::to_string(MaxSpawnSearchRadius) +
            " columns of " + std::to_string(SpawnHintXZ.x) + "," +
            std::to_string(SpawnHintXZ.y) + " - dropping in from above");

        m_Spawn = glm::vec3(
            static_cast<float>(SpawnHintXZ.x) + 0.5f,
            static_cast<float>(m_World.GetHeight()) - PlayerHalfExtents.y,
            static_cast<float>(SpawnHintXZ.y) + 0.5f);
    }

    //Writes the edited world beside the executable and logs where it went.
    void SaveWorld() const
    {
        // This runs inside a GLFW key callback, which is C code, and throwing
        // across a C frame is undefined. A failed save has to end here, as a log
        // line rather than a crash.
        try
        {
            VoxWriter::WriteFile(ToVoxModel(m_World), SavePath);

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
        // Same reason SaveWorld catches: this runs inside a GLFW key callback,
        // which is C code, and throwing across a C frame is undefined.
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

    //Selects the colour used when placing blocks, or logs an unhandled press.
    bool OnKeyPressed(KeyPressedEvent& event)
    {
        if (event.IsRepeat())
            return false;

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

        const int key = static_cast<int>(event.GetKeyCode());
        const int first = static_cast<int>(KeyCode::D1);
        if (key >= first && key < first + PlaceableBlockCount)
        {
            m_PlaceBlock = PlaceableBlocks[key - first];
            return true;
        }

        return false;
    }

    std::unique_ptr<Shader> m_Shader;
    std::shared_ptr<HudState> m_HudState;
    World m_World{ 1, 1, 1 };
    WorldRenderer m_WorldRenderer;
    BlockId m_PlaceBlock = BlockId{2};
    glm::vec3 m_Spawn{ 0.0f };
    glm::vec3 m_PlayerPosition{ 0.0f };
    //Where the player stood at the end of the previous fixed step. Rendering
    //interpolates between this and the current position, so motion stays smooth
    //when frames and steps do not line up.
    glm::vec3 m_PreviousPlayerPosition{ 0.0f };
    //Counted across the current frame's steps and published by OnFrameUpdate.
    int m_StepsThisFrame = 0;
    //Inverses of applied edits, newest last. Capped so a long session cannot
    //creep; the oldest entries are the least likely to be wanted back.
    static constexpr std::size_t MaxUndoDepth = 256;
    std::vector<BlockEdit> m_Undo;
    float m_VerticalVelocity = 0.0f;
    bool m_Grounded = false;
    bool m_EyeInFluid = false;
    PerspectiveCameraController m_CameraController;
};

class SandboxApplication final : public Application
{
public:
    //Creates the Sandbox layer and publishes a gameplay event.
    SandboxApplication()
    {
        //Shared so the overlay can read what the gameplay layer writes, without
        //either layer knowing about the other.
        auto hudState = std::make_shared<HudState>();

        PushLayer(std::make_unique<SandboxLayer>(GetEventBus(), hudState));
        PushOverlay(std::make_unique<HudLayer>(
            hudState,
            GetWindow().GetFramebufferWidth(),
            GetWindow().GetFramebufferHeight()));
        GetEventBus().Publish(PlayerDiedEvent{ 1, 2 });
    }
};

//Starts the Sandbox application and runs the engine loop.
int main()
{
    SandboxApplication app;
    app.Run();

    return 0;
}
