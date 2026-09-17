#pragma once

#include "Cubit/Cubit.h"
#include "Cubit/Renderer/ScreenOverlay.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

//Values the sandbox publishes for the overlay to display.
struct GameHudState
{
    glm::vec3 PlayerPosition{ 0.0f };
    bool Grounded = false;

    //Whether the camera is inside a fluid block. Drives the underwater tint and
    //the fog; separate from BodyInFluid because a wading player has their head
    //in open air while their legs are in the river.
    bool EyeInFluid = false;

    //Whether the player's box overlaps any fluid block. Drives the swim rules.
    bool BodyInFluid = false;

    std::uint32_t MeshFaceCount = 0;
    std::size_t DrawnChunks = 0;
    std::size_t TotalChunks = 0;
    std::size_t PendingChunks = 0;

    //Fixed simulation steps run during the last frame. Above 1 the renderer is
    //behind the simulation; a value sustained at FrameClock::MaxTicksPerFrame
    //suggests the frame rate has fallen far enough that steps may be being
    //dropped.
    int StepsPerFrame = 0;

    //Networking. All zero and false in single-player, and the overlay draws
    //none of these lines when Connected, Rejected and Disconnected are all
    //false - so the single-player readout is byte-for-byte what it was before
    //the wire existed, which the acceptance check depends on.
    bool Connected = false;
    bool Rejected = false;
    bool Disconnected = false;
    double RoundTripMs = 0.0;
    std::size_t PlayersInMatch = 0;

    //The shot. Connected only, like the lines above. Health is this player's
    //own, as the last snapshot reported it. ShotLabel is HIT or KILLED for a
    //short while after the server rules that one of this player's shots
    //connected, and empty otherwise - it only ever follows the server's word.
    std::uint8_t Health = 0;
    std::string ShotLabel;
};

//Draws the game's readout on top of the rendered scene.
//
//What to say, not how to say it: the pixel-space camera, the quad, the shader
//and the font atlas all live in the engine's ScreenOverlay, because two apps
//want a readout and neither owns the drawing of one.
class GameHudLayer final : public Layer
{
public:
    GameHudLayer(std::shared_ptr<const GameHudState> state, std::uint32_t width, std::uint32_t height)
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

        const glm::vec3& position = m_State->PlayerPosition;
        m_Overlay.DrawText(
            "POS " + ScreenOverlay::FormatOneDecimal(position.x) +
            " " + ScreenOverlay::FormatOneDecimal(position.y) +
            " " + ScreenOverlay::FormatOneDecimal(position.z),
            margin,
            y);

        y -= lineHeight;
        m_Overlay.DrawText(std::string("GND ") + (m_State->Grounded ? "1" : "0"), margin, y);

        y -= lineHeight;
        // The flags are digits. Every label on this readout has to be spelled
        // from DebugFont::Order: an unsupported character still renders as a
        // blank rather than failing, which would silently hide a set flag.
        // DebugFontTests checks the HUD's own words against the font.
        m_Overlay.DrawText(
            std::string("OCEAN ") +
            (m_State->EyeInFluid ? "1" : "0") +
            (m_State->BodyInFluid ? "1" : "0"),
            margin,
            y);

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
        m_Overlay.DrawText("FPS " + std::to_string(static_cast<int>(m_SmoothedFps + 0.5f)),
            margin, y);

        //Single-player draws nothing below this point, which is what keeps the
        //readout identical to the pre-networking one.
        if (!m_State->Connected && !m_State->Rejected && !m_State->Disconnected)
            return;

        //A refused handshake has to be visible on screen, not only in the log:
        //the log scrolls past behind a fullscreen window, and "nothing is
        //happening" is exactly what a silent rejection looks like.
        //
        //Every label here must be spelled from DebugFont::Order. An unsupported
        //character renders as a blank rather than failing, so a wrong label
        //would silently show as a gap - which is what kept these to CONNECTED
        //and NET while the font lacked most of the alphabet. DebugFontTests now
        //checks the words this readout draws.
        y -= lineHeight;

        if (m_State->Rejected)
        {
            m_Overlay.DrawText("NOT CONNECTED", margin, y);
            return;
        }

        //The server stopped or the connection was lost. The world stays on
        //screen as it last was, so without this a dead session looks exactly
        //like a quiet one.
        if (m_State->Disconnected)
        {
            m_Overlay.DrawText("DISCONNECTED", margin, y);
            return;
        }

        m_Overlay.DrawText("CONNECTED " + std::to_string(m_State->PlayersInMatch), margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("NET " + std::to_string(static_cast<int>(m_State->RoundTripMs + 0.5)),
            margin, y);

        y -= lineHeight;
        m_Overlay.DrawText("HEALTH " + std::to_string(m_State->Health), margin, y);

        if (!m_State->ShotLabel.empty())
        {
            y -= lineHeight;
            m_Overlay.DrawText(m_State->ShotLabel, margin, y);
        }
    }

    std::shared_ptr<const GameHudState> m_State;
    ScreenOverlay m_Overlay;
    float m_SmoothedFps = 0.0f;
};
