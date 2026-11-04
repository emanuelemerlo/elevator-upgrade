/**********************************************************************************
* Tests of the simulated clock and of whole simulation runs (cabins, dispatcher, generator,
* out of order cabins). Runs use a high clock speed, so they last a few seconds.
* No framework: every check prints a line and the process exits with a failure code
* if at least one of them fails. Run with: make test
**********************************************************************************/

#include "Call.h"
#include "Configuration.h"
#include "EventBus.h"
#include "Floors.h"
#include "LogBase.h"
#include "Management.h"
#include "People.h"
#include "Simulation.h"
#include "SimulationClock.h"
#include "Statistics.h"
#include "RunArchive.h"
#include "Scenario.h"
#include "BuildingContext.h"
#include <algorithm>

#include <chrono>
#include <functional>
#include <iostream>
#include <set>
#include <thread>

namespace
{
  int g_failures = 0;
  int g_checks = 0;

  void Check(const bool condition, const std::string& description)
  {
    ++g_checks;
    if (!condition)
      ++g_failures;

    std::cout << (condition ? "  [ok]   " : "  [FAIL] ") << description << std::endl;
  }

  typedef std::chrono::steady_clock RealClock;

  double RealMsSince(const RealClock::time_point start)
  {
    return std::chrono::duration<double, std::milli>(RealClock::now() - start).count();
  }

  // Polls a condition (real time) until it is true or the timeout expires.
  bool WaitFor(const std::function<bool()>& condition, const std::chrono::milliseconds timeout)
  {
    const auto start = RealClock::now();
    while (!condition())
    {
      if (RealClock::now() - start > timeout)
        return false;

      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return true;
  }

  Configuration::Settings BaseSettings()
  {
    Configuration::Settings settings;
    settings.numberOfElevators = 3;
    settings.numberOfFloors = 10;
    settings.generatorType = Configuration::CallsGenerator::Type::Random;
    settings.trafficPattern = Configuration::CallsGenerator::Pattern::Office;
    settings.averageCallsPerDay = 2000;
    settings.numberOfSimulationDays = 1;
    settings.dayDurationSeconds = static_cast<double>(SimulationClock::DayMs) / 1000.0 / SimulationClock::MaxSpeed;
    settings.randomSeed = 11;
    settings.defaultLogType = ILog::LogType::File;
    settings.traceLevel = ILog::TraceLevel::Warning;
    settings.logFilePath = "simulation_tests.log";
    return settings;
  }

  void Clock()
  {
    std::cout << "Simulated clock" << std::endl;

    SimulationClock::Reset(1000.0, 6 * SimulationClock::HourMs);
    Check(SimulationClock::StartTimeOfDayMs() == 6 * SimulationClock::HourMs, "the day starts at the configured hour");

    auto start = RealClock::now();
    Check(SimulationClock::SleepFor(std::chrono::milliseconds(2000)), "a sleep completes");
    const auto elapsed = RealMsSince(start);
    Check(elapsed >= 1.5 && elapsed < 200.0, "2 simulated seconds at 1000x last about 2 real ms");
    Check(SimulationClock::Now() >= 2000, "the simulated time has advanced by the sleep");

    SimulationClock::Pause();
    const auto pausedAt = SimulationClock::Now();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(SimulationClock::Now() == pausedAt && SimulationClock::Paused(), "a paused clock does not advance");

    SimulationClock::Step(5000);
    Check(WaitFor([]() { return SimulationClock::Paused(); }, std::chrono::milliseconds(500)), "a step pauses the clock again by itself");
    Check(SimulationClock::Now() == pausedAt + 5000, "a step advances exactly by the requested time");

    // A sleeping thread follows the speed changes.
    SimulationClock::Resume();
    SimulationClock::SetSpeed(1.0);
    start = RealClock::now();
    std::thread sleeper([]() { SimulationClock::SleepFor(std::chrono::milliseconds(60000)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    SimulationClock::SetSpeed(SimulationClock::MaxSpeed);
    sleeper.join();
    Check(RealMsSince(start) < 1000.0, "a speed change shortens a sleep already in progress");

    // Chained sleeps do not accumulate the delays of the operating system timers.
    SimulationClock::Reset(SimulationClock::MaxSpeed, 0);
    const auto chainStart = SimulationClock::Now();
    for (auto index = 0; index < 400; ++index)
      SimulationClock::SleepFor(std::chrono::milliseconds(500));
    const auto chained = SimulationClock::Now() - chainStart;
    Check(chained >= 200000 && chained < 230000, "400 x 0.5 s at 2000x stay on the simulated time line (drift < 15%)");

    // An abort wakes everybody, also a thread sleeping on a paused clock.
    SimulationClock::Pause();
    std::atomic_bool result{ true };
    std::thread pausedSleeper([&result]() { result = SimulationClock::SleepFor(std::chrono::milliseconds(1000)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    SimulationClock::Abort();
    pausedSleeper.join();
    Check(!result && !SimulationClock::SleepFor(std::chrono::milliseconds(1)), "after an abort every sleep returns false at once");
  }

  void WholeRun()
  {
    std::cout << "Whole run (2 simulated hours at 2000x)" << std::endl;

    auto settings = BaseSettings();
    settings.dayStartHour = 22.0;   // the run ends at midnight: 2 hours of traffic
    settings.averageCallsPerDay = 6000;
    Configuration::Apply(settings);

    Simulation simulation;
    simulation.Start();

    const auto finished = WaitFor([&simulation]() { return simulation.Finished(); }, std::chrono::seconds(30));
    const auto statistics = Statistics::GetSnapshot();
    simulation.Stop();

    Check(finished, "the run ends by itself after midnight");
    Check(statistics.queuedCalls > 50U, "the generator produced calls (" + std::to_string(statistics.queuedCalls) + ")");
    Check(statistics.completedPassengers + statistics.abandonedCalls == statistics.queuedCalls,
      "every passenger reached the destination, by lift or by the stairs");
    Check(statistics.requeuedCalls == 0U, "no boarding refused by a full cabin");
    Check(Configuration::Simulation::Completed() || Configuration::Simulation::CurrentHour() >= 23.0,
      "the run lasts until the last passengers of the day, close to midnight");
  }

  void OutOfOrder()
  {
    std::cout << "Out of order cabin with a passenger inside" << std::endl;

    auto settings = BaseSettings();
    settings.numberOfElevators = 2;
    settings.numberOfFloors = 20;
    settings.numberOfCalls = 1;           // the generator adds at most one random call
    settings.averageCallsPerDay = 1;
    settings.maxDelayBetweenCalls = std::chrono::hours(20);
    settings.dayDurationSeconds = 864.0;  // 100x: slow enough to catch the cabin between two floors
    Configuration::Apply(settings);

    Simulation simulation;
    simulation.Start();

    // Only A in service: the passenger can only board A.
    simulation.SetOutOfOrder("B", true);
    const auto call = simulation.AddCall(0, 19);
    Check(call != nullptr, "a manual call is accepted");

    Check(WaitFor([&call]() { return call->HasBoarded(); }, std::chrono::seconds(10)), "the passenger boards A");
    Check(WaitFor([&simulation]()
      {
        auto lock = simulation.Lock();
        for (const auto& elevator : simulation.GetManagement()->GetElevatorSnapshots())
          if (elevator.id == "A")
            return elevator.status == ElevatorStatus::Moving && elevator.currentFloor >= 2U;
        return false;
      }, std::chrono::seconds(10)), "A is travelling up");

    // A breaks down while moving: it stops at the next floor and the passenger gets out there.
    simulation.SetOutOfOrder("A", true);
    Check(WaitFor([&call]() { return call->GetEvacuations() == 1U; }, std::chrono::seconds(5)), "the passenger is evacuated");
    const auto evacuationFloor = call->GetStartFloor();
    Check(evacuationFloor > 0U && evacuationFloor < 19U, "the cabin stopped at an intermediate floor (" + std::to_string(evacuationFloor) + ")");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Check(!call->HasBoarded() || call->GetAssignedElevator() == "?", "with every cabin out of order nobody serves the passenger");

    // B comes back into service and completes the trip.
    simulation.SetOutOfOrder("B", false);
    Check(WaitFor([&call]() { return call->GetAssignedElevator() == "B"; }, std::chrono::seconds(5)), "the pending passenger is assigned to B");
    const auto waiting = [&call]()
    {
      for (const auto& person : Floors::GetPeople().Snapshot())
        if (person == call)
          return true;
      return false;
    };

    const auto onboard = [&simulation, &call]()
    {
      auto lock = simulation.Lock();
      for (const auto& elevator : simulation.GetManagement()->GetElevatorSnapshots())
        for (const auto& passenger : elevator.passengers)
          if (passenger == call)
            return true;
      return false;
    };

    Check(WaitFor([&waiting]() { return !waiting(); }, std::chrono::seconds(10)), "B picks the passenger up");
    Check(WaitFor([&onboard]() { return !onboard(); }, std::chrono::seconds(20)), "the passenger reaches the destination");

    // Boarding events count every boarding, the statistics only the first one of every passenger.
    std::set<std::string> boardedIds;
    std::size_t boardingsOfTheCall = 0U;
    const auto callIdField = "\"id\":" + std::to_string(call->GetId()) + ",";
    for (const auto& event : EventBus::Since(0U, 100000U))
    {
      if (event.type != "board")
        continue;

      boardedIds.insert(event.data.substr(0U, event.data.find(',')));
      if (event.data.find(callIdField) != std::string::npos)
        ++boardingsOfTheCall;
    }

    Check(boardingsOfTheCall == 2U, "the passenger boarded twice (A, then B)");
    Check(Statistics::GetSnapshot().boardedPassengers == boardedIds.size(), "the second boarding is not counted twice in the statistics");

    simulation.SetOutOfOrder("A", false);
    simulation.Stop();
  }

  void Restart()
  {
    std::cout << "Restart with new settings" << std::endl;

    auto settings = BaseSettings();
    settings.dayStartHour = 8.0;
    Configuration::Apply(settings);

    Simulation simulation;
    simulation.Start();
    WaitFor([]() { return Statistics::GetSnapshot().queuedCalls > 10U; }, std::chrono::seconds(10));

    settings.numberOfElevators = 6;
    settings.numberOfFloors = 25;
    simulation.Restart(settings);

    {
      auto lock = simulation.Lock();
      Check(simulation.GetManagement()->GetElevatorSnapshots().size() == 6U, "the new run has the new number of cabins");
    }

    Check(Floors::TopFloor() == 24U, "the new run has the new number of floors");
    Check(SimulationClock::Now() < 60 * SimulationClock::HourMs, "the clock started again");

    const auto queuedAfterRestart = Statistics::GetSnapshot().queuedCalls;
    Check(queuedAfterRestart < 200U, "the statistics started again");

    settings.numberOfFloors = 1;
    bool rejected = false;
    try
    {
      simulation.Restart(settings);
    }
    catch (const std::invalid_argument&)
    {
      rejected = true;
    }

    Check(rejected && simulation.Running(), "invalid settings are rejected and the current run goes on");
    simulation.Stop();
  }

  void Scenarios()
  {
    std::cout << "Reproducible scenarios" << std::endl;

    auto settings = BaseSettings();
    Configuration::Apply(settings);

    const auto first = Scenario::Build(42U);
    const auto again = Scenario::Build(42U);
    const auto other = Scenario::Build(43U);

    const auto same = [](const std::vector<ScheduledCall>& a, const std::vector<ScheduledCall>& b)
    {
      if (a.size() != b.size())
        return false;
      for (std::size_t index = 0U; index < a.size(); ++index)
        if (a[index].atMs != b[index].atMs || a[index].from != b[index].from || a[index].to != b[index].to ||
            a[index].patienceMs != b[index].patienceMs)
          return false;
      return true;
    };

    Check(first.size() > 1000U, "a day of an office produces the expected number of calls (" + std::to_string(first.size()) + ")");
    Check(same(first, again), "the same seed gives exactly the same passengers");
    Check(!same(first, other), "another seed gives other passengers");

    // Morning in an office: most trips start from the lobby.
    std::size_t morning = 0U;
    std::size_t fromLobby = 0U;
    for (const auto& call : first)
    {
      const auto hour = 6.0 + static_cast<double>(call.atMs) / SimulationClock::HourMs;
      if (hour >= 7.5 && hour <= 9.0)
      {
        ++morning;
        fromLobby += call.from == 0U ? 1U : 0U;
      }
    }
    Check(morning > 0U && fromLobby * 2U > morning, "in the office morning most passengers start from the lobby");

    const auto key = Scenario::Key(42U);
    settings.numberOfElevators = 6;
    settings.dispatchStrategy = Configuration::Dispatch::Strategy::Nearest;
    Configuration::Apply(settings);
    Check(Scenario::Key(42U) == key, "cabins and strategy do not change the scenario");

    settings.numberOfFloors = 12;
    Configuration::Apply(settings);
    Check(Scenario::Key(42U) != key, "the floors change the scenario");
  }

  void Strategies()
  {
    std::cout << "Every strategy on the same scenario (1 simulated hour at 2000x)" << std::endl;

    using Strategy = Configuration::Dispatch::Strategy;
    const Strategy strategies[] = { Strategy::Optimized, Strategy::Nearest, Strategy::Collective };

    const auto archived = RunArchive::All().size();
    std::vector<std::uint64_t> trips;

    for (const auto strategy : strategies)
    {
      auto settings = BaseSettings();
      settings.dayStartHour = 8.0;
      settings.numberOfCalls = 150;
      settings.averageCallsPerDay = 3000;
      settings.randomSeed = 77;
      settings.dispatchStrategy = strategy;
      Configuration::Apply(settings);

      Simulation simulation;
      simulation.Start();
      const auto finished = WaitFor([&simulation]() { simulation.CheckFinished(); return simulation.Archived(); }, std::chrono::seconds(30));
      const auto statistics = Statistics::GetSnapshot();
      Management::EnergyTotals energy;
      {
        auto lock = simulation.Lock();
        energy = simulation.GetManagement()->GetEnergy();
      }
      simulation.Stop();

      const std::string name = Configuration::Dispatch::StrategyName(strategy);
      Check(finished, name + ": the run ends and is archived");
      Check(statistics.completedPassengers + statistics.abandonedCalls == statistics.queuedCalls, name + ": nobody is left behind");
      Check(energy.travelWh > 0.0 && energy.standbyWh > 0.0, name + ": energy is counted (travel and standby)");
      trips.push_back(statistics.queuedCalls);
    }

    Check(trips[0] == 150U && trips[0] == trips[1] && trips[1] == trips[2], "the three strategies served the same 150 passengers");
    Check(RunArchive::All().size() == archived + 3U, "the three runs are in the archive");
    Check(RunArchive::All().back().find("\"strategy\":\"Collective\"") != std::string::npos, "the archive records the strategy");
  }

  void Parking()
  {
    std::cout << "Idle cabins spread along the building" << std::endl;

    auto settings = BaseSettings();
    settings.numberOfFloors = 10;
    settings.numberOfElevators = 3;
    settings.numberOfCalls = 1;
    settings.averageCallsPerDay = 1;
    settings.maxDelayBetweenCalls = std::chrono::hours(20);
    settings.idleParking = Configuration::Dispatch::Parking::Spread;
    Configuration::Apply(settings);

    Simulation simulation;
    simulation.Start();

    const auto parked = WaitFor([&simulation]()
      {
        auto lock = simulation.Lock();
        const auto cabins = simulation.GetManagement()->GetElevatorSnapshots();
        return cabins[0].currentFloor == 1U && cabins[1].currentFloor == 5U && cabins[2].currentFloor == 8U &&
          cabins[2].status == ElevatorStatus::Idle;
      }, std::chrono::seconds(10));

    Check(parked, "without calls the cabins wait at floors 1, 5 and 8");
    simulation.Stop();
  }

  void Stairs()
  {
    std::cout << "Passengers who lose their patience take the stairs" << std::endl;

    auto settings = BaseSettings();
    settings.numberOfElevators = 1;
    settings.numberOfCalls = 1;
    settings.averageCallsPerDay = 1;
    settings.maxDelayBetweenCalls = std::chrono::hours(20);
    settings.stairsPatienceSeconds = 30.0;
    settings.dayDurationSeconds = 864.0;   // 100x
    Configuration::Apply(settings);

    Simulation simulation;
    simulation.Start();
    simulation.SetOutOfOrder("A", true);

    const auto added = simulation.AddWave(TrafficProfile::Wave::Random, 6U);
    const auto patient = simulation.AddCall(2, 5);   // own passengers never give up

    Check(WaitFor([]() { return Statistics::GetSnapshot().abandonedCalls >= 6U; }, std::chrono::seconds(10)),
      "with the only cabin out of order the " + std::to_string(added) + " passengers of the wave take the stairs");
    // The scenario of the run has one passenger of its own, who also finds no cabin.
    WaitFor([&simulation, added]() { return Statistics::GetSnapshot().abandonedCalls == added + simulation.GeneratedCalls(); }, std::chrono::seconds(5));
    Check(Statistics::GetSnapshot().abandonedCalls == added + simulation.GeneratedCalls(), "exactly the passengers without a cabin");

    bool stillWaiting = false;
    for (const auto& person : Floors::GetPeople().Snapshot())
      stillWaiting = stillWaiting || person == patient;
    Check(stillWaiting, "your own passenger keeps waiting");

    simulation.Stop();
  }
}

namespace
{
  void Twin()
  {
    std::cout << "Two buildings side by side on the same clock" << std::endl;

    auto left = BaseSettings();
    left.dayStartHour = 8.0;
    left.numberOfCalls = 120;
    left.averageCallsPerDay = 3000;
    left.randomSeed = 99;
    left.dayDurationSeconds = 864.0 / 4.0;   // 400x
    auto right = left;
    right.numberOfElevators = 2;
    right.dispatchStrategy = Configuration::Dispatch::Strategy::Nearest;

    Simulation main;
    Simulation twin("twin");
    main.SetPauseWhenFinished(false);
    twin.SetPauseWhenFinished(false);
    main.SetPairId("pair-test");
    twin.SetPairId("pair-test");

    main.Restart(left);
    twin.Restart(right, "", false);

    // Each building sees its own settings, also from the threads of the test.
    {
      Context::Scope scope(twin.GetContext());
      Check(Configuration::Building::NumberOfElevators() == 2U, "the twin building has its own settings");
    }
    Check(Configuration::Building::NumberOfElevators() == 3U, "the main building keeps its settings");

    // A fault in the twin does not touch the main building.
    twin.SetOutOfOrder("B", true);
    {
      auto lock = main.Lock();
      bool anyOut = false;
      for (const auto& elevator : main.GetManagement()->GetElevatorSnapshots())
        anyOut = anyOut || elevator.outOfOrder;
      Check(!anyOut, "a fault in one building stays in that building");
    }
    twin.SetOutOfOrder("B", false);

    const auto bothDone = WaitFor([&main, &twin]()
      {
        main.CheckFinished();
        twin.CheckFinished();
        return main.Archived() && twin.Archived();
      }, std::chrono::seconds(60));
    Check(bothDone, "both buildings finish and are archived");

    StatisticsSnapshot mainStatistics;
    StatisticsSnapshot twinStatistics;
    mainStatistics = Statistics::GetSnapshot();
    {
      Context::Scope scope(twin.GetContext());
      twinStatistics = Statistics::GetSnapshot();
    }

    Check(mainStatistics.queuedCalls == 120U && twinStatistics.queuedCalls == 120U, "the same 120 passengers came to both buildings");
    Check(mainStatistics.elevatorAssignments.size() == 3U && twinStatistics.elevatorAssignments.size() <= 2U,
      "each building counts its own cabins");
    Check(mainStatistics.completedPassengers + mainStatistics.abandonedCalls == 120U &&
      twinStatistics.completedPassengers + twinStatistics.abandonedCalls == 120U, "both buildings served everybody");

    const auto runs = RunArchive::All();
    const auto paired = std::count_if(runs.begin(), runs.end(),
      [](const std::string& run) { return run.find("\"pair\":\"pair-test\"") != std::string::npos; });
    Check(paired == 2, "the two runs are archived as a pair");

    std::size_t twinEvents = 0U;
    for (const auto& event : EventBus::Since(0U, 100000U))
      twinEvents += event.building == "twin" ? 1U : 0U;
    Check(twinEvents > 0U, "the events of the twin are tagged with its name");

    twin.Stop();
    main.Stop();
  }
}

int main()
{
  Clock();
  WholeRun();
  OutOfOrder();
  Restart();
  Scenarios();
  Strategies();
  Parking();
  Stairs();
  Twin();

  LogBase::FlushPendingMessages();

  std::cout << std::endl << (g_checks - g_failures) << "/" << g_checks << " checks passed" << std::endl;
  return g_failures == 0 ? 0 : 1;
}
