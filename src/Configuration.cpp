#include "Configuration.h"
#include "SimulationClock.h"
#include "BuildingContext.h"
#include "Zoning.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <limits>

namespace
{
  // The settings belong to the building of the calling thread (see BuildingContext).
  Configuration::Settings& Settings() { return Context::Current().settings; }

  std::string ReadFile(const std::string& path)
  {
    std::ifstream file(path);
    if (!file.is_open())
      return {};

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  std::string ToLower(std::string value)
  {
    std::transform(value.begin(), value.end(), value.begin(),
      [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
  }

  bool FindUnsigned(const std::string& json, const std::string& key, unsigned int& value)
  {
    // A leading '-' is matched on purpose: a negative value must be reported as an
    // error instead of being silently ignored (and replaced by the default).
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?\\d+)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern))
      return false;

    const auto text = match[1].str();
    if (text.front() == '-')
      throw std::invalid_argument("Invalid value for '" + key + "': it must not be negative");

    const auto parsed = std::stoull(text);
    if (parsed > std::numeric_limits<unsigned int>::max())
      throw std::invalid_argument("Invalid value for '" + key + "': out of range");

    value = static_cast<unsigned int>(parsed);
    return true;
  }

  bool FindDouble(const std::string& json, const std::string& key, double& value)
  {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?\\d+(?:\\.\\d+)?)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern))
      return false;

    value = std::stod(match[1].str());
    return true;
  }

  bool FindBool(const std::string& json, const std::string& key, bool& value)
  {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern))
      return false;

    value = match[1].str() == "true";
    return true;
  }

  bool FindString(const std::string& json, const std::string& key, std::string& value)
  {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(json, match, pattern))
      return false;

    value = match[1].str();
    return true;
  }

  ILog::TraceLevel ParseTraceLevel(const std::string& value)
  {
    const auto normalized = ToLower(value);
    if (normalized == "debug")
      return ILog::TraceLevel::Debug;
    if (normalized == "verbose")
      return ILog::TraceLevel::Verbose;
    if (normalized == "info")
      return ILog::TraceLevel::Info;
    if (normalized == "warning")
      return ILog::TraceLevel::Warning;
    if (normalized == "error")
      return ILog::TraceLevel::Error;

    throw std::invalid_argument("Invalid log.traceLevel value: " + value);
  }

  Configuration::CallsGenerator::Type ParseGeneratorType(const std::string& value)
  {
    const auto normalized = ToLower(value);
    if (normalized == "random")
      return Configuration::CallsGenerator::Type::Random;
    if (normalized == "fixed")
      return Configuration::CallsGenerator::Type::Fixed;

    throw std::invalid_argument("Invalid callsGenerator.type value: " + value);
  }

  Configuration::CallsGenerator::Pattern ParseTrafficPattern(const std::string& value)
  {
    const auto normalized = ToLower(value);
    if (normalized == "uniform")
      return Configuration::CallsGenerator::Pattern::Uniform;
    if (normalized == "office")
      return Configuration::CallsGenerator::Pattern::Office;
    if (normalized == "residential")
      return Configuration::CallsGenerator::Pattern::Residential;

    throw std::invalid_argument("Invalid callsGenerator.trafficPattern value: " + value + " (Uniform, Office or Residential)");
  }

  Configuration::Dispatch::Strategy ParseStrategy(const std::string& value)
  {
    const auto normalized = ToLower(value);
    if (normalized == "optimized")
      return Configuration::Dispatch::Strategy::Optimized;
    if (normalized == "nearest")
      return Configuration::Dispatch::Strategy::Nearest;
    if (normalized == "collective")
      return Configuration::Dispatch::Strategy::Collective;

    throw std::invalid_argument("Invalid dispatch.dispatchStrategy value: " + value + " (Optimized, Nearest or Collective)");
  }

  Configuration::Dispatch::Parking ParseParking(const std::string& value)
  {
    const auto normalized = ToLower(value);
    if (normalized == "stay")
      return Configuration::Dispatch::Parking::Stay;
    if (normalized == "lobby")
      return Configuration::Dispatch::Parking::Lobby;
    if (normalized == "spread")
      return Configuration::Dispatch::Parking::Spread;

    throw std::invalid_argument("Invalid dispatch.idleParking value: " + value + " (Stay, Lobby or Spread)");
  }

  ILog::LogType ParseLogType(const std::string& value)
  {
    const auto normalized = ToLower(value);
    if (normalized == "screen")
      return ILog::LogType::Screen;
    if (normalized == "file")
      return ILog::LogType::File;
    if (normalized == "screenandfile" || normalized == "screen_and_file" || normalized == "both")
      return ILog::LogType::ScreenAndFile;

    throw std::invalid_argument("Invalid log.defaultLogType value: " + value);
  }

}

void Configuration::Validate(const Settings& settings)
{
    if (settings.numberOfElevators == 0)
      throw std::invalid_argument("building.numberOfElevators must be greater than 0");

    if (settings.numberOfElevators > 26 && settings.zones.empty())
      throw std::invalid_argument("building.numberOfElevators must be at most 26 (system generates IDs A-Z)");

    if (!(settings.expressFloorFactor > 0.0) || settings.expressFloorFactor > 1.0)
      throw std::invalid_argument("zoning.expressFloorFactor must be greater than 0 and at most 1");

    if (settings.numberOfFloors < 2)
      throw std::invalid_argument("building.numberOfFloors must be at least 2");

    if (settings.minDelayBetweenCalls > settings.maxDelayBetweenCalls)
      throw std::invalid_argument("callsGenerator.minDelayBetweenCallsMs must be <= maxDelayBetweenCallsMs");

    if (settings.numberOfCalls == 0)
      throw std::invalid_argument("callsGenerator.numberOfCalls must be greater than 0");

    if (settings.averageCallsPerDay == 0)
      throw std::invalid_argument("callsGenerator.averageCallsPerDay must be greater than 0");

    if (settings.maxConcurrentCalls == 0)
      throw std::invalid_argument("callsGenerator.maxConcurrentCalls must be greater than 0");

    if (settings.numberOfSimulationDays == 0)
      throw std::invalid_argument("callsGenerator.numberOfSimulationDays must be greater than 0");


    if (settings.timeToReachTheNextFloor.count() == 0)
      throw std::invalid_argument("elevator.timeToReachTheNextFloorMs must be greater than 0");

    const auto minDaySeconds = static_cast<double>(SimulationClock::DayMs) / 1000.0 / SimulationClock::MaxSpeed;
    const auto maxDaySeconds = static_cast<double>(SimulationClock::DayMs) / 1000.0 / SimulationClock::MinSpeed;
    if (!(settings.dayDurationSeconds >= minDaySeconds) || settings.dayDurationSeconds > maxDaySeconds)
      throw std::invalid_argument("simulation.dayDurationSeconds must be between " +
        std::to_string(static_cast<int>(std::ceil(minDaySeconds))) + " and " + std::to_string(static_cast<int>(maxDaySeconds)) +
        " (real seconds for 24 simulated hours)");

    if (!(settings.stairsPatienceSeconds >= 0.0) || settings.stairsPatienceSeconds > 86400.0)
      throw std::invalid_argument("callsGenerator.stairsPatienceSeconds must be between 0 (never) and 86400");

    if (!(settings.standbyPowerW >= 0.0) || settings.standbyPowerW > 100000.0)
      throw std::invalid_argument("energy.standbyPowerW must be between 0 and 100000 watts");

    if (!(settings.motorEfficiency > 0.0) || settings.motorEfficiency > 1.0)
      throw std::invalid_argument("energy.motorEfficiency must be greater than 0 and at most 1");

    if (!(settings.dayStartHour >= 0.0) || settings.dayStartHour >= 24.0)
      throw std::invalid_argument("simulation.dayStartHour must be between 0 and 24 (excluded)");

    if (settings.numberOfFloors > 60)
      throw std::invalid_argument("building.numberOfFloors must be at most 60");

    if (settings.maxPeoplePerElevator == 0)
      throw std::invalid_argument("elevator.maxPeople must be greater than 0");

    // The zones must cover the building exactly (throws with the reason).
    Zoning::Groups(settings);
}

bool Configuration::LoadFromFile(const std::string& path)
{
  const auto json = ReadFile(path);
  if (json.empty())
    throw std::runtime_error("Configuration file '" + path + "' not found or empty");

  Settings settings = ::Settings();
  ParseJson(json, settings);
  ::Settings() = settings;
  return true;
}

void Configuration::ParseJson(const std::string& json, Settings& settings)
{
  unsigned int value = 0;
  std::string text;

  // The simulated time line changed in this fork: refuse the old keys instead of silently ignoring them.
  if (json.find("\"simulationDayDurationMs\"") != std::string::npos || json.find("\"timeScale\"") != std::string::npos)
    throw std::invalid_argument(
      "simulationDayDurationMs and timeScale have been replaced by dayDurationSeconds "
      "(real seconds for 24 simulated hours, e.g. 1440 = one day in 24 minutes)");

  if (FindUnsigned(json, "numberOfElevators", value))
    settings.numberOfElevators = value;

  if (FindUnsigned(json, "numberOfFloors", value))
    settings.numberOfFloors = value;

  if (FindString(json, "type", text))
    settings.generatorType = ParseGeneratorType(text);

  if (FindUnsigned(json, "numberOfCalls", value))
    settings.numberOfCalls = value;

  if (FindUnsigned(json, "averageCallsPerDay", value))
    settings.averageCallsPerDay = value;

  if (FindUnsigned(json, "maxConcurrentCalls", value))
    settings.maxConcurrentCalls = value;

  if (FindUnsigned(json, "numberOfSimulationDays", value))
    settings.numberOfSimulationDays = value;

  if (FindString(json, "trafficPattern", text))
    settings.trafficPattern = ParseTrafficPattern(text);

  if (FindString(json, "dispatchStrategy", text))
    settings.dispatchStrategy = ParseStrategy(text);

  if (FindString(json, "idleParking", text))
    settings.idleParking = ParseParking(text);

  if (FindString(json, "zones", text))
    settings.zones = text;

  if (FindString(json, "skyLobbies", text))
    settings.skyLobbies = text;

  FindDouble(json, "expressFloorFactor", settings.expressFloorFactor);

  if (FindString(json, "runsFilePath", text))
    settings.runsFilePath = text;

  if (FindUnsigned(json, "minDelayBetweenCallsMs", value))
    settings.minDelayBetweenCalls = std::chrono::milliseconds(value);

  if (FindUnsigned(json, "maxDelayBetweenCallsMs", value))
    settings.maxDelayBetweenCalls = std::chrono::milliseconds(value);

  if (FindUnsigned(json, "timeToReachTheNextFloorMs", value))
    settings.timeToReachTheNextFloor = std::chrono::milliseconds(value);

  if (FindUnsigned(json, "enterAndExitTimeMs", value))
    settings.enterAndExitTime = std::chrono::milliseconds(value);

  if (FindUnsigned(json, "doorTransitionTimeMs", value))
    settings.doorTransitionTime = std::chrono::milliseconds(value);

  if (FindUnsigned(json, "maxPeople", value))
    settings.maxPeoplePerElevator = value;

  FindDouble(json, "dayDurationSeconds", settings.dayDurationSeconds);
  FindDouble(json, "stairsPatienceSeconds", settings.stairsPatienceSeconds);
  FindDouble(json, "standbyPowerW", settings.standbyPowerW);
  FindDouble(json, "motorEfficiency", settings.motorEfficiency);
  FindDouble(json, "dayStartHour", settings.dayStartHour);

  if (FindUnsigned(json, "randomSeed", value))
    settings.randomSeed = value;

  FindBool(json, "autoStop", settings.autoStop);

  if (FindString(json, "traceLevel", text))
    settings.traceLevel = ParseTraceLevel(text);

  if (FindString(json, "defaultLogType", text))
    settings.defaultLogType = ParseLogType(text);

  if (FindString(json, "filePath", text))
    settings.logFilePath = text;

  Validate(settings);
}

void Configuration::Apply(const Settings& settings)
{
  Validate(settings);
  ::Settings() = settings;
}

const Configuration::Settings& Configuration::Get()
{
  return ::Settings();
}

unsigned int Configuration::Building::NumberOfElevators()
{
  // With zones the cabins are the ones of the groups (and of the shuttles).
  return Get().zones.empty() ? Get().numberOfElevators : Zoning::TotalCabins(Get());
}

unsigned int Configuration::Building::NumberOfFloors()
{
  return Get().numberOfFloors;
}

std::chrono::milliseconds Configuration::Elevator::TimeToReachTheNextFloor()
{
  return Get().timeToReachTheNextFloor;
}

std::chrono::milliseconds Configuration::Elevator::TimeToPassAnExpressFloor()
{
  const auto floorMs = static_cast<double>(Get().timeToReachTheNextFloor.count());
  return std::chrono::milliseconds(std::max(1LL, static_cast<long long>(floorMs * Get().expressFloorFactor)));
}

std::chrono::milliseconds Configuration::Elevator::EnterAndExitTime()
{
  return Get().enterAndExitTime;
}

std::chrono::milliseconds Configuration::Elevator::DoorTransitionTime()
{
  return Get().doorTransitionTime;
}

unsigned int Configuration::Elevator::MaxPeople()
{
  return Get().maxPeoplePerElevator;
}

Configuration::CallsGenerator::Type Configuration::CallsGenerator::GeneratorType()
{
  return Get().generatorType;
}

unsigned int Configuration::CallsGenerator::NumberOfCalls()
{
  return Get().numberOfCalls;
}

unsigned int Configuration::CallsGenerator::AverageCallsPerDay()
{
  return Get().averageCallsPerDay;
}

unsigned int Configuration::CallsGenerator::MaxConcurrentCalls()
{
  return Get().maxConcurrentCalls;
}

unsigned int Configuration::CallsGenerator::NumberOfSimulationDays()
{
  return Get().numberOfSimulationDays;
}

Configuration::CallsGenerator::Pattern Configuration::CallsGenerator::TrafficPattern()
{
  return Get().trafficPattern;
}

long long Configuration::CallsGenerator::MinDelayBetweenCalls()
{
  return Get().minDelayBetweenCalls.count();
}

long long Configuration::CallsGenerator::MaxDelayBetweenCalls()
{
  return Get().maxDelayBetweenCalls.count();
}

ILog::TraceLevel Configuration::Log::TraceLevel()
{
  return Get().traceLevel;
}

ILog::LogType Configuration::Log::DefaultLogType()
{
  return Get().defaultLogType;
}

std::string Configuration::Log::FilePath()
{
  return Get().logFilePath;
}

bool Configuration::Log::WriteToScreen()
{
  return Get().defaultLogType == ILog::LogType::Screen ||
    Get().defaultLogType == ILog::LogType::ScreenAndFile;
}

bool Configuration::Log::WriteToFile()
{
  return Get().defaultLogType == ILog::LogType::File ||
    Get().defaultLogType == ILog::LogType::ScreenAndFile;
}

void Configuration::Log::Override(const ILog::LogType logType, const ILog::TraceLevel minimumLevel)
{
  ::Settings().defaultLogType = logType;
  ::Settings().traceLevel = std::max(::Settings().traceLevel, minimumLevel);
}

Configuration::Dispatch::Strategy Configuration::Dispatch::DispatchStrategy()
{
  return Get().dispatchStrategy;
}

Configuration::Dispatch::Parking Configuration::Dispatch::IdleParking()
{
  return Get().idleParking;
}

const char* Configuration::Dispatch::StrategyName(const Strategy strategy)
{
  switch (strategy)
  {
  case Strategy::Nearest: return "Nearest";
  case Strategy::Collective: return "Collective";
  case Strategy::Optimized:
  default: return "Optimized";
  }
}

const char* Configuration::Dispatch::ParkingName(const Parking parking)
{
  switch (parking)
  {
  case Parking::Lobby: return "Lobby";
  case Parking::Spread: return "Spread";
  case Parking::Stay:
  default: return "Stay";
  }
}

const char* Configuration::PatternName(const CallsGenerator::Pattern pattern)
{
  switch (pattern)
  {
  case CallsGenerator::Pattern::Uniform: return "Uniform";
  case CallsGenerator::Pattern::Residential: return "Residential";
  case CallsGenerator::Pattern::Office:
  default: return "Office";
  }
}

void Configuration::Simulation::StartClock()
{
  SimulationClock::Reset(InitialSpeed(), static_cast<long long>(DayStartHour() * static_cast<double>(SimulationClock::HourMs)));
}

double Configuration::Simulation::InitialSpeed()
{
  return static_cast<double>(SimulationClock::DayMs) / (Get().dayDurationSeconds * 1000.0);
}

double Configuration::Simulation::DayStartHour()
{
  return Get().dayStartHour;
}

unsigned int Configuration::Simulation::RandomSeed()
{
  return Get().randomSeed;
}

bool Configuration::Simulation::AutoStop()
{
  return Get().autoStop;
}

bool Configuration::Simulation::SleepFor(const std::chrono::milliseconds simulatedDuration)
{
  return SimulationClock::SleepFor(simulatedDuration);
}

long long Configuration::Simulation::ElapsedMs()
{
  return SimulationClock::Now();
}

double Configuration::Simulation::CurrentHour()
{
  const auto timeOfDayMs = SimulationClock::TimeOfDayMs() % SimulationClock::DayMs;
  return static_cast<double>(timeOfDayMs) / static_cast<double>(SimulationClock::HourMs);
}

unsigned int Configuration::Simulation::CurrentDay()
{
  const auto elapsedDays = static_cast<unsigned int>(SimulationClock::TimeOfDayMs() / SimulationClock::DayMs);
  return std::min(elapsedDays + 1U, CallsGenerator::NumberOfSimulationDays());
}

bool Configuration::Simulation::Completed()
{
  const auto lastMidnightMs = SimulationClock::DayMs * static_cast<long long>(CallsGenerator::NumberOfSimulationDays());
  return SimulationClock::TimeOfDayMs() >= lastMidnightMs;
}

std::string Configuration::Simulation::CurrentDayTimeLabel()
{
  const auto completed = Completed();
  // Once the last day is over the clock is shown at its end (24:00) instead of wrapping to the next day.
  const auto wholeMinutes = completed ? 24U * 60U : static_cast<unsigned int>(CurrentHour() * 60.0) % (24U * 60U);
  const auto hours = wholeMinutes / 60U;
  const auto minutes = wholeMinutes % 60U;

  std::stringstream label;
  label << "Day " << CurrentDay() << "/" << CallsGenerator::NumberOfSimulationDays()
        << " " << std::setw(2) << std::setfill('0') << hours
        << ":" << std::setw(2) << std::setfill('0') << minutes
        << std::setfill(' ');
  return label.str();
}
