#include "Cubit/Cubit.h"
#include "Cubit/Net/EnetTransport.h"
#include "Cubit/Net/MapHash.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxWriter.h"

#include "HudLayer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//How this Sandbox was launched. Default-constructed means single-player, which
//must stay byte-for-byte the app it was before networking existed: the
//project's rendering verification is scripted screenshots and POS/FACES probes
//run against it, and none of that may start depending on a socket.
struct SandboxOptions
{
    bool Connect = false;
    std::string Host = "127.0.0.1";
    std::uint16_t Port = 27015;

    //Round-trip milliseconds. Halved into NetworkSim's one-way latency.
    double LatencyRtt = 0.0;
    float Loss = 0.0f;
};

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

    //Near-black, so the outline reads against both lit terrain and sky.
    const glm::vec4 OutlineColor{ 0.05f, 0.05f, 0.05f, 1.0f };

    //Remote players, as wireframe boxes at their real half extents. Not a
    //character model: modelling is gameplay, and a model chosen now would be a
    //guess. Warm, so it separates from the near-black edit outline.
    const glm::vec4 RemotePlayerColor{ 0.9f, 0.3f, 0.2f, 1.0f };

    //Roughly where to start. Only a column: the height, and whether this exact
    //column is usable at all, are resolved against the loaded map. A hint over
    //a hill or the river moves to the nearest spot that can hold the player
    //rather than burying the camera in terrain — which used to render as a
    //black screen and read as a rendering bug.
    const glm::ivec2 SpawnHintXZ{ 240, 300 };

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
    SandboxLayer(EventBus& eventBus, std::shared_ptr<HudState> hudState,
        const SandboxOptions& options)
        : m_HudState(std::move(hudState)),
          m_Options(options),
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
        // Connected, the map arrives by name in Welcome and MatchClient's
        // loader builds it. Loading here as well would pay the whole 23.8 MB
        // load twice and leave a second world nothing ever reads. A connected
        // launch therefore records a much shorter load, which is honest.
        if (!m_Options.Connect)
        {
            LoadWorld(MapPath);
        }
#ifndef CB_DIST
        Profiler::EndSession();
#endif

        if (m_Options.Connect)
        {
            Connect();
        }
        else
        {
            // LoadWorld resolves the spawn but deliberately does not teleport
            // to it: F9 reloads mid-session and should leave the player where
            // they were working. Starting fresh is the one time it should.
            m_LocalPlayer = m_Match.AddPlayer(m_Spawn);

            // Connected, both of these wait: Player_() throws until the server
            // has said who we are and put us in a snapshot, so the first
            // OnRender that has a player does them instead - the same two
            // calls, through the same helper, not just the position half. The
            // shader below is NOT part of that: it is built either way,
            // because OnRender needs it before it needs a player.
            AimAtMapCentre();
            m_Aimed = true;

            UpdateCameraPosition(1.0f);
        }

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
        ++m_StepsThisFrame;

        // Reading the keyboard is this layer's job, not the controller's: the
        // controller is handed what the player asked for, which is what lets it
        // be stepped by a test with no window and no focus.
        CharacterInput input;
        input.Move = ReadWalkInput();
        input.Yaw = m_CameraController.GetYaw();
        input.Pitch = m_CameraController.GetPitch();
        input.Jump = Input::IsKeyPressed(KeyCode::Space);

        // BRANCH POINT 2 OF 3.
        if (m_Client)
        {
            // Input goes up; the answer comes back. Nothing is simulated here,
            // deliberately: pressing W does not move the view until the server
            // has said so. On 127.0.0.1 that is imperceptible; at --latency 150
            // it is unpleasant, and that is the point of this whole stage. If
            // it feels fine, prediction has grown by accident.
            m_Client->SetInput(input);
            m_Client->Step(timestep.GetSeconds());

            m_LocalPlayer = m_Client->LocalPlayer();

            m_HudState->Connected = m_Client->Connected();
            m_HudState->Rejected = m_Client->Rejected();
            m_HudState->RoundTripMs = m_Client->RoundTripTime() * 1000.0;
            m_HudState->PlayersInMatch = Match_().Players().size();

            if (!HaveLocalPlayer())
                return;
        }
        else
        {
            const PlayerCommand commands[] = { { m_LocalPlayer, input } };
            m_Match.Step(commands, static_cast<float>(timestep.GetSeconds()));
        }

        m_HudState->PlayerPosition = Player_().Position();
        m_HudState->Grounded = Player_().Grounded();
        m_HudState->BodyInFluid = Player_().BodyInFluid();
        m_HudState->EyeInFluid = Player_().EyeInFluid();

        // Falling off the edge of the map is a Sandbox rule rather than
        // character physics, so it stays here. The velocity is cleared
        // separately because Teleport deliberately leaves it alone.
        //
        // Connected, the rule belongs to whoever owns the simulation - the
        // server - so the client does not get to teleport itself. Doing it
        // locally would be a correction the server never made, and the next
        // snapshot would drag the player straight back off the edge.
        if (!m_Client && Player_().Position().y < FallResetHeight)
        {
            m_Match.TeleportPlayer(m_LocalPlayer, m_Spawn);
            m_Match.PlayerForWrite(m_LocalPlayer).SetVerticalVelocity(0.0f);
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
        // Nothing to draw from until the server has said who we are AND put us
        // in a snapshot. Player_() would throw, and until Welcome lands the
        // world is still the 1x1x1 placeholder MatchState was constructed with.
        // See HaveLocalPlayer for why the two halves are separate events.
        if (!HaveLocalPlayer())
            return;

        // The connected path's deferred half of the constructor's camera
        // setup. Single-player has already aimed and set this flag, so this
        // fires exactly once per session either way.
        if (!m_Aimed)
        {
            AimAtMapCentre();
            m_Aimed = true;
        }

        UpdateCameraPosition(alpha);

        m_WorldRenderer.Update(World_());

        Renderer::BeginScene(m_CameraController.GetCamera());
        // u_Transform already carries WorldOffset and the camera position is in
        // that same space — the invariant the transparency sort already relies
        // on — so the two can be subtracted directly.
        m_Shader->SetFloat3("u_FogColor", FogColor);
        m_Shader->SetFloat3("u_CameraPos", m_CameraController.GetCamera().GetPosition());
        m_Shader->SetFloat(
            "u_FogDensity", Player_().EyeInFluid() ? FogDensity : 0.0f);
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
        DrawRemotePlayers(alpha);
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
    //BRANCH POINT 1 OF 3. Everything else in this file reads the match through
    //here, which is what keeps a second mode from spreading across 600 lines.
    //
    //If a fourth branch point appears while working in this file, that is the
    //signal to extract rather than to continue.
    MatchState& Match_() { return m_Client ? m_Client->MatchForWrite() : m_Match; }
    const MatchState& Match_() const { return m_Client ? m_Client->Match() : m_Match; }

    //Shorthands, because the layer reads the world and its own character on
    //nearly every line and Match_().GetWorld() everywhere obscures them.
    World& World_() { return Match_().GetWorld(); }
    const World& World_() const { return Match_().GetWorld(); }
    const CharacterController& Player_() const
    {
        return Match_().Player(m_LocalPlayer);
    }

    //Whether there is a local character to read at all. Every Player_() call on
    //a per-frame path has to be behind this.
    //
    //Connected() is NOT the same question, and mistaking the two is a crash.
    //It goes true the moment Welcome is accepted, but Welcome carries no
    //position, so the local character does not exist until the first SNAPSHOT
    //naming it lands. The server sends both in one Step, which is why they
    //normally arrive together and why testing Connected() alone looked
    //sufficient - but the welcome is reliable and the snapshot is not. Drop or
    //reorder that one packet and the client is connected with an empty roster
    //for a tick, Player_() throws out of a fixed-step callback, and the process
    //goes with it. At the 5% loss this executable can be launched with, that is
    //roughly one join in twenty, not a corner case.
    bool HaveLocalPlayer() const
    {
        if (!m_Client)
            return true;

        return m_Client->Connected() && Match_().HasPlayer(m_LocalPlayer);
    }

    //Opens the socket and builds the client.
    //
    //Throws only when no socket can be made at all: ENet failing to start, a
    //host that cannot be created, a hostname that will not resolve. It does
    //NOT mean the server answered. enet_host_connect is asynchronous - it
    //returns a peer immediately and the Connected event arrives from Poll
    //several Advance calls later - so pointing --connect at a machine with
    //nothing listening on the port succeeds here and always will.
    //
    //What happens then is Rejected(): ENet gives up on the connect attempt and
    //raises a bare Disconnected, MatchClient latches the refusal, and the HUD
    //draws NOT CONNECTED. That is the honest failure path, not this throw.
    void Connect()
    {
        m_Socket = EnetTransport::Connect(m_Options.Host, m_Options.Port);
        if (m_Socket == nullptr)
            throw std::runtime_error("Could not reach the server");

        Transport* transport = m_Socket.get();

        if (m_Options.LatencyRtt > 0.0 || m_Options.Loss > 0.0f)
        {
            NetworkSim sim;
            sim.Latency = m_Options.LatencyRtt / 2000.0;
            sim.Loss = m_Options.Loss;
            m_Simulated = std::make_unique<SimulatedTransport>(*m_Socket, sim);
            transport = m_Simulated.get();
        }

        //The world is loaded by a callback rather than inside MatchClient
        //because this is the only place that knows where the Sandbox keeps its
        //assets. The server names the map; it never sends it.
        m_Client = std::make_unique<MatchClient>(*transport,
            [](const std::string& mapName) -> std::optional<LoadedMap>
            {
                const std::string path = "assets/maps/" + mapName;
                if (!std::filesystem::exists(path))
                    return std::nullopt;

                World world = BuildWorld(VoxLoader::LoadFile(path));

                //Same order the single-player load uses: light before anything
                //meshes, or the first frames bake a dark world into their
                //vertex colours.
                SkyLight::PropagateAll(world);
                return LoadedMap{ std::move(world), HashMapFile(path) };
            });
    }

    //Returns held movement keys in the character's own frame: x strafes, y
    //walks forward. No camera maths here any more - the simulation resolves
    //the direction from the yaw, which is what lets a server reproduce the
    //step rather than trust a vector this machine computed.
    //
    //Deliberately not normalised: Step caps the resolved vector, so pressing
    //two keys is capped there rather than scaled here.
    glm::vec2 ReadWalkInput() const
    {
        glm::vec2 move{ 0.0f };

        if (Input::IsKeyPressed(KeyCode::W))
            move.y += 1.0f;
        if (Input::IsKeyPressed(KeyCode::S))
            move.y -= 1.0f;
        if (Input::IsKeyPressed(KeyCode::D))
            move.x += 1.0f;
        if (Input::IsKeyPressed(KeyCode::A))
            move.x -= 1.0f;

        return move;
    }

    //Places the camera at eye height above the player, in world space, at the
    //point the player occupied `alpha` of the way through the current step.
    void UpdateCameraPosition(float alpha)
    {
        m_CameraController.SetPosition(
            Player_().InterpolatedEye(alpha) + WorldOffset);
    }

    //Faces the middle of the map, level with the eye. Aiming at the literal
    //centre of the world box would tilt the view into the ground.
    //
    //Goes through the controller rather than the camera because both hold a
    //copy of yaw and pitch - the reason SetRotation exists at all.
    void AimAtMapCentre()
    {
        const glm::vec3 eye = Player_().InterpolatedEye(1.0f);
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
        const PerspectiveCamera& camera = m_CameraController.GetCamera();
        const VoxelRayHit hit = VoxelRaycast::Cast(
            World_(),
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

    //Draws everyone else in the match as a wireframe box.
    //
    //Interpolated between the last two snapshots rather than snapped to the
    //newest, which is what MatchClient's SetState call preserves the previous
    //position for. Single-player draws nothing here: there is nobody else.
    void DrawRemotePlayers(float alpha)
    {
        if (!m_Client)
            return;

        for (const auto& [player, character] : Match_().Players())
        {
            if (player == m_LocalPlayer)
                continue;

            const glm::vec3 centre = character.InterpolatedPosition(alpha);
            const glm::vec3 half = character.Config().HalfExtents;
            DebugDraw::Box(centre - half, centre + half, RemotePlayerColor);
        }
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
            World_(),
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

        // BRANCH POINT 3 OF 3.
        if (m_Client)
        {
            // Nothing happens locally. The block disappears when the server
            // says so, one round trip later - which is the most legible
            // demonstration of latency this app has.
            m_Client->RequestEdit(edit);
            return true;
        }

        // Bounds and relighting both belong to ApplyBlockEdit now: an edit is
        // one operation, not a sequence a caller has to remember the rest of.
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(World_(), edit);
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
        // Single-player only. The stack describes edits THIS machine applied,
        // and connected it applied none - every edit it sees came back from the
        // server. Undoing here would mean editing the world behind the server's
        // back, and the next EditApplied would put it straight back.
        if (m_Client)
            return;

        if (m_Undo.empty())
            return;

        const BlockEdit inverse = m_Undo.back();
        m_Undo.pop_back();
        ApplyBlockEdit(World_(), inverse);
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
        // Building locally before handing it to the match means a bad file
        // leaves the current world untouched, rather than half-replaced.
        m_Match.ReplaceWorld(BuildWorld(VoxLoader::LoadFile(path)));

        // The stack describes a world that no longer exists.
        m_Undo.clear();

        // Light has to exist before anything meshes, or the first frames bake
        // a fully dark world into their vertex colours.
        SkyLight::PropagateAll(World_());

        // Before the lift, not after: the lift's last-resort fallback is the
        // spawn, so it has to be valid for the world just loaded.
        ResolveSpawn();

        // The constructor runs LoadWorld before AddPlayer, so on the very
        // first load there is no player yet to lift clear of terrain or
        // centre the camera on - it is about to be placed straight at
        // m_Spawn once AddPlayer runs. Every later call (F9) has a player,
        // and preserving its position clear of the reloaded terrain is the
        // whole reason this tail exists.
        if (m_Match.HasPlayer(m_LocalPlayer))
        {
            LiftPlayerClearOfTerrain();
            m_Match.PlayerForWrite(m_LocalPlayer).SetVerticalVelocity(0.0f);
            UpdateCameraPosition(1.0f);
        }
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
        const float top = static_cast<float>(World_().GetHeight());
        const glm::vec3& halfExtents = Player_().Config().HalfExtents;

        glm::vec3 lifted = Player_().Position();
        while (lifted.y < top &&
            VoxelCollision::Overlaps(World_(), lifted, halfExtents))
            lifted.y += 1.0f;

        // A column solid to the sky has nowhere to stand.
        if (VoxelCollision::Overlaps(World_(), lifted, halfExtents))
            lifted = m_Spawn;

        // A lift is a discontinuity, so it must not be interpolated through.
        m_Match.TeleportPlayer(m_LocalPlayer, lifted);
    }

    //Resolves the spawn hint against the loaded map.
    void ResolveSpawn()
    {
        // Half extents come from a default config rather than the live
        // player: this can run before the player exists (the constructor
        // calls LoadWorld, which calls this, before AddPlayer), and every
        // player the Sandbox ever creates uses the default configuration
        // anyway, so the value is the same either way.
        const glm::vec3 halfExtents = CharacterConfig{}.HalfExtents;

        const std::optional<glm::vec3> found =
            FindSpawn(World_(), SpawnHintXZ, halfExtents);

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
            static_cast<float>(World_().GetHeight()) - halfExtents.y,
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
        // Single-player only, and this one is not merely pointless connected -
        // it is incoherent. LoadWorld replaces m_Match's world, which nothing
        // reads while m_Client exists, but then relights World_(), which
        // resolves to the CLIENT's world. The result is half a reload applied
        // to the wrong one of two worlds. The map a connected session runs is
        // the server's to change.
        if (m_Client)
        {
            CB_INFO("Connected: the server owns the map, so F9 does nothing");
            return;
        }

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
    SandboxOptions m_Options;

    //Present only when connected. The client owns the MatchState everything
    //reads through Match_(); m_Match below is the single-player one and is left
    //untouched while these are alive.
    //
    //Declaration order is destruction order reversed: the client goes first, so
    //it cannot service a transport that has already gone.
    std::unique_ptr<EnetTransport> m_Socket;
    std::unique_ptr<SimulatedTransport> m_Simulated;
    std::unique_ptr<MatchClient> m_Client;

    //The simulation. One player today; the type is the seam a server will
    //step identically, which is why the Sandbox goes through it rather than
    //owning a world and a character directly.
    MatchState m_Match{ World(1, 1, 1) };
    PlayerId m_LocalPlayer = InvalidPlayer;

    //Whether the one-off opening camera aim has happened. Single-player sets it
    //in the constructor; connected, the first OnRender with a player does. It
    //is a latch rather than a re-aim because after that the view belongs to the
    //mouse, and re-running it would yank the player's aim back every frame.
    bool m_Aimed = false;

    WorldRenderer m_WorldRenderer;
    BlockId m_PlaceBlock = BlockId{2};
    glm::vec3 m_Spawn{ 0.0f };
    //Counted across the current frame's steps and published by OnFrameUpdate.
    int m_StepsThisFrame = 0;
    //Inverses of applied edits, newest last. Capped so a long session cannot
    //creep; the oldest entries are the least likely to be wanted back.
    static constexpr std::size_t MaxUndoDepth = 256;
    std::vector<BlockEdit> m_Undo;
    PerspectiveCameraController m_CameraController;
};

class SandboxApplication final : public Application
{
public:
    //Creates the Sandbox layer and publishes a gameplay event.
    explicit SandboxApplication(const SandboxOptions& options)
    {
        //Shared so the overlay can read what the gameplay layer writes, without
        //either layer knowing about the other.
        auto hudState = std::make_shared<HudState>();

        PushLayer(std::make_unique<SandboxLayer>(GetEventBus(), hudState, options));
        PushOverlay(std::make_unique<HudLayer>(
            hudState,
            GetWindow().GetFramebufferWidth(),
            GetWindow().GetFramebufferHeight()));
        GetEventBus().Publish(PlayerDiedEvent{ 1, 2 });
    }
};

//Starts the Sandbox application and runs the engine loop.
//
//With no arguments this is the single-player app exactly as it has always
//been, with no socket anywhere in it. That is load-bearing rather than polite:
//the project's whole rendering verification story is scripted runs of this
//executable checking POS and FACES, and none of it may start needing a server.
int main(int argc, char** argv)
{
    SandboxOptions options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--connect" && i + 1 < argc)
        {
            options.Connect = true;
            options.Host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
            options.Port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--latency" && i + 1 < argc)
            options.LatencyRtt = std::atof(argv[++i]);
        else if (arg == "--loss" && i + 1 < argc)
            options.Loss = static_cast<float>(std::atof(argv[++i])) / 100.0f;
    }

    SandboxApplication app(options);
    app.Run();

    return 0;
}
