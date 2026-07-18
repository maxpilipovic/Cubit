#include "Cubit/Cubit.h"

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

        Chunk chunk;
        BuildTestTerrain(chunk);

        const ChunkMeshData mesh = ChunkMesher::Build(chunk);
        CB_INFO(
            std::string("Chunk meshed into ") +
            std::to_string(mesh.Vertices.size()) + " vertices and " +
            std::to_string(mesh.Indices.size() / 3) + " triangles");

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

        // Places the chunk ahead of and below the camera's starting position.
        m_ChunkTransform = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(-8.0f, -12.0f, -28.0f));

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

    //Polls held movement input independently from routed key events.
    void OnUpdate(Timestep timestep) override
    {
        m_CameraController.OnUpdate(timestep);
    }

    //Draws the meshed voxel chunk through Cubit's scene renderer.
    void OnRender() override
    {
        Renderer::BeginScene(m_CameraController.GetCamera());
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
    }

private:
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
    glm::mat4 m_ChunkTransform{ 1.0f };
    PerspectiveCameraController m_CameraController;
};

class SandboxApplication final : public Application
{
public:
    //Creates the Sandbox layer and publishes a gameplay event.
    SandboxApplication()
    {
        PushLayer(std::make_unique<SandboxLayer>(GetEventBus()));
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
