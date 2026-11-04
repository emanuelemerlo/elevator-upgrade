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

  const char* PatternName(const Configuration::CallsGenerator::Pattern pattern)
  {
    switch (pattern)
    {
    case Configuration::CallsGenerator::Pattern::Uniform: return "Uniform";
    case Configuration::CallsGenerator::Pattern::Residential: return "Residential";
    case Configuration::CallsGenerator::Pattern::Office:
    default: return "Office";
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

  Json::Writer BuildingJson()
  {
    return Json::Writer::Object()
      .Add("floors", Configuration::Building::NumberOfFloors())
      .Add("elevators", Configuration::Building::NumberOfElevators())
      .Add("maxPeople", Configuration::Elevator::MaxPeople())
      .Add("floorMs", static_cast<long long>(Configuration::Elevator::TimeToReachTheNextFloor().count()))
      .Add("doorMs", static_cast<long long>(Configuration::Elevator::DoorTransitionTime().count()))
      .Add("dwellMs", static_cast<long long>(Configuration::Elevator::EnterAndExitTime().count()))
      .Add("pattern", PatternName(Configuration::CallsGenerator::TrafficPattern()));
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
      .Add("doorCycles", static_cast<unsigned long long>(elevator.doorCycles));

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

    return Json::Writer::Object()
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
      .Add("capacityLimited", static_cast<unsigned long long>(statistics.capacityLimitedDecisions))
      .Add("peakCabinPeople", static_cast<unsigned long long>(statistics.maxCabinPeople))
      .Add("assignedByElevator", assignments);
  }

  Json::Writer SettingsJson()
  {
    const auto& settings = Configuration::Get();

    return Json::Writer::Object()
      .Add("numberOfElevators", settings.numberOfElevators)
      .Add("numberOfFloors", settings.numberOfFloors)
      .Add("maxPeople", settings.maxPeoplePerElevator)
      .Add("averageCallsPerDay", settings.averageCallsPerDay)
      .Add("maxConcurrentCalls", settings.maxConcurrentCalls)
      .Add("numberOfSimulationDays", settings.numberOfSimulationDays)
      .Add("trafficPattern", PatternName(settings.trafficPattern))
      .Add("dayDurationSeconds", settings.dayDurationSeconds, 1)
      .Add("dayStartHour", settings.dayStartHour, 2)
      .Add("randomSeed", settings.randomSeed)
      .Add("timeToReachTheNextFloorMs", static_cast<long long>(settings.timeToReachTheNextFloor.count()))
      .Add("doorTransitionTimeMs", static_cast<long long>(settings.doorTransitionTime.count()))
      .Add("enterAndExitTimeMs", static_cast<long long>(settings.enterAndExitTime.count()));
  }
}

struct WebServer::Implementation
{
  Implementation(Simulation& simulation, std::string webRoot) : simulation(simulation), webRoot(std::move(webRoot)) {}

  void RegisterRoutes();

  // When every passenger of the run has arrived the clock is paused once, so that the run does not keep ticking
  // through empty days while the report is shown; the user can resume it at will.
  void PauseWhenFinished();

  std::string StateJson(std::uint64_t sinceSequence);

  Simulation& simulation;
  std::string webRoot;
  httplib::Server server;
  std::unique_ptr<std::thread> thread;
  std::atomic_bool finishedNotified{ false };
};

void WebServer::Implementation::PauseWhenFinished()
{
  if (!simulation.Running() || !simulation.Finished())
    return;

  if (finishedNotified.exchange(true))
    return;

  SimulationClock::Pause();
  EventBus::Publish("finished", Json::Writer::Object()
    .Add("completed", static_cast<unsigned long long>(Statistics::GetSnapshot().completedPassengers)));
}

std::string WebServer::Implementation::StateJson(const std::uint64_t sinceSequence)
{
  auto lock = simulation.Lock();
  PauseWhenFinished();

  auto elevators = Json::Writer::Array();
  if (auto* management = simulation.GetManagement())
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
      .Add("evacuated", person->GetEvacuations() > 0U));

  auto events = Json::Writer::Array();
  for (const auto& event : EventBus::Since(sinceSequence))
    events.Push(EventBus::ToJson(event));

  return Json::Writer::Object()
    .Add("clock", ClockJson(simulation))
    .Add("building", BuildingJson())
    .Add("elevators", elevators)
    .Add("waiting", waiting)
    .Add("stats", StatisticsJson(simulation))
    .Add("events", events)
    .Add("lastSeq", static_cast<unsigned long long>(EventBus::LastSequence()))
    .Str();
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
      .Add("settings", SettingsJson())
      .Add("trafficCurve", curve)
      .Add("minSpeed", SimulationClock::MinSpeed, 1)
      .Add("maxSpeed", SimulationClock::MaxSpeed, 0));
  });

  server.Get("/api/report", [this](const httplib::Request&, httplib::Response& response)
  {
    auto lock = simulation.Lock();
    auto lines = Json::Writer::Array();

    if (auto* management = simulation.GetManagement())
      for (const auto& line : management->BuildReport())
        lines.PushString(line);

    Reply(response, Json::Writer::Object().Add("lines", lines));
  });

  server.Post("/api/control", [this](const httplib::Request& request, httplib::Response& response)
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

    auto lock = simulation.Lock();

    if (!Floors::IsValid(from) || !Floors::IsValid(to) || from == to)
      return ReplyError(response, "from and to must be two different floors between 0 and " + std::to_string(Floors::TopFloor()));

    const auto call = simulation.AddCall(from, to);
    if (call == nullptr)
      return ReplyError(response, "the simulation is not running", 409);

    finishedNotified = false;
    Reply(response, Json::Writer::Object().Add("id", Id(call)));
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

    const auto added = simulation.AddWave(wave, count);
    if (added == 0U)
      return ReplyError(response, "the simulation is not running", 409);

    finishedNotified = false;
    EventBus::Publish("wave", Json::Writer::Object().Add("kind", kind).Add("count", added));
    Reply(response, Json::Writer::Object().Add("added", added));
  });

  server.Post("/api/elevator", [this](const httplib::Request& request, httplib::Response& response)
  {
    const auto id = request.get_param_value("id");
    const auto state = request.get_param_value("outOfOrder");

    if (state != "true" && state != "false")
      return ReplyError(response, "outOfOrder must be true or false");

    if (!simulation.SetOutOfOrder(id, state == "true"))
      return ReplyError(response, "unknown elevator '" + id + "' or simulation not running", 404);

    Reply(response, Json::Writer::Object().Add("id", id).Add("outOfOrder", state == "true"));
  });

  server.Post("/api/restart", [this](const httplib::Request& request, httplib::Response& response)
  {
    // The parameters have the names of config.json: they are turned into a small JSON text and read by the
    // same parser and validation of the configuration file. Only plain numbers and known words are accepted.
    static const char* NumericKeys[] = {
      "numberOfElevators", "numberOfFloors", "maxPeople", "averageCallsPerDay", "maxConcurrentCalls",
      "numberOfSimulationDays", "dayDurationSeconds", "dayStartHour", "randomSeed",
      "timeToReachTheNextFloorMs", "doorTransitionTimeMs", "enterAndExitTimeMs" };

    std::string json = "{";
    for (const auto* key : NumericKeys)
    {
      if (!request.has_param(key))
        continue;

      const auto value = request.get_param_value(key);
      double number = 0.0;
      if (!ParseNumber(value, number))
        return ReplyError(response, std::string(key) + " must be a number");

      json += "\"" + std::string(key) + "\": " + value + ", ";
    }

    if (request.has_param("trafficPattern"))
    {
      const auto pattern = request.get_param_value("trafficPattern");
      if (!std::all_of(pattern.begin(), pattern.end(), [](const unsigned char c) { return std::isalpha(c) != 0; }))
        return ReplyError(response, "trafficPattern must be Uniform, Office or Residential");

      json += "\"trafficPattern\": \"" + pattern + "\", ";
    }

    json += "\"type\": \"Random\" }";

    try
    {
      auto settings = Configuration::Get();
      Configuration::ParseJson(json, settings);
      simulation.Restart(settings);
    }
    catch (const std::exception& error)
    {
      return ReplyError(response, error.what());
    }

    finishedNotified = false;
    Reply(response, Json::Writer::Object().Add("settings", SettingsJson()));
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
