#include "Cubit/Cubit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct PlayerDiedEvent
{
    int Player;
    int Killer;
};

class SandboxLayer final : public Layer
{
public:
    //Subscribes the Sandbox layer to typed gameplay notifications.
    explicit SandboxLayer(EventBus& eventBus)
        : m_CameraController(16.0f / 9.0f)
    {
        eventBus.Subscribe<PlayerDiedEvent>(
            //Logs each player-death notification when it is published.
            [](const PlayerDiedEvent& event)
            {
                CB_INFO(
                    std::string("Player ") + std::to_string(event.Player) +
                    " was defeated by player " + std::to_string(event.Killer));
            });

        const float vertices[] =
        {
            // Position               Color
            -0.5f, -0.5f, -0.5f,      0.15f, 0.45f, 0.95f,
             0.5f, -0.5f, -0.5f,      0.20f, 0.80f, 0.45f,
             0.5f,  0.5f, -0.5f,      0.95f, 0.75f, 0.20f,
            -0.5f,  0.5f, -0.5f,      0.85f, 0.25f, 0.35f,
            -0.5f, -0.5f,  0.5f,      0.45f, 0.25f, 0.90f,
             0.5f, -0.5f,  0.5f,      0.10f, 0.75f, 0.80f,
             0.5f,  0.5f,  0.5f,      0.95f, 0.40f, 0.15f,
            -0.5f,  0.5f,  0.5f,      0.55f, 0.85f, 0.25f
        };
        const std::uint32_t indices[] =
        {
            0, 1, 2, 2, 3, 0,
            4, 6, 5, 6, 4, 7,
            0, 4, 5, 5, 1, 0,
            3, 2, 6, 6, 7, 3,
            1, 5, 6, 6, 2, 1,
            0, 3, 7, 7, 4, 0
        };

        m_VertexArray = std::make_unique<VertexArray>();
        m_VertexBuffer = std::make_unique<VertexBuffer>(
            vertices,
            static_cast<std::uint32_t>(sizeof(vertices)));
        m_VertexArray->AddBuffer(
            *m_VertexBuffer,
            BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float3 });
        m_IndexBuffer = std::make_unique<IndexBuffer>(indices, 36);

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

    //Draws an indexed cube through Cubit's scene renderer.
    void OnRender() override
    {
        Renderer::BeginScene(m_CameraController.GetCamera());
        Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader);

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
