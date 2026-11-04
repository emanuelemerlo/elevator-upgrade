#include "Simulation.h"

#include "Call.h"
#include "EventBus.h"
#include "Management.h"
#include "People.h"
#include "PeopleCallsGenerator.h"
#include "SimulationClock.h"
#include "Statistics.h"
#include "RunArchive.h"
#include "Scenario.h"
#include "BuildingContext.h"

#include <chrono>
#include <ctime>

Simulation::Simulation() :
  m_context(&Context::Default()),
  m_random(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count())),
  m_userSettings(Context::Default().settings)
{
}

Simulation::Simulation(const std::string& buildingName) :
  m_ownedContext(std::make_unique<BuildingContext>(buildingName)),
  m_context(m_ownedContext.get()),
  m_random(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count())),
  m_userSettings(Context::Default().settings)
{
  // A new building starts from the settings of the main one.
  m_context->settings = Context::Default().settings;
}

Configuration::Settings Simulation::UserSettings()
{
  Context::Scope scope(m_context);
  auto lock = Lock();
  return m_userSettings;
}

Simulation::~Simulation()
{
  CancelBatch();
  if (m_batchThread != nullptr && m_batchThread->joinable())
    m_batchThread->join();

  Stop();
}

void Simulation::Start(const bool leader)
{
  Context::Scope scope(m_context);
  auto lock = Lock();
  StartLocked(leader);
}

void Simulation::StartLocked(const bool leader)
{
  if (Running())
    return;

  // A new run: nothing of the previous one may leak into it.
  m_management.reset();
  m_generator.reset();
  Floors::GetPeople().Clear();
  Statistics::Reset();
  m_context->aborted = false;

  if (leader)
  {
    EventBus::Clear();
    Call::ResetIds();
    Configuration::Simulation::StartClock();
  }

  m_management = std::make_unique<Management>(Configuration::Building::NumberOfElevators());
  m_generator = std::make_unique<PeopleCallsGenerator>(*m_management);
  m_stopped = false;
  m_archived = false;
  m_interventions = 0U;
  m_seed = Scenario::ResolveSeed(Configuration::Simulation::RandomSeed());
  m_scenarioKey = Scenario::Key(m_seed);

  EventBus::Publish("start", Json::Writer::Object()
    .Add("elevators", Configuration::Building::NumberOfElevators())
    .Add("floors", Configuration::Building::NumberOfFloors())
    .Add("speed", SimulationClock::Speed(), 2)
    .Add("seed", m_seed)
    .Add("scenario", m_scenarioKey)
    .Add("strategy", Configuration::Dispatch::StrategyName(Configuration::Dispatch::DispatchStrategy()))
    .Add("parking", Configuration::Dispatch::ParkingName(Configuration::Dispatch::IdleParking()))
    .Add("batch", m_batchId));

  switch (Configuration::CallsGenerator::GeneratorType())
  {
  case Configuration::CallsGenerator::Type::Random:
    m_generator->StartRandom(m_seed);
    break;
  case Configuration::CallsGenerator::Type::Fixed:
  default:
    m_generator->StartFixed();
  }
}

void Simulation::Stop()
{
  Context::Scope scope(m_context);
  auto lock = Lock();
  StopLocked();
}

void Simulation::StopLocked()
{
  if (m_stopped || m_management == nullptr)
    return;

  // Order matters: the cabins are asked to stop first, then the building is aborted so that every sleep of its
  // threads (also on a paused clock) returns at once, then the threads are joined. The clock keeps running for
  // the other buildings, if any.
  m_management->RequestShutdown();
  m_context->aborted = true;
  SimulationClock::WakeAll();

  m_generator->Shutdown();
  m_management->Shutdown();
  m_stopped = true;

  EventBus::Publish("stop", Json::Writer::Object());
}

void Simulation::Restart(const Configuration::Settings& settings, const std::string& batchId, const bool leader)
{
  Configuration::Validate(settings);

  Context::Scope scope(m_context);
  auto lock = Lock();
  StopLocked();
  m_batchId = batchId;
  if (batchId.empty())
    m_userSettings = settings;

  // Destroyed only now: the final report of the previous run reads the cabins until a new run starts.
  m_generator.reset();
  m_management.reset();

  Configuration::Apply(settings);
  StartLocked(leader);
}

bool Simulation::Finished() const
{
  Context::Scope scope(m_context);
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
  Context::Scope scope(m_context);
  auto lock = Lock();

  if (!Running())
    return nullptr;

  auto call = std::make_shared<Call>(from, to, true);
  if (!call->IsValid())
    return nullptr;

  ++m_interventions;
  Floors::GetPeople().Insert(call);
  m_management->AssignCall(call);
  return call;
}

unsigned int Simulation::AddWave(const TrafficProfile::Wave wave, const unsigned int count)
{
  return AddTrips(MakeWave(wave, count));
}

std::vector<ScheduledCall> Simulation::MakeWave(const TrafficProfile::Wave wave, const unsigned int count)
{
  Context::Scope scope(m_context);
  auto lock = Lock();

  std::vector<ScheduledCall> trips;
  std::uniform_real_distribution<double> uniform(0.0, 1.0);

  for (auto index = 0U; index < count; ++index)
  {
    const auto trip = TrafficProfile::PickTrip(m_random, wave, Configuration::CallsGenerator::TrafficPattern());

    ScheduledCall call;
    call.from = trip.first;
    call.to = trip.second;
    call.patienceMs = Scenario::PatienceFor(uniform(m_random));
    trips.push_back(call);
  }

  return trips;
}

unsigned int Simulation::AddTrips(const std::vector<ScheduledCall>& trips)
{
  Context::Scope scope(m_context);
  auto lock = Lock();

  if (!Running())
    return 0U;

  unsigned int added = 0U;
  for (const auto& trip : trips)
  {
    auto call = std::make_shared<Call>(trip.from, trip.to);
    if (!call->IsValid())
      continue;

    call->SetPatience(trip.patienceMs);
    Floors::GetPeople().Insert(call);
    m_management->AssignCall(call);
    ++added;
  }

  if (added > 0U)
    ++m_interventions;

  return added;
}

bool Simulation::SetOutOfOrder(const std::string& elevatorId, const bool outOfOrder)
{
  Context::Scope scope(m_context);
  auto lock = Lock();

  if (!Running())
    return false;

  const auto done = m_management->SetOutOfOrder(elevatorId, outOfOrder);
  if (done && outOfOrder)
    ++m_interventions;

  return done;
}

bool Simulation::CheckFinished()
{
  Context::Scope scope(m_context);
  auto lock = Lock();

  if (!Running() || m_archived || !Finished())
    return false;

  m_archived = true;
  if (m_pauseWhenFinished)
    SimulationClock::Pause();
  Archive();
  return true;
}

bool Simulation::Archived()
{
  Context::Scope scope(m_context);
  auto lock = Lock();
  return m_archived;
}

void Simulation::Archive()
{
  const auto& settings = Configuration::Get();
  const auto statistics = Statistics::GetSnapshot();
  const auto waits = Statistics::GetWaitDistribution();

  std::uint64_t travelledFloors = 0U;
  std::uint64_t runs = 0U;
  std::uint64_t doorCycles = 0U;
  for (const auto& elevator : m_management->GetElevatorSnapshots())
  {
    travelledFloors += elevator.travelledFloors;
    runs += elevator.runs;
    doorCycles += elevator.doorCycles;
  }

  const auto energy = m_management->GetEnergy();
  const auto delivered = statistics.completedPassengers;

  char finishedAt[32] = {};
  const auto now = std::time(nullptr);
  std::strftime(finishedAt, sizeof(finishedAt), "%Y-%m-%d %H:%M", std::localtime(&now));

  const auto id = RunArchive::NewId("run-");
  const auto u64 = [](const std::uint64_t value) { return static_cast<unsigned long long>(value); };

  // Flat on purpose: every key becomes a column of the comparison table and of the CSV export.
  const auto record = Json::Writer::Object()
    .Add("id", id)
    .Add("finishedAt", std::string(finishedAt))
    .Add("batch", m_batchId)
    .Add("pair", m_pairId)
    .Add("scenario", m_scenarioKey)
    .Add("seed", m_seed)
    .Add("clean", m_interventions == 0U)
    .Add("interventions", m_interventions.load())
    .Add("strategy", Configuration::Dispatch::StrategyName(settings.dispatchStrategy))
    .Add("parking", Configuration::Dispatch::ParkingName(settings.idleParking))
    .Add("elevators", settings.numberOfElevators)
    .Add("floors", settings.numberOfFloors)
    .Add("maxPeople", settings.maxPeoplePerElevator)
    .Add("pattern", Configuration::PatternName(settings.trafficPattern))
    .Add("callsPerDay", settings.averageCallsPerDay)
    .Add("days", settings.numberOfSimulationDays)
    .Add("startHour", settings.dayStartHour, 2)
    .Add("floorMs", static_cast<long long>(settings.timeToReachTheNextFloor.count()))
    .Add("doorMs", static_cast<long long>(settings.doorTransitionTime.count()))
    .Add("dwellMs", static_cast<long long>(settings.enterAndExitTime.count()))
    .Add("patienceS", settings.stairsPatienceSeconds, 0)
    .Add("trips", u64(statistics.queuedCalls))
    .Add("delivered", u64(statistics.completedPassengers))
    .Add("stairs", u64(statistics.abandonedCalls))
    .Add("avgWaitMs", statistics.AverageWaitTimeMs(), 0)
    .Add("p50WaitMs", waits.p50Ms, 0)
    .Add("p95WaitMs", waits.p95Ms, 0)
    .Add("p99WaitMs", waits.p99Ms, 0)
    .Add("maxWaitMs", u64(statistics.maxWaitTimeMs))
    .Add("avgJourneyMs", statistics.AverageJourneyTimeMs(), 0)
    .Add("maxJourneyMs", u64(statistics.maxJourneyTimeMs))
    .Add("travelledFloors", u64(travelledFloors))
    .Add("runs", u64(runs))
    .Add("doorCycles", u64(doorCycles))
    .Add("capacityWaits", u64(statistics.capacityLimitedDecisions))
    .Add("travelKWh", energy.travelWh / 1000.0, 3)
    .Add("standbyKWh", energy.standbyWh / 1000.0, 3)
    .Add("energyKWh", energy.TotalWh() / 1000.0, 3)
    .Add("whPerTrip", delivered == 0U ? 0.0 : energy.TotalWh() / static_cast<double>(delivered), 2)
    .Add("standbyW", settings.standbyPowerW, 0)
    .Add("endedAt", Configuration::Simulation::CurrentDayTimeLabel())
    .Str();

  RunArchive::Add(record);

  EventBus::Publish("finished", Json::Writer::Object()
    .Add("completed", u64(statistics.completedPassengers))
    .Add("stairs", u64(statistics.abandonedCalls))
    .Add("run", id)
    .Add("batch", m_batchId)
    .Add("pair", m_pairId)
    .Add("clean", m_interventions == 0U));
}

std::string Simulation::StartBatch(const std::vector<Variant>& variants)
{
  if (variants.empty())
    throw std::invalid_argument("a comparison needs at least one variant");

  for (const auto& variant : variants)
    Configuration::Validate(variant.settings);

  std::lock_guard<std::mutex> lock(m_batchMutex);

  if (m_batch.running)
    throw std::runtime_error("a comparison is already running");

  if (m_batchThread != nullptr && m_batchThread->joinable())
    m_batchThread->join();

  m_batch = BatchStatus();
  m_batch.running = true;
  m_batch.id = RunArchive::NewId("batch-");
  m_batch.total = variants.size();
  m_batchCancel = false;

  m_batchThread = std::make_unique<std::thread>(&Simulation::BatchThread, this, variants, m_batch.id);
  return m_batch.id;
}

void Simulation::CancelBatch()
{
  m_batchCancel = true;
}

Simulation::BatchStatus Simulation::GetBatchStatus()
{
  std::lock_guard<std::mutex> lock(m_batchMutex);
  return m_batch;
}

void Simulation::BatchThread(std::vector<Variant> variants, std::string batchId)
{
  std::size_t completed = 0U;

  for (std::size_t index = 0U; index < variants.size() && !m_batchCancel; ++index)
  {
    {
      std::lock_guard<std::mutex> lock(m_batchMutex);
      m_batch.index = index;
      m_batch.label = variants[index].label;
    }

    try
    {
      Restart(variants[index].settings, batchId);
    }
    catch (const std::exception&)
    {
      break;
    }

    EventBus::Publish("batch", Json::Writer::Object()
      .Add("batch", batchId)
      .Add("index", static_cast<unsigned long long>(index))
      .Add("total", static_cast<unsigned long long>(variants.size()))
      .Add("label", variants[index].label));

    while (!m_batchCancel)
    {
      // The end of the run may also be detected by a web request: whoever comes first archives it.
      CheckFinished();
      if (Archived())
      {
        ++completed;
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_batchMutex);
    m_batch.running = false;
  }

  EventBus::Publish("batchDone", Json::Writer::Object()
    .Add("batch", batchId)
    .Add("completed", static_cast<unsigned long long>(completed))
    .Add("total", static_cast<unsigned long long>(variants.size()))
    .Add("cancelled", m_batchCancel.load()));
}
