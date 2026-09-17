#include "Cubit/CrashHandler.h"
#include "Cubit/FrameClock.h"
#include "Cubit/Logger.h"
#include "Cubit/Net/EnetTransport.h"
#include "Cubit/Net/MapHash.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <glm/glm.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#endif

namespace
{
    constexpr std::uint16_t DefaultPort = 27015;
    constexpr const char* DefaultMap = "assets/maps/battlefield512.vox";

    //Matches the Sandbox's own hint, so a player spawns where they would in
    //single-player rather than somewhere unrelated.
    const glm::ivec2 SpawnHint{ 240, 300 };

    //Set from a console handler on another thread; read by the loop every tick.
    std::atomic<bool> StopRequested{ false };

    //Set once the server and its transports have been destroyed - which is
    //what tells every client the session is over - and the last line is logged.
    std::atomic<bool> Stopped{ false };

#ifdef _WIN32
    //Ctrl+C, Ctrl+Break, and closing the console window.
    //
    //For a closed window Windows ends the process the moment this returns, and
    //gives it only a few seconds regardless. Waiting here for Stopped is what
    //lets the loop finish, disconnect its clients and log, instead of the
    //process vanishing mid-tick the way a kill does.
    BOOL WINAPI OnConsoleEvent(DWORD event)
    {
        switch (event)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            StopRequested = true;
            for (int i = 0; i < 400 && !Stopped; ++i)
                Sleep(10);
            return TRUE;

        default:
            return FALSE;
        }
    }
#else
    void OnSignal(int)
    {
        StopRequested = true;
    }
#endif
}

//The authoritative server. No window, no Application, no Renderer, no GL
//context - the MapGen precedent. The simulation core is already GL-free, which
//is what makes this eighty lines rather than an engine refactor.
int main(int argc, char** argv)
{
    CrashHandler::Install("Server");
    Logger::OpenFile("Server");

    std::string mapPath = DefaultMap;
    std::uint16_t port = DefaultPort;

    //Round-trip milliseconds, halved into the one-way latency NetworkSim wants.
    //Present on the server as well as the client so a demo can put the delay on
    //either side.
    double latencyRtt = 0.0;
    float loss = 0.0f;

    //Seconds to run before stopping exactly as a Ctrl+C would. Zero runs until
    //asked. For scripted runs: a script cannot send Ctrl+C to this console, and
    //killing the process skips the disconnect and the final log line.
    double durationSeconds = 0.0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--duration" && i + 1 < argc)
            durationSeconds = std::atof(argv[++i]);
        else if (arg == "--port" && i + 1 < argc)
            port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--latency" && i + 1 < argc)
            latencyRtt = std::atof(argv[++i]);
        else if (arg == "--loss" && i + 1 < argc)
            loss = static_cast<float>(std::atof(argv[++i])) / 100.0f;
        else
            mapPath = arg;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
#else
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    try
    {
        CB_INFO("Loading " + mapPath);

        World world = BuildWorld(VoxLoader::LoadFile(mapPath));

        //Before anything reads light. The server never meshes, but a client
        //reloading from a snapshot would otherwise disagree about lighting.
        SkyLight::PropagateAll(world);

        const std::uint64_t mapHash = HashMapFile(mapPath);
        if (mapHash == 0)
        {
            CB_ERROR("Could not hash the map file");
            return 1;
        }

        //Taken from CharacterConfig rather than written out here. The box a
        //player occupies is one fact, and a second copy of it on the server is
        //exactly how the server comes to spawn people somewhere the client
        //thinks is solid.
        const glm::vec3 halfExtents = CharacterConfig{}.HalfExtents;

        const std::optional<glm::vec3> spawn =
            FindSpawn(world, SpawnHint, halfExtents);

        if (!spawn.has_value())
        {
            CB_ERROR("No usable spawn near the hint");
            return 1;
        }

        std::unique_ptr<EnetTransport> socket = EnetTransport::Listen(port, 16);
        if (socket == nullptr)
            return 1;

        //The same decorator the tests use. Wrapping the real socket is what
        //lets a localhost demo show latency at all - loopback has none of its
        //own, so without this the round trip would be invisible and the stage
        //would prove nothing.
        NetworkSim sim;
        sim.Latency = latencyRtt / 2000.0;
        sim.Loss = loss;

        SimulatedTransport simulated(*socket, sim);
        Transport& transport = latencyRtt > 0.0 || loss > 0.0f
            ? static_cast<Transport&>(simulated)
            : static_cast<Transport&>(*socket);

        //The map name, not the path: the client resolves it against its own
        //assets directory, and the 23.8 MB of data is never sent.
        const std::string mapName =
            mapPath.substr(mapPath.find_last_of("/\\") + 1);

        MatchServer server(std::move(world), mapName, mapHash, *spawn, transport);

        CB_INFO("Listening on port " + std::to_string(port));

        //A fixed-step loop with no rendering. FrameClock turns a wall-clock
        //delta into whole simulation steps, exactly as it does in the Sandbox;
        //Alpha() is meaningless here because nothing interpolates.
        FrameClock clock;
        const auto started = std::chrono::steady_clock::now();
        auto previous = started;

        while (!StopRequested)
        {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed =
                std::chrono::duration<double>(now - previous).count();
            previous = now;

            const int steps = clock.Advance(elapsed);
            for (int i = 0; i < steps; ++i)
                server.Step(FrameClock::FixedStepSeconds);

            //After the steps, which are what read the inputs clients sent during
            //a stall. Ticks FrameClock would not repay each had an input waiting;
            //left queued, those inputs make every later one late for the rest of
            //the match.
            server.SkipTicks(clock.DiscardedTicks());

            if (durationSeconds > 0.0
                && std::chrono::duration<double>(now - started).count() >= durationSeconds)
                StopRequested = true;

            //Without this the loop spins a core flat out to do nothing. One
            //millisecond is far below the 16.7 ms step, so it costs no
            //simulation accuracy.
            if (steps == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        //Read before the server goes: leaving this scope destroys the server
        //and then the transports, and EnetTransport's destructor is what
        //disconnects every client.
        CB_INFO("Stopping after " + std::to_string(server.Match().Tick()) + " ticks: "
            + std::to_string(server.ClientCount()) + " clients connected, "
            + std::to_string(server.AcceptedEditCount()) + " edits accepted");
    }
    catch (const std::exception& error)
    {
        CB_CRITICAL(std::string("Server failed: ") + error.what());
        Stopped = true;
        return 1;
    }

    CB_INFO("Server stopped");
    Stopped = true;
    return 0;
}
