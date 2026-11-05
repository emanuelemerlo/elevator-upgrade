/**********************************************************************************
*        File: Configuration.h
* Description: Runtime simulation parameters.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include "ILog.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <limits>
#include <string>

/**
 * \brief Simulation parameters.
 */
namespace Configuration
{
  namespace CallsGenerator
  {
    /**
     * \brief Types of calls generators.
     */
    enum class Type { Fixed, Random };

    /**
     * \brief Who travels when during the simulated day (see TrafficProfile).
     *        Uniform: random origin and destination at any hour (the original model).
     *        Office: morning arrivals from the lobby, lunch in both directions, evening departures to the lobby.
     *        Residential: the opposite of Office (people leave home in the morning and come back in the evening).
     */
    enum class Pattern { Uniform, Office, Residential };
  }

  namespace Dispatch
  {
    /**
     * \brief How a call is given to a cabin.
     *        Optimized: the route of every cabin is simulated and the call goes where it costs the least passenger time.
     *        Nearest: the closest cabin in service with room for the passenger, whatever it is doing.
     *        Collective: the closest cabin among the idle ones and those already heading towards the call in its
     *                    direction (the classic directional collective control); the closest one otherwise.
     */
    enum class Strategy { Optimized, Nearest, Collective };

    /**
     * \brief Where an idle cabin waits for the next call.
     *        Stay: where it stopped. Lobby: it goes back to floor 0. Spread: every cabin has a home floor, evenly
     *        spaced along the building.
     */
    enum class Parking { Stay, Lobby, Spread };
  }

  struct Settings
  {
    unsigned int numberOfElevators{ 3 };
    unsigned int numberOfFloors{ 5 };

    CallsGenerator::Type generatorType{ CallsGenerator::Type::Random };
    unsigned int numberOfCalls{ std::numeric_limits<unsigned int>::max() }; // Random generator: stop after this many calls
    unsigned int averageCallsPerDay{ 600 };
    unsigned int maxConcurrentCalls{ 600 };
    unsigned int numberOfSimulationDays{ 1 };
    CallsGenerator::Pattern trafficPattern{ CallsGenerator::Pattern::Office };
    Dispatch::Strategy dispatchStrategy{ Dispatch::Strategy::Optimized };
    Dispatch::Parking idleParking{ Dispatch::Parking::Stay };

    // Zoning of tall buildings (see Zoning.h): empty = one group of numberOfElevators cabins serving every floor.
    std::string zones;        // e.g. "1-20:6, 21-40:6": floors of each zone and its cabins
    std::string skyLobbies;   // e.g. "30:4": intermediate lobby and cabins of its shuttle from the ground floor
    // Time to pass a floor the cabin does not serve (express run), as a share of timeToReachTheNextFloor.
    double expressFloorFactor{ 0.35 };
    // Average patience of a passenger, in simulated seconds: who has waited longer gives up and takes the stairs.
    // Every passenger of the scenario gets a patience between half and one and a half times this value. 0 = never.
    double stairsPatienceSeconds{ 120.0 };
    // A passenger who waited on the floors at most this long in all (simulated seconds) arrives happy: the
    // counterpart of the passengers who take the stairs. Only an evaluation, it does not change the scenario.
    double happyWaitSeconds{ 20.0 };

    // Energy model (see Energy.h): standby power of every cabin and efficiency of the motor.
    double standbyPowerW{ 200.0 };
    double motorEfficiency{ 0.75 };
    // Delays between two generated calls are drawn from the daily profile, then kept in this range (simulated ms).
    std::chrono::milliseconds minDelayBetweenCalls{ std::chrono::milliseconds(500) };
    std::chrono::milliseconds maxDelayBetweenCalls{ std::chrono::minutes(15) };

    // Durations of the cabins, in simulated milliseconds (the same time line as the 24 hours of the day).
    std::chrono::milliseconds timeToReachTheNextFloor{ std::chrono::seconds(2) };
    std::chrono::milliseconds enterAndExitTime{ std::chrono::seconds(3) };
    unsigned int maxPeoplePerElevator{ 12 };
    // Time needed by the doors to open or to close (each transition), in simulated milliseconds.
    std::chrono::milliseconds doorTransitionTime{ std::chrono::milliseconds(1500) };

    // How long a whole simulated day (24 hours) lasts on the wall clock, in seconds: it sets the initial speed of
    // the simulated clock (1440 s = 24 minutes = 60x). The speed can be changed at run time from the web interface.
    double dayDurationSeconds{ 1440.0 };
    // Time of day at which the simulation starts on day 1 (e.g. 6.5 = 06:30), so that a run does not begin with a night.
    double dayStartHour{ 6.0 };
    // Seed of the random calls generator: 0 = pick a different seed at every run.
    unsigned int randomSeed{ 0 };
    // Stop the simulation (and print the final report) as soon as all calls have been generated and served.
    bool autoStop{ true };

    ILog::TraceLevel traceLevel{ ILog::TraceLevel::Verbose };
    ILog::LogType defaultLogType{ ILog::LogType::Screen };
    std::string logFilePath{ "elevator.log" };
    // Results of the completed runs (one JSON object per line), read again at the next start of the web interface.
    std::string runsFilePath{ "runs.jsonl" };
  };

  bool LoadFromFile(const std::string& path);
  // Reads the settings found in a JSON text over 'settings' (missing keys keep their value) and validates them.
  void ParseJson(const std::string& json, Settings& settings);
  // Throws std::invalid_argument with an explanatory message if a value is not acceptable.
  void Validate(const Settings& settings);
  // Replaces the current settings. Only allowed while the simulation is not running.
  void Apply(const Settings& settings);
  const Settings& Get();

  namespace Building
  {
    unsigned int NumberOfElevators();
    unsigned int NumberOfFloors();
  }

  namespace Elevator
  {
    std::chrono::milliseconds TimeToReachTheNextFloor();
    std::chrono::milliseconds TimeToPassAnExpressFloor();
    std::chrono::milliseconds EnterAndExitTime();
    std::chrono::milliseconds DoorTransitionTime();
    unsigned int MaxPeople();
  }

  namespace CallsGenerator
  {
    Type GeneratorType();
    unsigned int NumberOfCalls();
    unsigned int AverageCallsPerDay();
    unsigned int MaxConcurrentCalls();
    unsigned int NumberOfSimulationDays();
    Pattern TrafficPattern();
    long long MinDelayBetweenCalls();
    long long MaxDelayBetweenCalls();
  }

  namespace Dispatch
  {
    Strategy DispatchStrategy();
    Parking IdleParking();
    const char* StrategyName(Strategy strategy);
    const char* ParkingName(Parking parking);
  }

  const char* PatternName(CallsGenerator::Pattern pattern);

  namespace Simulation
  {
    /** \brief Restarts the simulated clock at the configured start hour and speed. */
    void StartClock();

    /** \brief Initial speed of the simulated clock: simulated milliseconds per real millisecond. */
    double InitialSpeed();
    double DayStartHour();
    unsigned int RandomSeed();
    bool AutoStop();

    /** \brief Sleeps for a simulated duration. \return false if the simulation has been aborted. */
    bool SleepFor(std::chrono::milliseconds simulatedDuration);
    /** \brief Simulated time elapsed since StartClock(), in milliseconds. */
    long long ElapsedMs();

    /** \brief Hour of the simulated day, 0.0 - 24.0. */
    double CurrentHour();
    unsigned int CurrentDay();
    /** \brief True when the last configured day is over (midnight). */
    bool Completed();
    std::string CurrentDayTimeLabel();
  }

  namespace Log
  {
    ILog::TraceLevel TraceLevel();
    ILog::LogType DefaultLogType();
    std::string FilePath();
    bool WriteToScreen();
    bool WriteToFile();

    /** \brief Changes where the log is written (e.g. the web mode never draws the console dashboard). */
    void Override(ILog::LogType logType, ILog::TraceLevel minimumLevel);
  }
}
