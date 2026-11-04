#include "PeopleCallsGenerator.h"

#include "Call.h"
#include "Management.h"
#include "Floors.h"
#include "People.h"
#include "Watchdog.h"
#include "Configuration.h"
#include "Statistics.h"
#include "TrafficProfile.h"
#include "Scenario.h"
#include "SimulationClock.h"

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

void PeopleCallsGenerator::StartRandom(const unsigned int seed)
{
  m_finished = false;

  // The whole day is decided now: same seed and traffic settings, same passengers at the same times.
  m_seed = seed;
  m_schedule = Scenario::Build(seed);
  m_log.Trace("Scenario seed " + std::to_string(seed) + ": " + std::to_string(m_schedule.size()) + " calls", Log::TraceLevel::Info);

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

  for (const auto& scheduled : peopleCallsGenerator->m_schedule)
  {
    // Every sleep returns false when the simulation is aborted: the generator must not wait for the next call.
    if (!SimulationClock::SleepUntil(scheduled.atMs))
      break;

    // Safety limit of the building population: the call waits (it keeps its passenger) until there is room.
    auto aborted = false;
    while (MaxConcurrentCallsReached() && !StopRequested())
    {
      if (!Configuration::Simulation::SleepFor(1s))
      {
        aborted = true;
        break;
      }
    }

    if (aborted || StopRequested())
      break;

    const auto call = std::make_shared<Call>(scheduled.from, scheduled.to);
    call->SetPatience(scheduled.patienceMs);

    peopleCallsGenerator->m_log.Trace("Generated call " + call->ToString());

    Floors::GetPeople().Insert(call);

    // Assign call synchronously to avoid unbounded thread creation
    peopleCallsGenerator->m_management.AssignCall(call);
    ++peopleCallsGenerator->m_generatedCalls;
  }

  peopleCallsGenerator->m_log.Trace("Scenario completed: generator stopped");
  peopleCallsGenerator->m_finished = true;
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

