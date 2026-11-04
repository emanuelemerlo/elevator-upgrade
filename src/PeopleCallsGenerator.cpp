#include "PeopleCallsGenerator.h"

#include "Call.h"
#include "Management.h"
#include "Floors.h"
#include "People.h"
#include "Watchdog.h"
#include "Configuration.h"
#include "Statistics.h"
#include "TrafficProfile.h"

#include <random>
#include <chrono>
#include <future>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>

using namespace std::chrono_literals;

PeopleCallsGenerator::PeopleCallsGenerator(Management& management) : m_management(management)
{
  m_log.SetTraceId("Generator");
}

PeopleCallsGenerator::~PeopleCallsGenerator()
{
  Shutdown();
}

namespace
{
  // Delay before the next call (simulated time): arrivals are random (exponential, i.e. a Poisson process) around
  // the average delay of the current hour, which follows the daily profile, then kept in the configured range.
  std::chrono::milliseconds DelayForDailyProfile(std::default_random_engine& generator)
  {
    const auto averageCallsPerDay = static_cast<double>(Configuration::CallsGenerator::AverageCallsPerDay());
    const auto averageDelayMs = static_cast<double>(SimulationClock::DayMs) / averageCallsPerDay;
    const auto profiledDelayMs = averageDelayMs / TrafficProfile::Intensity(Configuration::Simulation::CurrentHour());

    std::exponential_distribution<double> arrival(1.0 / profiledDelayMs);

    const long long lowerBound = std::max(1LL, Configuration::CallsGenerator::MinDelayBetweenCalls());
    const long long upperBound = std::max(lowerBound, Configuration::CallsGenerator::MaxDelayBetweenCalls());
    const auto delayMs = std::min(static_cast<double>(upperBound), std::max(static_cast<double>(lowerBound), arrival(generator)));

    return std::chrono::milliseconds(static_cast<long long>(delayMs));
  }

  bool MaxConcurrentCallsReached()
  {
    return Statistics::ActiveCalls() >= Configuration::CallsGenerator::MaxConcurrentCalls();
  }

  unsigned int GeneratorSeed()
  {
    const auto configuredSeed = Configuration::Simulation::RandomSeed();
    if (configuredSeed != 0U)
      return configuredSeed;

    return static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count());
  }
}

void PeopleCallsGenerator::StartRandom()
{
  m_finished = false;

  if (m_generatorThread != nullptr && m_generatorThread->IsActive())
    m_generatorThread->Stop();

  m_generatorThread = std::make_unique<RandomGeneratorThread>(this);
  m_generatorThread->Start();
  m_generatorThread->Go();
}

void PeopleCallsGenerator::StartFixed()
{
  m_finished = false;

  if (m_generatorThread != nullptr && m_generatorThread->IsActive())
    m_generatorThread->Stop();

  m_generatorThread = std::make_unique<FixedGeneratorThread>(this);
  m_generatorThread->Start();
  m_generatorThread->Go();
}

void PeopleCallsGenerator::Shutdown()
{
  if (m_generatorThread == nullptr)
    return;

  m_generatorThread->Stop();
  m_generatorThread.reset();

  m_log.Trace("Shutdown completed", Log::TraceLevel::Verbose);
}

void PeopleCallsGenerator::RandomGeneratorThread::CycleFunction(PeopleCallsGenerator* peopleCallsGenerator)
{
  if (peopleCallsGenerator == nullptr || StopRequested())
    return;

  if (!Configuration::Simulation::SleepFor(2s)) // arbitrary delay before start, interrupted by an abort
  {
    peopleCallsGenerator->m_finished = true;
    return;
  }

  // Same seed -> same sequence of calls (floors and delays); the seed is traced to reproduce a run.
  const auto seed = GeneratorSeed();
  peopleCallsGenerator->m_log.Trace("Random seed: " + std::to_string(seed), Log::TraceLevel::Info);
  std::default_random_engine generator(seed);

  const auto pattern = Configuration::CallsGenerator::TrafficPattern();

  const auto maxCalls = Configuration::CallsGenerator::NumberOfCalls();
  unsigned int generatedCalls = 0U;

  do
  {
    if (Configuration::Simulation::Completed())
    {
      peopleCallsGenerator->m_log.Trace("Simulation days completed: generator stopped");
      break;
    }

    if (generatedCalls >= maxCalls)
    {
      peopleCallsGenerator->m_log.Trace("Configured number of calls reached: generator stopped");
      break;
    }

    if (MaxConcurrentCallsReached())
    {
      if (!Configuration::Simulation::SleepFor(DelayForDailyProfile(generator)))
        break;

      continue;
    }

    // Who travels depends on the hour: arrivals in the morning, departures in the evening (see TrafficProfile).
    const auto trip = TrafficProfile::PickTrip(generator, Configuration::Simulation::CurrentHour(), pattern);
    const auto call = std::make_shared<Call>(trip.first, trip.second);

    peopleCallsGenerator->m_log.Trace("Generated call " + call->ToString());

    Floors::GetPeople().Insert(call);

    // Assign call synchronously to avoid unbounded thread creation
    peopleCallsGenerator->m_management.AssignCall(call);
    ++generatedCalls;
    ++peopleCallsGenerator->m_generatedCalls;

    // The sleep returns false when the simulation is aborted: the generator must not wait for the next call.
    if (!Configuration::Simulation::SleepFor(DelayForDailyProfile(generator)))
      break;

  } while (!StopRequested());

  peopleCallsGenerator->m_finished = true;
  peopleCallsGenerator->m_log.Trace("CycleFunction exit", ILog::TraceLevel::Debug);
}

void PeopleCallsGenerator::FixedGeneratorThread::CycleFunction(PeopleCallsGenerator* peopleCallsGenerator)
{
  if (peopleCallsGenerator == nullptr)
    return;

  std::default_random_engine generator(GeneratorSeed());

  const std::uniform_int_distribution<long long> randomDelay(
    Configuration::CallsGenerator::MinDelayBetweenCalls(),
    Configuration::CallsGenerator::MaxDelayBetweenCalls());

  if (!Configuration::Simulation::SleepFor(2s)) // arbitrary delay before start, interrupted by an abort
  {
    peopleCallsGenerator->m_finished = true;
    return;
  }

  const auto topFloor = Floors::TopFloor();
  const auto bottomFloor = Floors::BottomFloor;

  std::list<std::shared_ptr<Call>> calls = {
    std::make_shared<Call>(2,1),
    std::make_shared<Call>(5,1),
    std::make_shared<Call>(6,1),
    std::make_shared<Call>(7,1),
    std::make_shared<Call>(8,1),

    std::make_shared<Call>(0,8),
    std::make_shared<Call>(2,8),
    std::make_shared<Call>(3,8),
    std::make_shared<Call>(4,8),
    std::make_shared<Call>(6,8),

    std::make_shared<Call>(1, topFloor),
    std::make_shared<Call>(3, topFloor),
    std::make_shared<Call>(5, topFloor),
    std::make_shared<Call>(7, topFloor),
    std::make_shared<Call>(8, topFloor),

    std::make_shared<Call>(bottomFloor, 2),
    std::make_shared<Call>(bottomFloor, 4),
    std::make_shared<Call>(bottomFloor, 6),
    std::make_shared<Call>(bottomFloor, 8),
    std::make_shared<Call>(bottomFloor, 9)
  };

  for (const auto& call : calls)
  {
    if (StopRequested())
      break;

    if (!call->IsValid())
      continue;

    peopleCallsGenerator->m_log.Trace("Asking call assignment " + call->ToString());

    Floors::GetPeople().Insert(call);

    // Assign call synchronously
    peopleCallsGenerator->m_management.AssignCall(call);
    ++peopleCallsGenerator->m_generatedCalls;

    auto getDelay = std::bind(randomDelay, std::ref(generator));

    // The sleep returns false when the simulation is aborted: the remaining calls are not generated.
    if (!Configuration::Simulation::SleepFor(std::chrono::milliseconds(getDelay())))
      break;
  }

  peopleCallsGenerator->m_finished = true;
}

