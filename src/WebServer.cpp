#include "WebServer.h"

#include "Call.h"
#include "Configuration.h"
#include "Elevator.h"
#include "EventBus.h"
#include "Floors.h"
#include "Json.h"
#include "Management.h"
#include "People.h"
#include "Simulation.h"
#include "SimulationClock.h"
#include "Statistics.h"
#include "TrafficProfile.h"
#include "RunArchive.h"
#include "Scenario.h"
#include "Zoning.h"
#include "Version.h"
#include "BuildingContext.h"

// cpp-httplib is third party code: it is compiled without the warnings of the project (Visual Studio uses /W4 /WX).
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "../third_party/cpp-httplib/httplib.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <iostream>
#include <thread>
#include <sstream>

namespace
{
  const char* JsonType = "application/json; charset=utf-8";

  const char* StatusName(const ElevatorStatus status)
  {
    switch (status)
    {
    case ElevatorStatus::OutOfOrder: return "outOfOrder";
    case ElevatorStatus::PeopleEnterAndExit: return "boarding";
    case ElevatorStatus::Moving: return "moving";
    case ElevatorStatus::Idle:
    default: return "idle";
    }
  }

  const char* DirectionName(const Direction direction)
  {
    switch (direction)
    {
    case Direction::Up: return "up";
    case Direction::Down: return "down";
    case Direction::Both: return "both";
    case Direction::None:
    default: return "none";
    }
  }

  const char* DoorsName(const ElevatorSnapshot& elevator)
  {
    switch (elevator.doorsMotion)
    {
    case DoorsMotion::Opening: return "opening";
    case DoorsMotion::Closing: return "closing";
    case DoorsMotion::None:
    default: return elevator.doorsOpen ? "open" : "closed";
    }
  }

  unsigned long long Id(const std::shared_ptr<Call>& call) { return static_cast<unsigned long long>(call->GetId()); }

  void Reply(httplib::Response& response, const Json::Writer& body, const int status = 200)
  {
    response.status = status;
    response.set_content(body.Str(), JsonType);
    response.set_header("Cache-Control", "no-store");
  }

  void ReplyError(httplib::Response& response, const std::string& message, const int status = 400)
  {
    Reply(response, Json::Writer::Object().Add("error", message), status);
  }

  bool ParseUnsigned(const std::string& text, unsigned int& value)
  {
    if (text.empty() || text.size() > 9U || !std::all_of(text.begin(), text.end(), [](const unsigned char c) { return std::isdigit(c) != 0; }))
      return false;

    value = static_cast<unsigned int>(std::stoul(text));
    return true;
  }

  bool ParseNumber(const std::string& text, double& value)
  {
    try
    {
      std::size_t used = 0U;
      value = std::stod(text, &used);
      return used == text.size() && std::isfinite(value);
    }
    catch (const std::exception&)
    {
      return false;
    }
  }

  Json::Writer ClockJson(const Simulation& simulation)
  {
    const auto speed = SimulationClock::Speed();

    return Json::Writer::Object()
      .Add("nowMs", SimulationClock::Now())
      .Add("timeOfDayMs", SimulationClock::TimeOfDayMs())
      .Add("startTimeOfDayMs", SimulationClock::StartTimeOfDayMs())
      .Add("day", Configuration::Simulation::CurrentDay())
      .Add("days", Configuration::CallsGenerator::NumberOfSimulationDays())
      .Add("label", Configuration::Simulation::CurrentDayTimeLabel())
      .Add("speed", speed, 3)
      .Add("dayDurationSeconds", static_cast<double>(SimulationClock::DayMs) / 1000.0 / speed, 1)
      .Add("paused", SimulationClock::Paused())
      .Add("dayOver", Configuration::Simulation::Completed())
      .Add("running", simulation.Running())
      .Add("finished", simulation.Finished());
  }

  Json::Writer RunJson(Simulation& simulation)
  {
    const auto batch = simulation.GetBatchStatus();

    auto batchJson = Json::Writer::Object();
    batchJson.Add("running", batch.running);
    if (!batch.id.empty())
      batchJson.Add("id", batch.id)
        .Add("index", static_cast<unsigned long long>(batch.index))
        .Add("total", static_cast<unsigned long long>(batch.total))
        .Add("label", batch.label);

    return Json::Writer::Object()
      .Add("seed", simulation.Seed())
      .Add("scenario", simulation.ScenarioKey())
      .Add("interventions", simulation.Interventions())
      .Add("strategy", Configuration::Dispatch::StrategyName(Configuration::Dispatch::DispatchStrategy()))
      .Add("parking", Configuration::Dispatch::ParkingName(Configuration::Dispatch::IdleParking()))
      .Add("batch", batchJson);
  }

  Json::Writer GroupsJson()
  {
    auto groups = Json::Writer::Array();
    for (const auto& group : Zoning::Groups(Configuration::Get()))
      groups.Push(Json::Writer::Object()
        .Add("name", group.name)
        .Add("lobby", group.lobby)
        .Add("from", group.from)
        .Add("to", group.to)
        .Add("cabins", group.cabins)
        .Add("shuttle", group.shuttle));
    return groups;
  }

  Json::Writer BuildingJson()
  {
    return Json::Writer::Object()
      .Add("floors", Configuration::Building::NumberOfFloors())
      .Add("elevators", Configuration::Building::NumberOfElevators())
      .Add("maxPeople", Configuration::Elevator::MaxPeople())
      .Add("floorMs", static_cast<long long>(Configuration::Elevator::TimeToReachTheNextFloor().count()))
      .Add("doorMs", static_cast<long long>(Configuration::Elevator::DoorTransitionTime().count()))
      .Add("dwellMs", static_cast<long long>(Configuration::Elevator::EnterAndExitTime().count()))
      .Add("pattern", Configuration::PatternName(Configuration::CallsGenerator::TrafficPattern()))
      .Add("patienceS", Configuration::Get().stairsPatienceSeconds, 0)
      .Add("happyS", Configuration::Get().happyWaitSeconds, 0)
      .Add("layout", Zoning::Layout(Configuration::Get()))
      .Add("groups", GroupsJson());
  }

  Json::Writer ElevatorJson(const ElevatorSnapshot& elevator)
  {
    auto passengers = Json::Writer::Array();
    for (const auto& passenger : elevator.passengers)
      passengers.Push(Json::Writer::Object()
        .Add("id", Id(passenger))
        .Add("to", passenger->GetDestinationFloor())
        .Add("manual", passenger->IsManual()));

    auto stops = Json::Writer::Array();
    for (std::size_t floor = 0U; floor < elevator.stops.size(); ++floor)
      if (elevator.stops[floor].first)
        stops.Push(Json::Writer::Object()
          .Add("floor", static_cast<unsigned int>(floor))
          .Add("dir", DirectionName(elevator.stops[floor].second)));

    auto json = Json::Writer::Object();
    json.Add("id", elevator.id)
      .Add("floor", elevator.currentFloor)
      .Add("status", StatusName(elevator.status))
      .Add("dir", DirectionName(elevator.direction))
      .Add("outOfOrder", elevator.outOfOrder)
      .Add("doors", DoorsName(elevator))
      .Add("doorsStartMs", elevator.doorsMotionStartMs);

    if (Floors::IsValid(elevator.motionToFloor))
      json.Add("motionTo", elevator.motionToFloor).Add("motionStartMs", elevator.motionStartMs);

    if (Floors::IsValid(elevator.targetFloor))
      json.Add("target", elevator.targetFloor);

    json.Add("passengers", passengers)
      .Add("stops", stops)
      .Add("travelledFloors", static_cast<unsigned long long>(elevator.travelledFloors))
      .Add("runs", static_cast<unsigned long long>(elevator.runs))
      .Add("doorCycles", static_cast<unsigned long long>(elevator.doorCycles))
      .Add("travelWh", elevator.travelEnergyWh, 1)
      .Add("group", elevator.group)
      .Add("motionMs", elevator.motionDurationMs);

    return json;
  }

  Json::Writer StatisticsJson(Simulation& simulation)
  {
    const auto statistics = Statistics::GetSnapshot();
    const auto waits = Statistics::GetWaitDistribution();
    auto* management = simulation.GetManagement();

    auto assignments = Json::Writer::Object();
    for (const auto& item : statistics.elevatorAssignments)
      assignments.Add(item.elevatorId, static_cast<unsigned long long>(item.assignedCalls));

    const auto energy = management == nullptr ? Management::EnergyTotals() : management->GetEnergy();

    return Json::Writer::Object()
      .Add("travelWh", energy.travelWh, 1)
      .Add("standbyWh", energy.standbyWh, 1)
      .Add("energyWh", energy.TotalWh(), 1)
      .Add("whPerTrip", statistics.completedPassengers == 0U ? 0.0 : energy.TotalWh() / static_cast<double>(statistics.completedPassengers), 2)
      .Add("queued", static_cast<unsigned long long>(statistics.queuedCalls))
      .Add("assigned", static_cast<unsigned long long>(statistics.assignedCalls))
      .Add("boarded", static_cast<unsigned long long>(statistics.boardedPassengers))
      .Add("completed", static_cast<unsigned long long>(statistics.completedPassengers))
      .Add("active", static_cast<unsigned long long>(Statistics::ActiveCalls()))
      .Add("generated", simulation.GeneratedCalls())
      .Add("pending", static_cast<unsigned long long>(management == nullptr ? 0U : management->PendingCallsCount()))
      .Add("avgWaitMs", statistics.AverageWaitTimeMs(), 0)
      .Add("maxWaitMs", static_cast<unsigned long long>(statistics.maxWaitTimeMs))
      .Add("p50WaitMs", waits.p50Ms, 0)
      .Add("p95WaitMs", waits.p95Ms, 0)
      .Add("p99WaitMs", waits.p99Ms, 0)
      .Add("avgJourneyMs", statistics.AverageJourneyTimeMs(), 0)
      .Add("maxJourneyMs", static_cast<unsigned long long>(statistics.maxJourneyTimeMs))
      .Add("requeued", static_cast<unsigned long long>(statistics.requeuedCalls))
      .Add("stairs", static_cast<unsigned long long>(statistics.abandonedCalls))
      .Add("happy", static_cast<unsigned long long>(statistics.happyPassengers))
      .Add("transfers", static_cast<unsigned long long>(statistics.transfers))
      .Add("transferWaitMs", statistics.AverageTransferWaitMs(), 0)
      .Add("capacityLimited", static_cast<unsigned long long>(statistics.capacityLimitedDecisions))
      .Add("peakCabinPeople", static_cast<unsigned long long>(statistics.maxCabinPeople))
      .Add("assignedByElevator", assignments);
  }

  Json::Writer SettingsJson(const Configuration::Settings& settings)
  {
    return Json::Writer::Object()
      .Add("numberOfElevators", settings.numberOfElevators)
      .Add("numberOfFloors", settings.numberOfFloors)
      .Add("maxPeople", settings.maxPeoplePerElevator)
      .Add("averageCallsPerDay", settings.averageCallsPerDay)
      .Add("maxConcurrentCalls", settings.maxConcurrentCalls)
      .Add("numberOfSimulationDays", settings.numberOfSimulationDays)
      .Add("trafficPattern", Configuration::PatternName(settings.trafficPattern))
      .Add("dispatchStrategy", Configuration::Dispatch::StrategyName(settings.dispatchStrategy))
      .Add("idleParking", Configuration::Dispatch::ParkingName(settings.idleParking))
      .Add("stairsPatienceSeconds", settings.stairsPatienceSeconds, 0)
      .Add("happyWaitSeconds", settings.happyWaitSeconds, 0)
      .Add("standbyPowerW", settings.standbyPowerW, 0)
      .Add("motorEfficiency", settings.motorEfficiency, 2)
      .Add("layout", Zoning::Layout(settings))
      .Add("expressFloorFactor", settings.expressFloorFactor, 2)
      .Add("dayDurationSeconds", settings.dayDurationSeconds, 1)
      .Add("dayStartHour", settings.dayStartHour, 2)
      .Add("randomSeed", settings.randomSeed)
      .Add("timeToReachTheNextFloorMs", static_cast<long long>(settings.timeToReachTheNextFloor.count()))
      .Add("doorTransitionTimeMs", static_cast<long long>(settings.doorTransitionTime.count()))
      .Add("enterAndExitTimeMs", static_cast<long long>(settings.enterAndExitTime.count()));
  }
}

namespace
{
  bool IsWord(const std::string& text)
  {
    return !text.empty() && text.size() < 32U &&
      std::all_of(text.begin(), text.end(), [](const unsigned char c) { return std::isalpha(c) != 0; });
  }

  /**
   * \brief Reads the settings of a run from the request parameters over 'settings'. The parameters have the names
   *        of config.json: they are turned into a small JSON text and read by the same parser and validation of the
   *        configuration file. Only plain numbers and single words are accepted. Throws std::invalid_argument.
   */
  void ParseSettings(const httplib::Request& request, Configuration::Settings& settings)
  {
    static const char* NumericKeys[] = {
      "numberOfElevators", "numberOfFloors", "maxPeople", "averageCallsPerDay", "maxConcurrentCalls",
      "numberOfSimulationDays", "dayDurationSeconds", "dayStartHour", "randomSeed", "stairsPatienceSeconds", "happyWaitSeconds",
      "standbyPowerW", "motorEfficiency", "expressFloorFactor",
      "timeToReachTheNextFloorMs", "doorTransitionTimeMs", "enterAndExitTimeMs" };

    static const char* WordKeys[] = { "trafficPattern", "dispatchStrategy", "idleParking" };

    std::string json = "{";
    for (const auto* key : NumericKeys)
    {
      if (!request.has_param(key))
        continue;

      const auto value = request.get_param_value(key);
      double number = 0.0;
      if (!ParseNumber(value, number))
        throw std::invalid_argument(std::string(key) + " must be a number");

      json += "\"" + std::string(key) + "\": " + value + ", ";
    }

    for (const auto* key : WordKeys)
    {
      if (!request.has_param(key))
        continue;

      const auto value = request.get_param_value(key);
      if (!IsWord(value))
        throw std::invalid_argument(std::string(key) + " must be a single word");

      json += "\"" + std::string(key) + "\": \"" + value + "\", ";
    }

    json += "\"type\": \"Random\" }";
    Configuration::ParseJson(json, settings);

    // Cabins and zones in one parameter: "4", "1-20:6, 21-40:6", "... @ 30:4" (see Zoning.h).
    if (request.has_param("layout"))
    {
      Zoning::ApplyLayout(request.get_param_value("layout"), settings);
      Configuration::Validate(settings);
    }
  }

  std::vector<std::string> SplitList(const std::string& text)
  {
    std::vector<std::string> items;
    std::string item;
    for (const auto character : text + ",")
    {
      if (character == ',')
      {
        if (!item.empty())
          items.push_back(item);
        item.clear();
      }
      else if (character != ' ')
      {
        item.push_back(character);
      }
    }
    return items;
  }
}

struct WebServer::Implementation
{
  Implementation(Simulation& simulation, std::string webRoot) : simulation(simulation), webRoot(std::move(webRoot)) {}

  void RegisterRoutes();

  // When every passenger of the run has arrived the clock is paused once, so that the run does not keep ticking
  // through empty days while the report is shown; the user can resume it at will.
  void PauseWhenFinished();
  // Commands that change the run are refused while a comparison is running, so that its runs stay comparable.
  bool RejectDuringBatch(httplib::Response& response);

  std::string StateJson(std::uint64_t sinceSequence);
  // Cabins, waiting passengers, statistics and run of one building (entered with its context).
  std::string BuildingStateJson(Simulation& building);

  // Twin run: a second building on the same scenario and the same clock, next to the main one.
  void StartTwin(const Configuration::Settings& left, const Configuration::Settings& right);
  void StopTwin();
  // The building named by a request parameter: "twin" for the second building, the main one otherwise (or nullptr).
  Simulation* Building(const std::string& name);

  Simulation& simulation;
  std::recursive_mutex twinMutex;
  std::unique_ptr<Simulation> twin;
  bool twinPaused{ false };
  std::string webRoot;
  httplib::Server server;
  std::unique_ptr<std::thread> thread;
};

void WebServer::Implementation::PauseWhenFinished()
{
  std::lock_guard<std::recursive_mutex> lock(twinMutex);

  // Archives the run (and pauses the clock) the first time its end is seen.
  simulation.CheckFinished();
  if (twin == nullptr)
    return;

  // A twin run pauses only when both buildings are done: the faster one waits for the other.
  twin->CheckFinished();
  if (!twinPaused && simulation.Archived() && twin->Archived())
  {
    twinPaused = true;
    SimulationClock::Pause();
    EventBus::Publish("twinFinished", Json::Writer::Object().Add("pair", simulation.PairId()));
  }
}

Simulation* WebServer::Implementation::Building(const std::string& name)
{
  std::lock_guard<std::recursive_mutex> lock(twinMutex);

  if (name == "twin")
    return twin.get();

  return &simulation;
}

void WebServer::Implementation::StopTwin()
{
  std::lock_guard<std::recursive_mutex> lock(twinMutex);

  if (twin != nullptr)
  {
    twin->Stop();
    twin.reset();
  }

  simulation.SetPauseWhenFinished(true);
  simulation.SetPairId("");
}

void WebServer::Implementation::StartTwin(const Configuration::Settings& left, const Configuration::Settings& right)
{
  Configuration::Validate(left);
  Configuration::Validate(right);

  std::lock_guard<std::recursive_mutex> lock(twinMutex);
  StopTwin();

  const auto pairId = RunArchive::NewId("pair-");

  // The main building starts the shared clock; the twin joins it at once, so the scenario (same seed) brings the
  // same passengers to both at the same simulated times.
  simulation.SetPauseWhenFinished(false);
  simulation.SetPairId(pairId);
  simulation.Restart(left);

  twin = std::make_unique<Simulation>("twin");
  twin->SetPauseWhenFinished(false);
  twin->SetPairId(pairId);
  twin->Restart(right, "", false);
  twinPaused = false;
}

bool WebServer::Implementation::RejectDuringBatch(httplib::Response& response)
{
  if (!simulation.GetBatchStatus().running)
    return false;

  ReplyError(response, "a comparison is running: wait for it to finish or cancel it", 409);
  return true;
}

std::string WebServer::Implementation::BuildingStateJson(Simulation& building)
{
  Context::Scope scope(building.GetContext());
  auto lock = building.Lock();

  auto elevators = Json::Writer::Array();
  if (auto* management = building.GetManagement())
    for (const auto& elevator : management->GetElevatorSnapshots())
      elevators.Push(ElevatorJson(elevator));

  auto waiting = Json::Writer::Array();
  for (const auto& person : Floors::GetPeople().Snapshot())
    waiting.Push(Json::Writer::Object()
      .Add("id", Id(person))
      .Add("from", person->GetStartFloor())
      .Add("to", person->GetDestinationFloor())
      .Add("elevator", person->GetAssignedElevator())
      .Add("waitMs", static_cast<long long>(person->GetWaitingTime().count()))
      .Add("manual", person->IsManual())
      .Add("evacuated", person->GetEvacuations() > 0U)
      .Add("finalTo", person->GetFinalDestination())
      .Add("transfer", person->GetTransfers() > 0U));

  // Members of the state object, without the braces: the main building is at the top level of the state.
  auto json = Json::Writer::Object()
    .Add("run", RunJson(building))
    .Add("building", BuildingJson())
    .Add("elevators", elevators)
    .Add("waiting", waiting)
    .Add("stats", StatisticsJson(building))
    .Str();

  return json.substr(1U, json.size() - 2U);
}

std::string WebServer::Implementation::StateJson(const std::uint64_t sinceSequence)
{
  std::lock_guard<std::recursive_mutex> twinLock(twinMutex);
  PauseWhenFinished();

  auto events = Json::Writer::Array();
  for (const auto& event : EventBus::Since(sinceSequence))
    events.Push(EventBus::ToJson(event));

  std::string text = "{\"clock\":" + ClockJson(simulation).Str() + "," + BuildingStateJson(simulation);
  if (twin != nullptr)
    text += ",\"twin\":{" + BuildingStateJson(*twin) + "}";

  text += ",\"events\":" + events.Str() + ",\"lastSeq\":" + std::to_string(EventBus::LastSequence()) + "}";
  return text;
}

void WebServer::Implementation::RegisterRoutes()
{
  server.Get("/api/state", [this](const httplib::Request& request, httplib::Response& response)
  {
    unsigned int since = 0U;
    std::uint64_t sinceSequence = 0U;
    if (request.has_param("since") && ParseUnsigned(request.get_param_value("since"), since))
      sinceSequence = since;

    response.set_content(StateJson(sinceSequence), JsonType);
    response.set_header("Cache-Control", "no-store");
  });

  server.Get("/api/config", [this](const httplib::Request&, httplib::Response& response)
  {
    auto lock = simulation.Lock();

    auto curve = Json::Writer::Array();
    for (const auto value : TrafficProfile::DailyCurve(96U))
      curve.PushNumber(value, 3);

    Reply(response, Json::Writer::Object()
      .Add("version", ELEVATOR_VERSION)
      .Add("settings", SettingsJson(simulation.UserSettings()))
      .Add("trafficCurve", curve)
      .Add("minSpeed", SimulationClock::MinSpeed, 1)
      .Add("maxSpeed", SimulationClock::MaxSpeed, 0));
  });

  server.Get("/api/report", [this](const httplib::Request& request, httplib::Response& response)
  {
    auto* building = Building(request.get_param_value("building"));
    if (building == nullptr)
      return ReplyError(response, "there is no twin building", 404);

    Context::Scope scope(building->GetContext());
    auto lock = building->Lock();
    auto lines = Json::Writer::Array();

    if (auto* management = building->GetManagement())
      for (const auto& line : management->BuildReport())
        lines.PushString(line);

    Reply(response, Json::Writer::Object().Add("lines", lines));
  });

  server.Post("/api/control", [](const httplib::Request& request, httplib::Response& response)
  {
    const auto action = request.get_param_value("action");

    if (action == "pause")
    {
      SimulationClock::Pause();
    }
    else if (action == "resume")
    {
      SimulationClock::Resume();
    }
    else if (action == "step")
    {
      unsigned int stepMs = 60000U;
      if (request.has_param("ms") && !ParseUnsigned(request.get_param_value("ms"), stepMs))
        return ReplyError(response, "ms must be a positive number of simulated milliseconds");

      SimulationClock::Step(stepMs);
    }
    else if (action == "speed")
    {
      double speed = 0.0;
      if (!ParseNumber(request.get_param_value("value"), speed) || speed <= 0.0)
        return ReplyError(response, "value must be a positive speed (simulated seconds per real second)");

      SimulationClock::SetSpeed(speed);
    }
    else
    {
      return ReplyError(response, "action must be pause, resume, step or speed");
    }

    EventBus::Publish("control", Json::Writer::Object()
      .Add("action", action)
      .Add("speed", SimulationClock::Speed(), 2)
      .Add("paused", SimulationClock::Paused()));

    Reply(response, Json::Writer::Object().Add("speed", SimulationClock::Speed(), 3).Add("paused", SimulationClock::Paused()));
  });

  server.Post("/api/call", [this](const httplib::Request& request, httplib::Response& response)
  {
    unsigned int from = 0U;
    unsigned int to = 0U;

    if (!ParseUnsigned(request.get_param_value("from"), from) || !ParseUnsigned(request.get_param_value("to"), to))
      return ReplyError(response, "from and to must be floor numbers");

    if (RejectDuringBatch(response))
      return;

    auto lock = simulation.Lock();

    if (!Floors::IsValid(from) || !Floors::IsValid(to) || from == to)
      return ReplyError(response, "from and to must be two different floors between 0 and " + std::to_string(Floors::TopFloor()));

    const auto call = simulation.AddCall(from, to);
    if (call == nullptr)
      return ReplyError(response, "the simulation is not running", 409);

    // In a twin run the same passenger appears in both buildings, so the comparison stays fair.
    auto ids = Json::Writer::Array();
    ids.PushNumber(static_cast<double>(call->GetId()), 0);
    {
      std::lock_guard<std::recursive_mutex> twinLock(twinMutex);
      if (twin != nullptr)
        if (const auto mirrored = twin->AddCall(from, to))
          ids.PushNumber(static_cast<double>(mirrored->GetId()), 0);
    }

    Reply(response, Json::Writer::Object().Add("id", Id(call)).Add("ids", ids));
  });

  server.Post("/api/wave", [this](const httplib::Request& request, httplib::Response& response)
  {
    const auto kind = request.get_param_value("kind");
    unsigned int count = 20U;

    if (request.has_param("count") && (!ParseUnsigned(request.get_param_value("count"), count) || count == 0U || count > 500U))
      return ReplyError(response, "count must be between 1 and 500");

    TrafficProfile::Wave wave = TrafficProfile::Wave::Random;
    if (kind == "morning")
      wave = TrafficProfile::Wave::Morning;
    else if (kind == "lunch")
      wave = TrafficProfile::Wave::Lunch;
    else if (kind == "evening")
      wave = TrafficProfile::Wave::Evening;
    else if (kind != "random")
      return ReplyError(response, "kind must be morning, lunch, evening or random");

    if (RejectDuringBatch(response))
      return;

    // The wave is decided once: in a twin run both buildings get the same people.
    const auto trips = simulation.MakeWave(wave, count);
    const auto added = simulation.AddTrips(trips);
    {
      std::lock_guard<std::recursive_mutex> twinLock(twinMutex);
      if (twin != nullptr)
        twin->AddTrips(trips);
    }
    if (added == 0U)
      return ReplyError(response, "the simulation is not running", 409);

    EventBus::Publish("wave", Json::Writer::Object().Add("kind", kind).Add("count", added));
    Reply(response, Json::Writer::Object().Add("added", added));
  });

  server.Post("/api/elevator", [this](const httplib::Request& request, httplib::Response& response)
  {
    const auto id = request.get_param_value("id");
    const auto state = request.get_param_value("outOfOrder");

    if (state != "true" && state != "false")
      return ReplyError(response, "outOfOrder must be true or false");

    if (RejectDuringBatch(response))
      return;

    auto* building = Building(request.get_param_value("building"));
    if (building == nullptr || !building->SetOutOfOrder(id, state == "true"))
      return ReplyError(response, "unknown elevator '" + id + "' or simulation not running", 404);

    Reply(response, Json::Writer::Object().Add("id", id).Add("outOfOrder", state == "true"));
  });

  server.Post("/api/restart", [this](const httplib::Request& request, httplib::Response& response)
  {
    if (RejectDuringBatch(response))
      return;

    try
    {
      auto settings = simulation.UserSettings();
      ParseSettings(request, settings);
      Configuration::Validate(settings);
      StopTwin();
      simulation.Restart(settings);
    }
    catch (const std::exception& error)
    {
      return ReplyError(response, error.what());
    }

    Reply(response, Json::Writer::Object().Add("settings", SettingsJson(simulation.UserSettings())));
  });

  server.Get("/api/runs", [](const httplib::Request&, httplib::Response& response)
  {
    auto runs = Json::Writer::Array();
    for (const auto& record : RunArchive::All())
      runs.Push(record);

    Reply(response, Json::Writer::Object().Add("runs", runs));
  });

  server.Post("/api/runs/clear", [](const httplib::Request&, httplib::Response& response)
  {
    RunArchive::Clear();
    Reply(response, Json::Writer::Object().Add("runs", Json::Writer::Array()));
  });

  server.Post("/api/batch", [this](const httplib::Request& request, httplib::Response& response)
  {
    // The scenario (seed, floors, traffic) is the same for every variant; strategies, parking and number of
    // cabins vary. The variants run one after the other at the highest speed.
    constexpr std::size_t MaxVariants = 24U;

    try
    {
      auto base = simulation.UserSettings();
      ParseSettings(request, base);
      base.randomSeed = Scenario::ResolveSeed(base.randomSeed);
      base.dayDurationSeconds = static_cast<double>(SimulationClock::DayMs) / 1000.0 / SimulationClock::MaxSpeed;

      auto strategies = SplitList(request.get_param_value("strategies"));
      auto parkings = SplitList(request.get_param_value("parkings"));
      // Layouts are separated by '|' because a layout with zones contains commas.
      std::vector<std::string> layouts;
      {
        std::stringstream stream(request.get_param_value("layouts"));
        std::string layout;
        while (std::getline(stream, layout, '|'))
          if (layout.find_first_not_of(' ') != std::string::npos)
            layouts.push_back(layout);
      }
      for (const auto& count : SplitList(request.get_param_value("elevators")))
        layouts.push_back(count);
      if (strategies.empty())
        strategies.push_back(Configuration::Dispatch::StrategyName(base.dispatchStrategy));
      if (parkings.empty())
        parkings.push_back(Configuration::Dispatch::ParkingName(base.idleParking));
      if (layouts.empty())
        layouts.push_back(Zoning::Layout(base));

      if (strategies.size() * parkings.size() * layouts.size() > MaxVariants)
        throw std::invalid_argument("at most " + std::to_string(MaxVariants) + " variants in one comparison");

      std::vector<Simulation::Variant> variants;
      for (const auto& layout : layouts)
        for (const auto& strategy : strategies)
          for (const auto& parking : parkings)
          {
            if (!IsWord(strategy) || !IsWord(parking))
              throw std::invalid_argument("variants must be lists of strategies, parking rules and layouts");

            Simulation::Variant variant;
            variant.settings = base;
            Zoning::ApplyLayout(layout, variant.settings);
            Configuration::ParseJson("{ \"dispatchStrategy\": \"" + strategy + "\", \"idleParking\": \"" + parking + "\" }",
              variant.settings);

            const auto cabins = Zoning::TotalCabins(variant.settings);
            const auto zoned = !variant.settings.zones.empty();
            variant.label = std::string(Configuration::Dispatch::StrategyName(variant.settings.dispatchStrategy)) + ", " +
              Configuration::Dispatch::ParkingName(variant.settings.idleParking) + ", " +
              (zoned ? "zones " + Zoning::Layout(variant.settings) : std::to_string(cabins) + " cabins");
            variants.push_back(variant);
          }

      StopTwin();
      const auto id = simulation.StartBatch(variants);
      Reply(response, Json::Writer::Object()
        .Add("batch", id)
        .Add("variants", static_cast<unsigned long long>(variants.size()))
        .Add("seed", base.randomSeed));
    }
    catch (const std::exception& error)
    {
      return ReplyError(response, error.what());
    }
  });

  server.Post("/api/batch/cancel", [this](const httplib::Request&, httplib::Response& response)
  {
    simulation.CancelBatch();
    Reply(response, Json::Writer::Object().Add("cancelled", true));
  });

  server.Post("/api/twin", [this](const httplib::Request& request, httplib::Response& response)
  {
    // Two buildings on the same scenario: the parameters of the scenario have the names of config.json, the two
    // variants are given by left* and right* parameters (dispatchStrategy, idleParking, numberOfElevators, maxPeople).
    if (RejectDuringBatch(response))
      return;

    try
    {
      auto base = simulation.UserSettings();
      ParseSettings(request, base);
      base.randomSeed = Scenario::ResolveSeed(base.randomSeed);

      const auto variant = [&request, &base](const std::string& side)
      {
        // leftDispatchStrategy, rightNumberOfElevators...
        const auto name = [&side](std::string key)
        {
          key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
          return side + key;
        };

        auto settings = base;
        std::string json = "{";

        for (const auto* key : { "dispatchStrategy", "idleParking" })
        {
          const auto value = request.get_param_value(name(key));
          if (value.empty())
            continue;
          if (!IsWord(value))
            throw std::invalid_argument(name(key) + " must be a single word");
          json += "\"" + std::string(key) + "\": \"" + value + "\", ";
        }

        for (const auto* key : { "numberOfElevators", "maxPeople" })
        {
          const auto value = request.get_param_value(name(key));
          if (value.empty())
            continue;
          unsigned int number = 0U;
          if (!ParseUnsigned(value, number))
            throw std::invalid_argument(name(key) + " must be a number");
          json += "\"" + std::string(key) + "\": " + value + ", ";
        }

        json += "\"type\": \"Random\" }";
        Configuration::ParseJson(json, settings);

        const auto layout = request.get_param_value(name("layout"));
        if (!layout.empty())
          Zoning::ApplyLayout(layout, settings);

        return settings;
      };

      StartTwin(variant("left"), variant("right"));
      Reply(response, Json::Writer::Object().Add("seed", base.randomSeed));
    }
    catch (const std::exception& error)
    {
      return ReplyError(response, error.what());
    }
  });

  server.Post("/api/twin/stop", [this](const httplib::Request&, httplib::Response& response)
  {
    StopTwin();
    Reply(response, Json::Writer::Object().Add("twin", false));
  });

  if (!server.set_mount_point("/", webRoot))
    std::cerr << "Web interface folder '" << webRoot << "' not found: only the /api endpoints are available "
              << "(start the program from the repository folder or use --web-root)." << std::endl;

  server.set_exception_handler([](const httplib::Request&, httplib::Response& response, std::exception_ptr error)
  {
    std::string message = "internal error";
    try
    {
      if (error)
        std::rethrow_exception(error);
    }
    catch (const std::exception& exception)
    {
      message = exception.what();
    }
    catch (...)
    {
    }

    ReplyError(response, message, 500);
  });
}

WebServer::WebServer(Simulation& simulation, std::string webRoot) :
  m_implementation(std::make_unique<Implementation>(simulation, std::move(webRoot)))
{
  m_implementation->RegisterRoutes();
}

WebServer::~WebServer()
{
  Stop();
}

bool WebServer::Start(const std::string& host, const int port)
{
  if (m_implementation->thread != nullptr)
    return true;

  if (!m_implementation->server.bind_to_port(host, port))
    return false;

  m_implementation->thread = std::make_unique<std::thread>([this]() { m_implementation->server.listen_after_bind(); });
  return true;
}

void WebServer::Stop()
{
  if (m_implementation->thread == nullptr)
    return;

  m_implementation->server.stop();

  if (m_implementation->thread->joinable())
    m_implementation->thread->join();

  m_implementation->thread.reset();
}
