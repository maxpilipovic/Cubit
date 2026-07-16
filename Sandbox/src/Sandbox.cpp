#include "Cubit/Cubit.h"

#include <glm/gtc/matrix_transform.hpp>
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
        : m_CameraController(16.0f / 9.0f, true)
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
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
             0.0f,  0.5f, 0.0f
        };
        const std::uint32_t indices[] = { 0, 1, 2 };

        m_VertexArray = std::make_unique<VertexArray>();
        m_VertexBuffer = std::make_unique<VertexBuffer>(
            vertices,
            static_cast<std::uint32_t>(sizeof(vertices)));
        m_VertexArray->AddBuffer(*m_VertexBuffer, BufferLayout{ ShaderDataType::Float3 });
        m_IndexBuffer = std::make_unique<IndexBuffer>(indices, 3);

        constexpr std::string_view vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;

            void main()
            {
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";
        constexpr std::string_view fragmentSource = R"(
            #version 330 core
            layout(location = 0) out vec4 color;

            void main()
            {
                color = vec4(0.15, 0.65, 0.95, 1.0);
            }
        )";
        m_Shader = std::make_unique<Shader>(vertexSource, fragmentSource);
    }

    //Polls held movement input independently from routed key events.
    void OnUpdate(Timestep timestep) override
    {
        m_CameraController.OnUpdate(timestep);
        m_ObjectRotation += 45.0f * static_cast<float>(timestep.GetSeconds());
    }

    //Draws several transformed objects through Cubit's scene renderer.
    void OnRender() override
    {
        Renderer::BeginScene(m_CameraController.GetCamera());

        const glm::mat4 leftTransform =
            glm::translate(glm::mat4(1.0f), glm::vec3(-0.8f, 0.0f, 0.0f)) *
            glm::rotate(
                glm::mat4(1.0f),
                glm::radians(m_ObjectRotation),
                glm::vec3(0.0f, 0.0f, 1.0f));
        Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader, leftTransform);

        const glm::mat4 centerTransform =
            glm::scale(glm::mat4(1.0f), glm::vec3(0.65f, 0.65f, 1.0f));
        Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader, centerTransform);

        const glm::mat4 rightTransform =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.8f, 0.0f, 0.0f)) *
            glm::rotate(
                glm::mat4(1.0f),
                glm::radians(-m_ObjectRotation),
                glm::vec3(0.0f, 0.0f, 1.0f));
        Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader, rightTransform);

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
    OrthographicCameraController m_CameraController;
    float m_ObjectRotation = 0.0f;
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
