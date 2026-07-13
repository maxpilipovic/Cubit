#include "Cubit/Cubit.h"

#include <string>

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
        : m_PlayerDiedSubscription(
            eventBus.Subscribe<PlayerDiedEvent>(
                //Logs each immediate or deferred player-death notification.
                [](const PlayerDiedEvent& event)
                {
                    CB_INFO(
                        std::string("Player ") + std::to_string(event.Player) +
                        " was defeated by player " + std::to_string(event.Killer));
                }))
    {
    }

    //Polls held movement input independently from routed key events.
    void OnUpdate(Timestep timestep) override
    {
        (void)timestep;
        const bool movingForward = Input::IsKeyPressed(KeyCode::W);
        (void)movingForward;
    }

    //Routes one-time key presses through the typed platform dispatcher.
    void OnEvent(Event& event) override
    {
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

    EventSubscription m_PlayerDiedSubscription;
};

class SandboxApplication final : public Application
{
public:
    //Creates the Sandbox layer and demonstrates immediate and deferred gameplay events.
    SandboxApplication()
    {
        PushLayer(std::make_unique<SandboxLayer>(GetEventBus()));
        GetEventBus().Publish(PlayerDiedEvent{ 1, 2 });
        GetEventBus().Enqueue(PlayerDiedEvent{ 3, 4 });
    }
};

//Starts the Sandbox application and runs the engine loop.
int main()
{
    SandboxApplication app;
    app.Run();

    return 0;
}
