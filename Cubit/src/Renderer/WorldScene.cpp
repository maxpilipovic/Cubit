#include "cub.h"

#include "Cubit/Renderer/WorldScene.h"

#include "Cubit/Renderer/Mesh.h"
#include "Cubit/Renderer/Renderer.h"

#include <string_view>

WorldScene::WorldScene()
{
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
        uniform float u_Brightness;

        void main()
        {
            // Exponential, so it needs no far-plane constant and never
            // saturates abruptly. Density is zero when dry, which makes
            // this a mix against nothing rather than a branch.
            float d = length(v_WorldPos - u_CameraPos);
            float f = 1.0 - exp(-u_FogDensity * d);
            // Brightness applies before the fog: fog is the colour of the
            // air between camera and surface, so a dark model fades to the
            // same fog colour a bright one does.
            color = vec4(mix(v_Color.rgb * u_Brightness, u_FogColor, f), v_Color.a);
        }
    )";

    m_Shader = std::make_unique<Shader>(vertexSource, fragmentSource);
}

WorldScene::~WorldScene() = default;

void WorldScene::Update(World& world)
{
    m_Renderer.Update(world);
}

void WorldScene::Render(const PerspectiveCamera& camera, const glm::vec3& worldOffset,
    const glm::vec3& fogColor, float fogDensity)
{
    Renderer::BeginScene(camera);

    //u_Transform already carries worldOffset and the camera position is in that
    //same space - the invariant the transparency sort also relies on - so the
    //two can be subtracted directly in the shader.
    m_Shader->SetFloat3("u_FogColor", fogColor);
    m_Shader->SetFloat3("u_CameraPos", camera.GetPosition());
    m_Shader->SetFloat("u_FogDensity", fogDensity);
    m_Shader->SetFloat("u_Brightness", 1.0f);

    m_Renderer.Render(
        *m_Shader,
        camera.GetViewProjectionMatrix(),
        worldOffset,
        camera.GetPosition());

    Renderer::EndScene();
}

void WorldScene::DrawMesh(const Mesh& mesh, const glm::mat4& transform, float brightness)
{
    if (mesh.Empty())
        return;

    m_Shader->Bind();
    m_Shader->SetFloat("u_Brightness", brightness);
    Renderer::Submit(mesh.Array(), mesh.Indices(), *m_Shader, transform);
}
