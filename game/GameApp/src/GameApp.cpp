#include "Cubit/Cubit.h"
#include "Cubit/Net/EnetTransport.h"
#include "Cubit/Net/MapHash.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Renderer/Mesh.h"
#include "Cubit/Voxel/ModelMesher.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxWriter.h"

#include "DeathAnnouncer.h"
#include "GameHudLayer.h"
#include "GameOptions.h"
#include "GameRules.h"

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
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    //The rules this game is played by, read once. Both this client and any
    //server it joins come from one build, which is what keeps the reach the
    //prediction uses equal to the reach the server rules by.
    const MatchRules Rules = CubitGame::Rules();

    //Centre the 128x48x128 map roughly on the origin for the view.
    const glm::vec3 WorldOffset{ -64.0f, -24.0f, -64.0f };

    //Near-black, so the outline reads against both lit terrain and sky.
    const glm::vec4 OutlineColor{ 0.05f, 0.05f, 0.05f, 1.0f };

    //The local tracer, drawn the instant the fire button goes down.
    const glm::vec4 TracerColor{ 1.0f, 0.9f, 0.4f, 1.0f };

    //The server's ruling, drawn where the shot stopped: red where it named a
    //victim, grey where it did not.
    const glm::vec4 ImpactHitColor{ 1.0f, 0.15f, 0.15f, 1.0f };
    const glm::vec4 ImpactMissColor{ 0.6f, 0.6f, 0.6f, 1.0f };
    constexpr float ImpactHalfSize = 0.1f;

    //How long a tracer and a ruling stay on screen, in simulation ticks rather
    //than frames. Debug renders at 144 fps and Release faster, so "a few
    //frames" would be a different - and nearly invisible - length on each.
    constexpr std::uint64_t TracerTicks = 12;
    constexpr std::uint64_t ShotMarkerTicks = 45;

    //Where the tracer is drawn FROM, relative to the eye, in blocks. A line
    //from the eye along the view direction projects onto a single point under
    //the crosshair, so the player who fired it could never see it. Purely
    //visual: the shot itself leaves from the eye, on both ends of the wire.
    constexpr float TracerMuzzleRight = 0.25f;
    constexpr float TracerMuzzleDown = 0.2f;

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

}

class PlayerLayer final : public Layer
{
public:
    //Subscribes the player layer to typed gameplay notifications.
    PlayerLayer(EventBus& eventBus, std::shared_ptr<GameHudState> hudState,
        const GameOptions& options)
        : m_EventBus(eventBus),
          m_HudState(std::move(hudState)),
          m_Options(options),
          m_CameraController(16.0f / 9.0f)
    {
        Input::SetCursorCaptured(m_Cursor.Captured());

        //Held as a member: the callback captures `this`, so the subscription must
        //end when this layer does.
        m_DeathSubscription = eventBus.Subscribe<CubitGame::PlayerDiedEvent>(
            [this](const CubitGame::PlayerDiedEvent& event)
            {
                OnPlayerDied(event);
            });

        //Load is the phase worth a capture: it is one-shot, it is the largest
        //remaining cost in the engine, and it is what docs/performance.md P8
        //tabulates. Written beside the executable, like the assets it loads.
        //Opened before the first asset is touched rather than after, so "load"
        //in the capture means the same thing as load does here — a session that
        //starts partway through would quietly stop tabulating whatever moved
        //above it.
        //
        //Guarded on CB_DIST even though the macros already compile out under it:
        //BeginSession/EndSession themselves are not macros, so left unguarded
        //they would still open a session, record nothing, and write an empty
        //profile-load.json beside a shipped executable on every launch.
#ifndef CB_DIST
        Profiler::BeginSession("load", "profile-load.json");
#endif

        //Loaded once and drawn for every remote player. A missing file throws, the
        //same way a missing map does: it is an asset the game ships, and falling
        //back to wireframes would hide a broken build rather than report it.
        const VoxModel model = VoxLoader::LoadFile("assets/models/player.vox");

        //Same argument one line up, for a file that parsed but says the figure
        //is nothing tall: the draw scales by this height, so a zero would put an
        //inf through the transform and draw garbage somewhere off in space.
        //A broken asset should say so here, not be inferred from a missing
        //player later.
        CB_ASSERT(model.Size.y > 0, "The player model needs a height to be scaled by");

        m_PlayerModelHeight = static_cast<float>(model.Size.y);
        //model.Size.x is front-to-back and model.Size.z is shoulder-to-shoulder
        //for a figure authored facing +x, which is the opposite of what their
        //ordinary English names would suggest.
        m_PlayerModelDepth = static_cast<float>(model.Size.x);
        m_PlayerModelWidth = static_cast<float>(model.Size.z);
        m_PlayerMesh = std::make_unique<Mesh>(ModelMesher::Build(model));

        //The world starts with every chunk dirty, so the first render meshes it.
        //A game that cannot load its map has nothing to do, so this
        //does not catch — the failure propagates out of the constructor.
        //
        // Connected, the map arrives by name in Welcome and MatchClient's
        // loader builds it. Loading here as well would pay the whole 23.8 MB
        // load twice and leave a second world nothing ever reads. A connected
        // launch therefore records a much shorter load, which is honest.
        if (!m_Options.Connect)
        {
            LoadWorld(CubitGame::MapPath);
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
            // calls, through the same helper, not just the position half. The world
            // scene is NOT part of that: its shader is built with the scene
            // either way, because OnRender needs it before it needs a player.
            AimAtMapCentre();
            m_Aimed = true;

            UpdateCameraPosition(1.0f);
        }

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
        const CharacterInput input = ReadInput();

        // BRANCH POINT 2 OF 3.
        if (m_Client)
        {
            // Through Stage 2, nothing was simulated here, deliberately:
            // pressing W did not move the view until the server had said so,
            // so the latency was plainly visible rather than hidden behind a
            // guess. This is the stage that ends that: Step below applies
            // this input locally and immediately, so W moves the view on the
            // same frame even at --latency 150.
            //
            // The server is still the authority throughout - the local move
            // is a guess, held until acknowledged and corrected against every
            // snapshot (CorrectionThreshold's deadzone means "corrected"
            // still allows a little disagreement to pass unremarked, so the
            // client is not exactly the server between snapshots even when it
            // feels like it is). Feeling right is what a correct guess looks
            // like, not proof there isn't one being made.
            m_Client->SetInput(input);
            m_Client->Step(timestep.GetSeconds());

            m_LocalPlayer = m_Client->LocalPlayer();

            m_HudState->Connected = m_Client->Connected();
            m_HudState->Rejected = m_Client->Rejected();
            m_HudState->Disconnected = m_Client->Disconnected();
            m_HudState->RoundTripMs = m_Client->RoundTripTime() * 1000.0;
            m_HudState->PlayersInMatch = Match_().Players().size();
            m_HudState->Health = m_Client->LocalHealth();

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

        // Falling off the edge of the map is a game rule rather than
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
        m_StepsThisFrame = 0;

        AnnounceDeaths();
    }

    //Publishes one death for each new killing ruling the server sends.
    //
    //Done here rather than in DrawShots, which reads the same ruling: that
    //gives up early once the impact marker's window has passed, so a frame
    //lost to a stall would drop the announcement with it. A marker is a
    //decoration and can be missed; a death is not.
    void AnnounceDeaths()
    {
        if (!m_Client || !m_Client->LastShot().has_value())
            return;

        const std::optional<CubitGame::PlayerDiedEvent> died =
            m_DeathAnnouncer.Observe(*m_Client->LastShot());

        if (died.has_value())
            m_EventBus.Publish(*died);
    }

    //Draws the meshed voxel world through Cubit's scene renderer.
    void OnRender(float alpha) override
    {
        // Kept before the early return below, so a click that lands before the
        // first snapshot still has this frame's alpha rather than a stale one.
        m_LastAlpha = alpha;

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

        m_Scene.Update(World_());
        m_Scene.Render(
            m_CameraController.GetCamera(),
            WorldOffset,
            FogColor,
            Player_().EyeInFluid() ? FogDensity : 0.0f);

        // Flushed here, while the world camera is current. The HUD overlay
        // renders after this layer and leaves an orthographic matrix behind, so
        // a later flush would draw these lines in screen space.
        DrawTargetedBlockOutline();
        DrawRemotePlayers(alpha);
        DrawShots();
        DebugDraw::Flush(m_CameraController.GetCamera(), glm::translate(glm::mat4(1.0f), WorldOffset));

        m_HudState->MeshFaceCount = m_Scene.TotalFaceCount();
        m_HudState->DrawnChunks = m_Scene.DrawnChunkCount();
        m_HudState->TotalChunks = m_Scene.TotalChunkCount();
        m_HudState->PendingChunks = m_Scene.PendingCount();
    }

    //Routes one-time key presses through the typed platform dispatcher.
    void OnEvent(Event& event) override
    {
        //Mouse-look only while the game has the mouse: a released cursor is the
        //player pointing at something else.
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
                //never also edits or fires.
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

    //The stage's acceptance number, from a real run rather than a test.
    //
    //Logged at detach rather than drawn on the HUD: it is a whole-run figure,
    //read once the run is over. It was first kept off the HUD because the
    //debug font could not draw most of its words; the font can now, so that
    //reason no longer holds on its own.
    void OnDetach() override
    {
        if (!m_Client)
            return;

        const MatchClient::CorrectionStats stats = m_Client->Corrections();
        const double perThousand = stats.Snapshots == 0
            ? 0.0
            : 1000.0 * static_cast<double>(stats.Count) / static_cast<double>(stats.Snapshots);

        CB_INFO("NETSTATS snapshots=" + std::to_string(stats.Snapshots)
            + " corrections=" + std::to_string(stats.Count)
            + " per1000=" + std::to_string(perThousand)
            + " mean=" + std::to_string(stats.Mean)
            + " max=" + std::to_string(stats.Max));
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
        //because this is the only place that knows where the game keeps its
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
            },
            Rules);
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
            Rules.ReachDistance,
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

    //Draws everyone else in the match as the player model.
    //
    //Drawn from MatchClient's interpolation ring, six ticks behind the newest
    //server tick this client has seen, rather than snapped to the newest or
    //extrapolated past it. Single-player draws nothing here: there is nobody
    //else.
    void DrawRemotePlayers(float alpha)
    {
        if (!m_Client)
            return;

        for (const auto& [player, character] : Match_().Players())
        {
            if (player == m_LocalPlayer)
                continue;

            //From the interpolation ring rather than from the character, which
            //holds whatever the last snapshot said and steps between packets.
            //The character is still what supplies the size: how big a player is
            //is simulation, where they are drawn is not.
            const MatchClient::RemotePose pose = m_Client->PoseOf(player, alpha);
            const glm::vec3 half = character.Config().HalfExtents;

            //The model is meshed in its own voxel units, so it is scaled to the
            //height the simulation says a player is. An artist can rebuild the
            //model at any resolution and it still fits: nothing here knows how
            //many voxels tall it is except by asking the file.
            const float scale = (half.y * 2.0f) / m_PlayerModelHeight;

            //Feet at the bottom of the collision box, centred on it, turned to
            //face where the player faces. The model is authored facing +x, which
            //is what yaw 0 means everywhere else in Cubit (see Heading.h).
            //
            //Negated: glm::rotate turns +x toward -z as its angle grows, while
            //Heading.h's yaw turns +x toward +z, so undoing that mismatch takes
            //-pose.Yaw here, not +pose.Yaw. Checked against Heading.h's formula
            //by hand (rotating the model's local +x by -yaw reproduces
            //HeadingForward(yaw) exactly) and then against a fixed-position,
            //fixed-yaw probe standing next to the player in single-player: at
            //yaw 90 the model turned a quarter-turn from its yaw-0 pose, the
            //way HeadingForward(90) turning toward +z says it should.
            glm::mat4 transform = glm::translate(glm::mat4(1.0f),
                WorldOffset + pose.Position - glm::vec3(0.0f, half.y, 0.0f));
            transform = glm::rotate(transform, glm::radians(-pose.Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(scale));
            transform = glm::translate(transform,
                glm::vec3(-0.5f * m_PlayerModelDepth, 0.0f, -0.5f * m_PlayerModelWidth));

            m_Scene.DrawMesh(*m_PlayerMesh, transform, BrightnessAt(pose.Position));
        }
    }

    //How lit a model standing here should be: the world's sky light where its
    //middle is, with a floor so someone in a sealed tunnel is dim rather than
    //invisible. One sample for the whole model - it is a person, not terrain,
    //and re-shading its vertices every frame it moves would cost far more than
    //this is worth.
    //
    //This floor is not the floor ChunkMesher::LightFloor documents, and a model
    //in the dark is darker than the wall behind it. A chunk vertex is floored
    //once, on the finished product of face shade, AO and light. A model is
    //floored twice on two separate terms: ModelMesher bakes
    //LightFloor + 0.85 * (shade * AO) at mesh time, and this floors the sky
    //term again, so what reaches the screen is their product. In a sealed
    //tunnel that is 0.881 * 0.15 = 0.132 on an open front face and
    //0.4305 * 0.15 = 0.065 on a fully occluded bottom one, against the 0.15 a
    //chunk face beside it is guaranteed. At full sky light the two agree
    //exactly, which is why this is only visible in the dark.
    //
    //Not fixed here because no per-draw multiplier can floor a product whose
    //other half varies per vertex. The fix is for the model to bake raw
    //shade * AO and the shader to apply the floor after multiplying by light,
    //which needs the shader to know a model vertex from a chunk one. Written up
    //as B3c in docs/engine-roadmap.md.
    float BrightnessAt(const glm::vec3& position) const
    {
        const glm::ivec3 cell = glm::ivec3(glm::floor(position));
        const float light = static_cast<float>(World_().GetSkyLight(cell.x, cell.y, cell.z))
            / static_cast<float>(SkyLight::Max);

        return ChunkMesher::LightFloor + (1.0f - ChunkMesher::LightFloor) * light;
    }

    //What the player is asking for this instant: the movement keys held, the
    //camera's aim, and jump. One place, because a shot has to send the same
    //aim a step would.
    CharacterInput ReadInput()
    {
        CharacterInput input;
        input.Move = ReadWalkInput();
        input.Yaw = m_CameraController.GetYaw();
        input.Pitch = m_CameraController.GetPitch();
        input.Jump = Input::IsKeyPressed(KeyCode::Space);
        return input;
    }

    //Fires along the camera's view ray: the local tracer now, and the shot to
    //the server when connected. Single-player has nobody to shoot, so the
    //tracer is all there is - drawn regardless, so the binding is visibly
    //alive.
    void FireShot()
    {
        const PerspectiveCamera& camera = m_CameraController.GetCamera();
        const glm::vec3 eye = camera.GetPosition() - WorldOffset;
        const glm::vec3 forward = camera.GetForwardDirection();

        if (m_Client)
        {
            // Fire sends the yaw and pitch of the INPUT, which OnFixedUpdate
            // last set from the camera up to a whole step ago - and the mouse
            // moves between steps. Refreshing it here sends the aim the
            // crosshair shows on this frame, which is also the direction the
            // tracer below is drawn along. The next step reads it afresh.
            m_Client->SetInput(ReadInput());
            m_Client->Fire(m_LastAlpha);
        }

        // Solid only, like an edit: water does not stop a shot.
        const VoxelRayHit hit = VoxelRaycast::Cast(World_(), eye, forward, Rules.ShotRange, true);
        const float length = hit.Hit ? hit.Distance : Rules.ShotRange;

        // Pitch is clamped short of straight up or down, so forward is never
        // parallel to the world's up and this cross product never vanishes.
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::cross(right, forward);

        m_TracerFrom = eye + right * TracerMuzzleRight - up * TracerMuzzleDown;
        m_TracerTo = eye + forward * length;
        m_TracerTick = Match_().Tick();
        m_TracerActive = true;
    }

    //The local tracer while it lasts, and the server's ruling on the most
    //recent shot anybody fired.
    //
    //The two are deliberately separate. The tracer is this machine's own trace
    //and appears the instant the button goes down. Whether it HIT anybody is
    //the server's to say, and a hit marker that has to be taken back is worse
    //than one a round trip late - so the marker and the HUD word come only
    //from LastShot.
    void DrawShots()
    {
        const std::uint64_t now = Match_().Tick();

        if (m_TracerActive && now >= m_TracerTick && now - m_TracerTick < TracerTicks)
            DebugDraw::Line(m_TracerFrom, m_TracerTo, TracerColor);
        else
            m_TracerActive = false;

        m_HudState->ShotLabel.clear();

        if (!m_Client || !m_Client->LastShot().has_value())
            return;

        // ReceivedAtTick is on this client's own clock, which is the one
        // Match_() reads connected - so the two can be subtracted directly.
        const MatchClient::ShotReport& shot = *m_Client->LastShot();
        if (now < shot.ReceivedAtTick || now - shot.ReceivedAtTick >= ShotMarkerTicks)
            return;

        const bool connected = shot.Victim != InvalidPlayer;
        const glm::vec3 half(ImpactHalfSize);
        DebugDraw::Box(shot.Impact - half, shot.Impact + half,
            connected ? ImpactHitColor : ImpactMissColor);

        // Every client receives every ruling, so everybody's impacts draw
        // above. Only this player's own shots put a word on the HUD.
        if (connected && shot.Shooter == m_LocalPlayer)
            m_HudState->ShotLabel = shot.Killed ? "KILLED" : "HIT";
    }

    //Breaks or places a block along the camera's view ray.
    bool OnMouseButtonPressed(MouseButtonPressedEvent& event)
    {
        const MouseCode button = event.GetMouseButton();

        // Middle rather than left, and the reason is verification rather than
        // ergonomics: a script can drive the mouse but NOT the keyboard, so a
        // shot bound to a key would be the one feature nobody can screenshot.
        // It also leaves both edit paths below byte-for-byte as they were.
        if (button == MouseCode::Middle)
        {
            if (!HaveLocalPlayer())
                return false;

            FireShot();
            return true;
        }

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
            Rules.ReachDistance,
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
            // Predicted, not waited for. MatchClient checks the edit against
            // the same rules the server runs and shows it on the next step if
            // it is legal; the server applies it on the same tick and only a
            // refusal ever takes it back. Single-player below still applies
            // edits directly and keeps its undo stack.
            // Requested, not done: the line below only asks. Single-player
            // logs the edit it applied, and without this the connected path
            // logged nothing at all - so a click that never became an edit and
            // a click that never happened looked identical in the log, which
            // cost a verification session on 2026-09-18.
            CB_INFO(
                std::string("Requested ") + (button == MouseCode::Left ? "break" : "place") +
                " at " + std::to_string(target.x) + "," +
                std::to_string(target.y) + "," + std::to_string(target.z));

            m_Client->RequestEdit(edit);
            return true;
        }

        // Bounds and relighting both belong to ApplyBlockEdit now: an edit is
        // one operation, not a sequence a caller has to remember the rest of.
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(World_(), edit);
        if (!inverse)
            return false;

        // What a dig left hanging comes down with it. Only a dig: placing a
        // block cannot take anything's support away.
        if (button == MouseCode::Left)
            CollapseAfter({ target });

        CB_INFO(
            std::string(button == MouseCode::Left ? "Broke" : "Placed") +
            " block at " + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z));

        return true;
    }

    //Clears whatever the cells just emptied have left with nothing holding it
    //up, and returns how to put that back.
    //
    //Single-player only, like the undo stack: connected, the server decides what
    //comes loose and sends it, and working it out here as well would be editing
    //the world behind the server's back.
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

    //Logs a player-death notification received from the gameplay event bus.
    void OnPlayerDied(const CubitGame::PlayerDiedEvent& event)
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
        // player the game ever creates uses the default configuration
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

    //Hands the cursor to the desktop or takes it back, to match m_Cursor.
    void ApplyCursor()
    {
        Input::SetCursorCaptured(m_Cursor.Captured());

        //The cursor moved freely while it was released, so the last position
        //mouse-look saw would turn into one large jump of the view.
        if (m_Cursor.Captured())
            m_CameraController.ResetMouseTracking();
    }

    //Selects the colour used when placing blocks, or logs an unhandled press.
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

        const int key = static_cast<int>(event.GetKeyCode());
        const int first = static_cast<int>(KeyCode::D1);
        if (key >= first && key < first + PlaceableBlockCount)
        {
            m_PlaceBlock = PlaceableBlocks[key - first];
            return true;
        }

        return false;
    }

    //The application's bus, which outlives every layer on it. Held so a death
    //ruled by the server can be published, not only listened for.
    EventBus& m_EventBus;
    std::shared_ptr<GameHudState> m_HudState;
    GameOptions m_Options;

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
    //step identically, which is why the game goes through it rather than
    //owning a world and a character directly.
    MatchState m_Match{ World(1, 1, 1) };
    PlayerId m_LocalPlayer = InvalidPlayer;

    //Whether the one-off opening camera aim has happened. Single-player sets it
    //in the constructor; connected, the first OnRender with a player does. It
    //is a latch rather than a re-aim because after that the view belongs to the
    //mouse, and re-running it would yank the player's aim back every frame.
    bool m_Aimed = false;

    //The renderer's position within the current step, as of the last frame.
    //Fire must be handed the alpha PoseOf drew remote players with, and a
    //click arrives between frames, so this is the frame the player saw.
    float m_LastAlpha = 1.0f;

    //The local tracer, in world space, and the tick it was fired on.
    glm::vec3 m_TracerFrom{ 0.0f };
    glm::vec3 m_TracerTo{ 0.0f };
    std::uint64_t m_TracerTick = 0;
    bool m_TracerActive = false;

    WorldScene m_Scene;

    //The player model, meshed once at load and drawn at every remote player's
    //pose. Dimensions read from the file rather than hardcoded, so rebuilding
    //the model at a different resolution or proportions needs no code change.
    std::unique_ptr<Mesh> m_PlayerMesh;
    float m_PlayerModelHeight = 0.0f;
    float m_PlayerModelDepth = 0.0f;
    float m_PlayerModelWidth = 0.0f;

    BlockId m_PlaceBlock = BlockId{2};
    glm::vec3 m_Spawn{ 0.0f };
    //Counted across the current frame's steps and published by OnFrameUpdate.
    int m_StepsThisFrame = 0;
    PerspectiveCameraController m_CameraController;

    //Whether the game has the mouse. Escape and losing focus give it back; a
    //click takes it again.
    CursorCapture m_Cursor;

    //This layer's place on the gameplay event bus, ended by its destructor.
    Subscription m_DeathSubscription;

    //Which killing ruling has already been announced. LastShot holds the most
    //recent one for as long as its marker is drawn, so without this the same
    //death would be published every frame of that window.
    CubitGame::DeathAnnouncer m_DeathAnnouncer;
};

class GameApplication final : public Application
{
public:
    //Creates the game layers.
    explicit GameApplication(const GameOptions& options)
    {
        //Shared so the overlay can read what the gameplay layer writes, without
        //either layer knowing about the other.
        auto hudState = std::make_shared<GameHudState>();

        PushLayer(std::make_unique<PlayerLayer>(GetEventBus(), hudState, options));
        PushOverlay(std::make_unique<GameHudLayer>(
            hudState,
            GetWindow().GetFramebufferWidth(),
            GetWindow().GetFramebufferHeight()));
    }
};

//Starts the game and runs the engine loop.
//
//With no arguments this is the single-player app exactly as it has always
//been, with no socket anywhere in it. That is load-bearing rather than polite:
//the project's whole rendering verification story is scripted runs of this
//executable checking POS and FACES, and none of it may start needing a server.
int main(int argc, char** argv)
{
    CrashHandler::Install("Game");
    Logger::OpenFile("Game");

    GameOptions options;

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

    GameApplication app(options);
    app.Run();

    return 0;
}
