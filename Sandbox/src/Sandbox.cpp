#include "Cubit/Cubit.h"

#include "HudLayer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct PlayerDiedEvent
{
    int Player;
    int Killer;
};

namespace
{
    //Chunk-space origin of the rendered chunk, in world units. Placed so the
    //camera starts looking straight at terrain that is within editing reach.
    const glm::vec3 ChunkOrigin{ -8.0f, -6.0f, -18.0f };

    //How far the player can reach to edit terrain, in blocks.
    constexpr float ReachDistance = 12.0f;

    //Half extents of the player's 0.6 x 1.8 x 0.6 collision box.
    const glm::vec3 PlayerHalfExtents{ 0.3f, 0.9f, 0.3f };

    //Where the player starts, in chunk space, above the terrain surface.
    const glm::vec3 SpawnPosition{ 8.5f, 12.0f, 8.5f };

    //Eye height above the centre of the player box.
    constexpr float EyeOffset = 0.7f;

    constexpr float WalkSpeed = 5.0f;
    constexpr float JumpSpeed = 9.0f;
    constexpr float Gravity = 24.0f;

    //Height below the chunk at which a fallen player is returned to spawn.
    constexpr float FallResetHeight = -20.0f;

    //Returns how many solid blocks are stacked in one chunk column.
    int GetTerrainHeight(int x, int z)
    {
        const float wave =
            std::sin(static_cast<float>(x) * 0.35f) +
            std::cos(static_cast<float>(z) * 0.35f);

        return 6 + static_cast<int>(std::lround(wave * 2.0f));
    }

    //Fills a chunk with test data whose buried blocks exercise face culling.
    //This is sandbox fixture data, not a world generator.
    void BuildTestTerrain(Chunk& chunk)
    {
        for (int z = 0; z < Chunk::Depth; ++z)
        {
            for (int x = 0; x < Chunk::Width; ++x)
            {
                const int height = GetTerrainHeight(x, z);

                for (int y = 0; y < height; ++y)
                    chunk.SetBlock(x, y, z, BlockType::Solid);
            }
        }
    }
}

class SandboxLayer final : public Layer
{
public:
    //Subscribes the Sandbox layer to typed gameplay notifications.
    explicit SandboxLayer(EventBus& eventBus)
        : m_CameraController(16.0f / 9.0f)
    {
        Input::SetCursorCaptured(true);

        eventBus.Subscribe<PlayerDiedEvent>(
            [this](const PlayerDiedEvent& event)
            {
                OnPlayerDied(event);
            });

        BuildTestTerrain(m_Chunk);
        RebuildMesh();

        m_ChunkTransform = glm::translate(glm::mat4(1.0f), ChunkOrigin);
        UpdateCameraPosition();

        constexpr std::string_view vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Color;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;
            out vec3 v_Color;

            void main()
            {
                v_Color = a_Color;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";
        constexpr std::string_view fragmentSource = R"(
            #version 330 core
            layout(location = 0) out vec4 color;
            in vec3 v_Color;

            void main()
            {
                color = vec4(v_Color, 1.0);
            }
        )";
        m_Shader = std::make_unique<Shader>(vertexSource, fragmentSource);
    }

    //Walks the player through the chunk under gravity and moves the camera to
    //the resulting eye position.
    void OnUpdate(Timestep timestep) override
    {
        const float seconds = static_cast<float>(timestep.GetSeconds());
        const glm::vec3 walk = ReadWalkInput() * WalkSpeed;

        if (m_Grounded && Input::IsKeyPressed(KeyCode::Space))
            m_VerticalVelocity = JumpSpeed;

        m_VerticalVelocity -= Gravity * seconds;

        const VoxelMoveResult move = VoxelCollision::MoveBox(
            m_Chunk,
            m_PlayerPosition,
            PlayerHalfExtents,
            glm::vec3(walk.x, m_VerticalVelocity, walk.z) * seconds);

        m_PlayerPosition = move.Position;
        m_Grounded = move.Grounded;

        // Landing or hitting a ceiling ends vertical motion.
        if (move.BlockedY)
            m_VerticalVelocity = 0.0f;

        // The chunk is not a closed world, so a player can walk off its edge.
        if (m_PlayerPosition.y < FallResetHeight)
        {
            m_PlayerPosition = SpawnPosition;
            m_VerticalVelocity = 0.0f;
        }

        UpdateCameraPosition();
    }

    //Draws the meshed voxel chunk through Cubit's scene renderer.
    void OnRender() override
    {
        Renderer::BeginScene(m_CameraController.GetCamera());

        // An emptied chunk meshes to nothing, leaving no geometry to draw.
        if (m_IndexBuffer->GetCount() > 0)
            Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader, m_ChunkTransform);

        Renderer::EndScene();
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

    //Places the camera at eye height above the player, in world space.
    void UpdateCameraPosition()
    {
        m_CameraController.SetPosition(
            m_PlayerPosition + ChunkOrigin + glm::vec3(0.0f, EyeOffset, 0.0f));
    }

    //Rebuilds the chunk mesh and replaces the GPU buffers holding it.
    //Recreating the buffers is cheap at one chunk and avoids sizing them up front.
    void RebuildMesh()
    {
        const ChunkMeshData mesh = ChunkMesher::Build(m_Chunk);

        m_VertexArray = std::make_unique<VertexArray>();
        m_VertexBuffer = std::make_unique<VertexBuffer>(
            mesh.Vertices.data(),
            static_cast<std::uint32_t>(mesh.Vertices.size() * sizeof(VoxelVertex)));
        m_VertexArray->AddBuffer(
            *m_VertexBuffer,
            BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float3 });
        m_IndexBuffer = std::make_unique<IndexBuffer>(
            mesh.Indices.data(),
            static_cast<std::uint32_t>(mesh.Indices.size()));
    }

    //Breaks or places a block along the camera's view ray.
    bool OnMouseButtonPressed(MouseButtonPressedEvent& event)
    {
        const MouseCode button = event.GetMouseButton();
        if (button != MouseCode::Left && button != MouseCode::Right)
            return false;

        const PerspectiveCamera& camera = m_CameraController.GetCamera();
        const VoxelRayHit hit = VoxelRaycast::Cast(
            m_Chunk,
            camera.GetPosition() - ChunkOrigin,
            camera.GetForwardDirection(),
            ReachDistance);

        if (!hit.Hit)
            return false;

        const glm::ivec3 target = button == MouseCode::Left
            ? hit.Block
            : hit.Block + hit.Normal;

        // A ray starting inside a block has no entry face, so there is nowhere
        // to place against.
        if (button == MouseCode::Right && hit.Normal == glm::ivec3(0))
            return false;

        if (!Chunk::IsInBounds(target.x, target.y, target.z))
            return false;

        m_Chunk.SetBlock(
            target.x,
            target.y,
            target.z,
            button == MouseCode::Left ? BlockType::Air : BlockType::Solid);
        RebuildMesh();

        CB_INFO(
            std::string(button == MouseCode::Left ? "Broke" : "Placed") +
            " block at " + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z) +
            " (mesh now " + std::to_string(m_IndexBuffer->GetCount() / 6) +
            " faces)");

        return true;
    }

    //Logs a player-death notification received from the gameplay event bus.
    void OnPlayerDied(const PlayerDiedEvent& event)
    {
        CB_INFO(
            std::string("Player ") + std::to_string(event.Player) +
            " was defeated by player " + std::to_string(event.Killer));
    }

    //Logs a non-repeated key press without consuming it from lower layers.
    bool OnKeyPressed(KeyPressedEvent& event)
    {
        if (!event.IsRepeat())
            CB_INFO("Sandbox received a key press");

        return false;
    }

    std::unique_ptr<VertexArray> m_VertexArray;
    std::unique_ptr<VertexBuffer> m_VertexBuffer;
    std::unique_ptr<IndexBuffer> m_IndexBuffer;
    std::unique_ptr<Shader> m_Shader;
    Chunk m_Chunk;
    glm::mat4 m_ChunkTransform{ 1.0f };
    glm::vec3 m_PlayerPosition{ SpawnPosition };
    float m_VerticalVelocity = 0.0f;
    bool m_Grounded = false;
    PerspectiveCameraController m_CameraController;
};

class SandboxApplication final : public Application
{
public:
    //Creates the Sandbox layer and publishes a gameplay event.
    SandboxApplication()
    {
        PushLayer(std::make_unique<SandboxLayer>(GetEventBus()));
        PushOverlay(std::make_unique<HudLayer>(
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
