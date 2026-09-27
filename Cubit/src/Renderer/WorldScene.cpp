#include "cub.h"

#include "Cubit/Renderer/WorldScene.h"

#include "Cubit/Renderer/Mesh.h"
#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Voxel/ChunkMesher.h"

#include "Core/CoreLogger.h"

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
        uniform float u_LightFloor;

        // 0 for chunks, whose vertex colour is already the finished shade
        // with alpha as opacity. 1 for a ModelMesher model, whose rgb is the
        // palette colour and whose alpha is raw face shade times AO: the
        // floor goes on after the light is multiplied in, once, which is
        // what ChunkMesher does for a chunk vertex at mesh time.
        uniform float u_ModelLighting;

        void main()
        {
            // Exponential, so it needs no far-plane constant and never
            // saturates abruptly. Density is zero when dry, which makes
            // this a mix against nothing rather than a branch.
            float d = length(v_WorldPos - u_CameraPos);
            float f = 1.0 - exp(-u_FogDensity * d);

            float modelShade = u_LightFloor
                + (1.0 - u_LightFloor) * v_Color.a * u_Brightness;
            vec3 lit = mix(v_Color.rgb * u_Brightness, v_Color.rgb * modelShade, u_ModelLighting);
            float alpha = mix(v_Color.a, 1.0, u_ModelLighting);

            // Lighting applies before the fog: fog is the colour of the air
            // between camera and surface, so a dark model fades to the same
            // fog colour a bright one does.
            color = vec4(mix(lit, u_FogColor, f), alpha);
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
    m_HasRendered = true;

    //u_Transform already carries worldOffset and the camera position is in that
    //same space - the invariant the transparency sort also relies on - so the
    //two can be subtracted directly in the shader.
    m_Shader->SetFloat3("u_FogColor", fogColor);
    m_Shader->SetFloat3("u_CameraPos", camera.GetPosition());
    m_Shader->SetFloat("u_FogDensity", fogDensity);
    m_Shader->SetFloat("u_Brightness", 1.0f);
    m_Shader->SetFloat("u_LightFloor", ChunkMesher::LightFloor);
    m_Shader->SetFloat("u_ModelLighting", 0.0f);

    m_Renderer.Render(
        *m_Shader,
        camera.GetViewProjectionMatrix(),
        worldOffset,
        camera.GetPosition());

    Renderer::EndScene();
}

void WorldScene::DrawMesh(const Mesh& mesh, const glm::mat4& transform, float brightness)
{
    //Renderer::Submit reads a view-projection that only BeginScene sets, and
    //EndScene never clears it, so calling this before Render has ever run
    //would not fail - it would draw with whatever matrix (or none) happens
    //to be sitting in the renderer's static state. That is a silently wrong
    //frame, which is worse than a crash, so catch it here instead.
    CB_CORE_ASSERT(m_HasRendered, "DrawMesh called before WorldScene::Render set a camera");

    if (mesh.Empty())
        return;

    m_Shader->Bind();
    m_Shader->SetFloat("u_Brightness", brightness);
    m_Shader->SetFloat("u_ModelLighting", 1.0f);
    Renderer::Submit(mesh.Array(), mesh.Indices(), *m_Shader, transform);

    //Put back, so nothing drawn with this shader after a model - a later
    //chunk pass in the same frame - is lit as one.
    m_Shader->SetFloat("u_Brightness", 1.0f);
    m_Shader->SetFloat("u_ModelLighting", 0.0f);
}
