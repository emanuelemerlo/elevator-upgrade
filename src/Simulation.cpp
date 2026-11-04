#include "Simulation.h"

#include "Call.h"
#include "EventBus.h"
#include "Management.h"
#include "People.h"
#include "PeopleCallsGenerator.h"
#include "SimulationClock.h"
#include "Statistics.h"

#include <chrono>

Simulation::Simulation() :
  m_random(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()))
{
}

Simulation::~Simulation()
{
  Stop();
}

void Simulation::Start()
{
  auto lock = Lock();

  if (Running())
    return;

  // A new run: nothing of the previous one may leak into it.
  m_management.reset();
  m_generator.reset();
  Floors::GetPeople().Clear();
  Statistics::Reset();
  EventBus::Clear();
  Call::ResetIds();

  Configuration::Simulation::StartClock();

  m_management = std::make_unique<Management>(Configuration::Building::NumberOfElevators());
  m_generator = std::make_unique<PeopleCallsGenerator>(*m_management);
  m_stopped = false;

  EventBus::Publish("start", Json::Writer::Object()
    .Add("elevators", Configuration::Building::NumberOfElevators())
    .Add("floors", Configuration::Building::NumberOfFloors())
    .Add("speed", SimulationClock::Speed(), 2));

  switch (Configuration::CallsGenerator::GeneratorType())
  {
  case Configuration::CallsGenerator::Type::Random:
    m_generator->StartRandom();
    break;
  case Configuration::CallsGenerator::Type::Fixed:
  default:
    m_generator->StartFixed();
  }
}

void Simulation::Stop()
{
  auto lock = Lock();
  StopLocked();
}

void Simulation::StopLocked()
{
  if (m_stopped || m_management == nullptr)
    return;

  // Order matters: the cabins are asked to stop first, then the clock is aborted so that every sleep
  // (also a paused one) returns at once, then the threads are joined.
  m_management->RequestShutdown();
  SimulationClock::Abort();

  m_generator->Shutdown();
  m_management->Shutdown();
  m_stopped = true;

  EventBus::Publish("stop", Json::Writer::Object());
}

void Simulation::Restart(const Configuration::Settings& settings)
{
  Configuration::Validate(settings);

  auto lock = Lock();
  StopLocked();

  // Destroyed only now: the final report of the previous run reads the cabins until a new run starts.
  m_generator.reset();
  m_management.reset();

  Configuration::Apply(settings);
  Start();
}

bool Simulation::Finished() const
{
  return GeneratorFinished() && Statistics::ActiveCalls() == 0U;
}

bool Simulation::GeneratorFinished() const
{
  return m_generator != nullptr && m_generator->Finished();
}

unsigned int Simulation::GeneratedCalls() const
{
  return m_generator == nullptr ? 0U : m_generator->GeneratedCalls();
}

std::shared_ptr<Call> Simulation::AddCall(const Floors::FloorNumber from, const Floors::FloorNumber to)
{
  auto lock = Lock();

  if (!Running())
    return nullptr;

  auto call = std::make_shared<Call>(from, to, true);
  if (!call->IsValid())
    return nullptr;

  Floors::GetPeople().Insert(call);
  m_management->AssignCall(call);
  return call;
}

unsigned int Simulation::AddWave(const TrafficProfile::Wave wave, const unsigned int count)
{
  auto lock = Lock();

  if (!Running())
    return 0U;

  unsigned int added = 0U;
  for (auto index = 0U; index < count; ++index)
  {
    const auto trip = TrafficProfile::PickTrip(m_random, wave, Configuration::CallsGenerator::TrafficPattern());
    auto call = std::make_shared<Call>(trip.first, trip.second);

    if (!call->IsValid())
      continue;

    Floors::GetPeople().Insert(call);
    m_management->AssignCall(call);
    ++added;
  }

  return added;
}

bool Simulation::SetOutOfOrder(const std::string& elevatorId, const bool outOfOrder)
{
  auto lock = Lock();

  if (!Running())
    return false;

  return m_management->SetOutOfOrder(elevatorId, outOfOrder);
}
