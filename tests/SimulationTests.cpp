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
    Check(SimulationClock::TimeOfDayMs() - SimulationClock::Now() == 6 * SimulationClock::HourMs, "the day starts at the configured hour");

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
    Check(statistics.completedPassengers == statistics.queuedCalls, "every passenger reached the destination");
    Check(statistics.requeuedCalls == 0U, "no boarding refused by a full cabin");
    Check(Configuration::Simulation::Completed(), "the simulated clock is past the end of the last day");
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
}

int main()
{
  Clock();
  WholeRun();
  OutOfOrder();
  Restart();

  LogBase::FlushPendingMessages();

  std::cout << std::endl << (g_checks - g_failures) << "/" << g_checks << " checks passed" << std::endl;
  return g_failures == 0 ? 0 : 1;
}
