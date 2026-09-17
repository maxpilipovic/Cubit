#pragma once

#include "Cubit/Cubit.h"
#include "Cubit/Renderer/ScreenOverlay.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

//What the harness publishes for its readout.
//
//No player: the Sandbox exercises the engine, and a character belongs to a
//game. The position is the free camera's, and the counters are the renderer's.
struct HudState
{
    glm::vec3 CameraPosition{ 0.0f };

    //Whether the camera is inside a fluid block. Drives the underwater tint and
    //the fog.
    bool EyeInFluid = false;

    std::uint32_t MeshFaceCount = 0;
    std::size_t DrawnChunks = 0;
    std::size_t TotalChunks = 0;
    std::size_t PendingChunks = 0;

    //Fixed simulation steps run during the last frame. Above 1 the renderer is
    //behind the simulation; a value sustained at FrameClock::MaxTicksPerFrame
    //suggests the frame rate has fallen far enough that steps may be dropped.
    int StepsPerFrame = 0;

    //Operations that can still be undone: an edit or a whole blast.
    std::size_t UndoDepth = 0;
};

//Draws the harness's readout on top of the rendered scene.
//
//What to say, not how to say it: the pixel-space camera, the quad, the shader
//and the font atlas live in the engine's ScreenOverlay, because the game wants
//a readout too and neither app owns the drawing of one.
class HudLayer final : public Layer
{
public:
    HudLayer(std::shared_ptr<const HudState> state, std::uint32_t width, std::uint32_t height)
        : m_State(std::move(state)),
          m_Overlay(width, height)
    {
    }

    //Tracks a smoothed frame rate for the readout.
    void OnFrameUpdate(Timestep timestep) override
    {
        const float seconds = static_cast<float>(timestep.GetSeconds());
        if (seconds <= 0.0f)
            return;

        //Exponential smoothing, otherwise the number is unreadable.
        const float instant = 1.0f / seconds;
        m_SmoothedFps = m_SmoothedFps <= 0.0f
            ? instant
            : m_SmoothedFps * 0.9f + instant * 0.1f;
    }

    void OnRender(float alpha) override
    {
        (void)alpha;
        m_Overlay.Begin();

        if (m_State->EyeInFluid)
            m_Overlay.FillScreen(UnderwaterTint);

        m_Overlay.DrawCrosshair();
        DrawReadout();

        m_Overlay.End();
    }

    //Keeps the overlay projection matched to the framebuffer, which is what the
    //viewport is sized in. It can differ from window coordinates on displays
    //with scaling.
    void OnEvent(Event& event) override
    {
        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<FramebufferResizeEvent>(
            [this](FramebufferResizeEvent& resizeEvent)
            {
                m_Overlay.Resize(resizeEvent.GetWidth(), resizeEvent.GetHeight());
                return false;
            });
    }

    //Every label this readout draws. DebugFontTests checks the list against the
    //font: an unsupported character draws as a blank rather than failing, so a
    //label that drifts out of the font silently hides the value beside it.
    static constexpr std::string_view Labels[] = {
        "POS", "OCEAN", "FACES", "DRAWN", "PENDING", "STEPS", "UNDO", "FPS"
    };

private:
    //Covers the whole screen while submerged. The fog cannot reach the sky, so
    //without this, looking up from underwater shows an untouched clear colour.
    static inline const glm::vec4 UnderwaterTint{ 0.15f, 0.40f, 0.70f, 0.45f };

    //Draws the debug lines down from the top-left corner.
    void DrawReadout() const
    {
        const float lineHeight = ScreenOverlay::LineHeight();
        const float margin = ScreenOverlay::Margin;
        float y = m_Overlay.TopLine();

        const glm::vec3& position = m_State->CameraPosition;
        m_Overlay.DrawText(
            "POS " + ScreenOverlay::FormatOneDecimal(position.x) +
            " " + ScreenOverlay::FormatOneDecimal(position.y) +
            " " + ScreenOverlay::FormatOneDecimal(position.z),
            margin,
            y);

        y -= lineHeight;
        m_Overlay.DrawText(std::string("OCEAN ") + (m_State->EyeInFluid ? "1" : "0"),
            margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("FACES " + std::to_string(m_State->MeshFaceCount), margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("DRAWN " + std::to_string(m_State->DrawnChunks) +
            "/" + std::to_string(m_State->TotalChunks), margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("PENDING " + std::to_string(m_State->PendingChunks), margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("STEPS " + std::to_string(m_State->StepsPerFrame), margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("UNDO " + std::to_string(m_State->UndoDepth), margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("FPS " + std::to_string(static_cast<int>(m_SmoothedFps + 0.5f)),
            margin, y);
    }

    std::shared_ptr<const HudState> m_State;
    ScreenOverlay m_Overlay;
    float m_SmoothedFps = 0.0f;
};
