#include "Management.h"
#include "Configuration.h"
#include "Simulation.h"
#include "SimulationClock.h"
#include "WebServer.h"
#include "RunArchive.h"
#include "Version.h"
#include "Log.h"
#include "LogBase.h"
#include "Statistics.h"
#include "SymbolicInterface.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string> 
#include <thread>

namespace
{
  // State shared with the thread that waits for the Enter key. It is heap allocated and shared because that
  // thread stays blocked on stdin and can outlive main() when the simulation ends by itself.
  struct StopSignal
  {
    std::mutex mutex;
    std::condition_variable condition;
    bool stopRequested{ false };
  };

  void WaitForEnterKey(const std::shared_ptr<StopSignal> signal)
  {
    const auto key = std::cin.get();

    // End of input (non interactive run, e.g. stdin redirected): with the automatic stop enabled the
    // simulation just keeps going until it completes; without it nothing else could stop it.
    if (key == std::char_traits<char>::eof() && Configuration::Simulation::AutoStop())
      return;

    {
      std::lock_guard<std::mutex> lock(signal->mutex);
      signal->stopRequested = true;
    }

    signal->condition.notify_all();
  }

  // Waits until the user presses Enter or, with the automatic stop enabled, until every call has been generated
  // and served. Returns true if the simulation completed by itself.
  bool WaitForEndOfSimulation(const std::shared_ptr<StopSignal>& signal, const Simulation& simulation)
  {
    // If the generator is done but passengers stop making progress, the run is closed anyway:
    // the final report shows what is left (pending boarding / completion).
    constexpr long long NoProgressLimitMs = 5LL * 60LL * 1000LL;

    auto lastProgressCount = std::uint64_t{ 0U };
    auto lastProgressTimeMs = Configuration::Simulation::ElapsedMs();

    std::unique_lock<std::mutex> lock(signal->mutex);

    while (!signal->stopRequested)
    {
      signal->condition.wait_for(lock, std::chrono::milliseconds(50), [&signal]() { return signal->stopRequested; });

      if (!Configuration::Simulation::AutoStop() || !simulation.GeneratorFinished())
        continue;

      if (simulation.Finished())
        return true;

      const auto snapshot = Statistics::GetSnapshot();
      const auto progressCount = snapshot.boardedPassengers + snapshot.completedPassengers;
      const auto nowMs = Configuration::Simulation::ElapsedMs();

      if (progressCount != lastProgressCount)
      {
        lastProgressCount = progressCount;
        lastProgressTimeMs = nowMs;
      }
      else if (nowMs - lastProgressTimeMs > NoProgressLimitMs)
      {
        return true;
      }
    }

    return false;
  }

  struct CommandLine
  {
    bool web{ false };
    bool version{ false };
    std::string host{ "127.0.0.1" };
    int port{ 8080 };
    std::string webRoot{ "web" };
    std::string configPath{ "config.json" };
  };

  void PrintUsage()
  {
    std::cout <<
      "Elevator playground " ELEVATOR_VERSION "\n"
      "Usage: Elevator.exe [--web] [--port N] [--host ADDRESS] [--web-root DIR] [--config FILE] [--version]\n"
      "  (no option)     console simulation with the symbolic dashboard, as in the original project\n"
      "  --web           runs the simulation behind a web interface: open the printed address in a browser\n"
      "  --port N        web interface port (default 8080)\n"
      "  --host ADDRESS  address to listen on (default 127.0.0.1, use 0.0.0.0 to allow other computers)\n"
      "  --web-root DIR  folder with the web interface files (default ./web)\n"
      "  --config FILE   configuration file (default config.json)\n"
      "  --version       prints the version and exits\n";
  }

  bool ParseCommandLine(const int argc, char* argv[], CommandLine& commandLine)
  {
    for (int index = 1; index < argc; ++index)
    {
      const std::string argument = argv[index];
      const auto hasValue = index + 1 < argc;

      if (argument == "--web")
        commandLine.web = true;
      else if (argument == "--version")
        commandLine.version = true;
      else if (argument == "--port" && hasValue)
        commandLine.port = std::stoi(argv[++index]);
      else if (argument == "--host" && hasValue)
        commandLine.host = argv[++index];
      else if (argument == "--web-root" && hasValue)
        commandLine.webRoot = argv[++index];
      else if (argument == "--config" && hasValue)
        commandLine.configPath = argv[++index];
      else
        return false;
    }

    return true;
  }

  int RunConsole(Log& log)
  {
    log.Trace("Press Enter to stop...");

    Simulation simulation;
    simulation.Start();

    SymbolicInterface symbolicInterface(*simulation.GetManagement());
    // The dashboard is drawn on the console: do not draw it when logging only to file.
    if (Configuration::Log::WriteToScreen())
      symbolicInterface.Start();

    const auto stopSignal = std::make_shared<StopSignal>();
    std::thread(WaitForEnterKey, stopSignal).detach();

    const auto completed = WaitForEndOfSimulation(stopSignal, simulation);
    log.Trace(completed ? "Simulation completed: all calls served, shutting down..." : "Shutdown requested...");

    // A completed run is added to the archive of the results, the same one of the web interface.
    if (completed)
      simulation.CheckFinished();

    symbolicInterface.Stop();
    simulation.Stop();
    simulation.GetManagement()->TraceStatistics();
    return 0;
  }

  int RunWeb(const CommandLine& commandLine)
  {
    // The console belongs to the web server messages: the log goes to its file only, and only from Warning up,
    // because hours of simulated traffic at high speed would produce a very large file.
    Configuration::Log::Override(ILog::LogType::File, ILog::TraceLevel::Warning);

    Simulation simulation;
    simulation.Start();

    WebServer server(simulation, commandLine.webRoot);
    if (!server.Start(commandLine.host, commandLine.port))
    {
      std::cerr << "Cannot listen on " << commandLine.host << ":" << commandLine.port
                << " (port in use?). Choose another one with --port." << std::endl;
      simulation.Stop();
      return 1;
    }

    const auto shownHost = commandLine.host == "0.0.0.0" ? "localhost" : commandLine.host;
    std::cout << "Elevator playground " << ELEVATOR_VERSION << " running: open http://" << shownHost << ":" << commandLine.port << "/ in your browser" << std::endl;
    std::cout << "Press Enter to quit." << std::endl;

    // Without a console (stdin closed) the server keeps running until the process is terminated.
    if (std::cin.get() == std::char_traits<char>::eof())
      for (;;)
        std::this_thread::sleep_for(std::chrono::hours(1));

    server.Stop();
    simulation.Stop();
    return 0;
  }
}

int main(int argc, char* argv[])
{
  CommandLine commandLine;

  try
  {
    if (!ParseCommandLine(argc, argv, commandLine))
    {
      PrintUsage();
      return 2;
    }

    if (commandLine.version)
    {
      std::cout << "Elevator playground " << ELEVATOR_VERSION << std::endl;
      return 0;
    }

    Configuration::LoadFromFile(commandLine.configPath);
    RunArchive::Load(Configuration::Get().runsFilePath);
  }
  catch(std::exception& e)
  {
    std::cerr << "Invalid configuration: " << e.what() << std::endl;
    return 1;
  }

  int result = 0;

  try
  {
    if (commandLine.web)
    {
      result = RunWeb(commandLine);
    }
    else
    {
      Log log;
      result = RunConsole(log);
    }
  }
  catch(std::exception& e)
  {
    Log log;
    log.Trace(std::string("** CAUGHT EXCEPTION ** ") + e.what(), Log::TraceLevel::Error);
    result = 1;
  }

  // Make sure every pending message (final report, error messages) has been written before exiting.
  LogBase::FlushPendingMessages();
  return result;
}
