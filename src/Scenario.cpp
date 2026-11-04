#include "Scenario.h"

#include "SimulationClock.h"
#include "TrafficProfile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>

namespace
{
  // Generator start delay of the original program, kept so that the first passenger does not appear at time 0.
  constexpr long long FirstCallMs = 2000;

  std::uint64_t Fnv1a(const std::string& text)
  {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto character : text)
    {
      hash ^= static_cast<unsigned char>(character);
      hash *= 1099511628211ULL;
    }
    return hash;
  }
}

unsigned int Scenario::ResolveSeed(const unsigned int configuredSeed)
{
  if (configuredSeed != 0U)
    return configuredSeed;

  // Small enough to be typed again in the interface.
  std::random_device device;
  const auto seed = (device() ^ static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count())) % 999999U;
  return seed + 1U;
}

long long Scenario::PatienceFor(const double uniform01)
{
  const auto meanMs = Configuration::Get().stairsPatienceSeconds * 1000.0;
  if (meanMs <= 0.0)
    return 0;

  return static_cast<long long>(meanMs * (0.5 + uniform01));
}

std::vector<ScheduledCall> Scenario::Build(const unsigned int seed)
{
  const auto& settings = Configuration::Get();

  // Two independent streams: changing the patience does not change who travels and when.
  std::default_random_engine tripGenerator(seed);
  std::default_random_engine patienceGenerator(seed ^ 0x9E3779B9U);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);

  const auto startOfDayMs = static_cast<long long>(settings.dayStartHour * static_cast<double>(SimulationClock::HourMs));
  const auto endMs = SimulationClock::DayMs * static_cast<long long>(settings.numberOfSimulationDays) - startOfDayMs;
  const auto averageDelayMs = static_cast<double>(SimulationClock::DayMs) / static_cast<double>(settings.averageCallsPerDay);
  const auto minDelayMs = static_cast<double>(std::max<long long>(1, settings.minDelayBetweenCalls.count()));
  const auto maxDelayMs = std::max(minDelayMs, static_cast<double>(settings.maxDelayBetweenCalls.count()));

  std::vector<ScheduledCall> calls;
  calls.reserve(std::min<std::size_t>(settings.numberOfCalls, settings.averageCallsPerDay * settings.numberOfSimulationDays + 16U));

  auto atMs = FirstCallMs;

  while (atMs < endMs && calls.size() < settings.numberOfCalls)
  {
    const auto hour = std::fmod(static_cast<double>(startOfDayMs + atMs) / static_cast<double>(SimulationClock::HourMs), 24.0);
    const auto trip = TrafficProfile::PickTrip(tripGenerator, hour, settings.trafficPattern);

    ScheduledCall call;
    call.atMs = atMs;
    call.from = trip.first;
    call.to = trip.second;
    call.patienceMs = PatienceFor(uniform(patienceGenerator));
    calls.push_back(call);

    // Arrivals are random (exponential delays, i.e. a Poisson process) around the average delay of this hour.
    std::exponential_distribution<double> arrival(TrafficProfile::Intensity(hour) / averageDelayMs);
    atMs += static_cast<long long>(std::min(maxDelayMs, std::max(minDelayMs, arrival(tripGenerator))));
  }

  return calls;
}

std::string Scenario::Key(const unsigned int seed)
{
  const auto& settings = Configuration::Get();

  std::ostringstream text;
  text << seed << '|' << settings.numberOfFloors << '|' << Configuration::PatternName(settings.trafficPattern) << '|'
       << settings.averageCallsPerDay << '|' << settings.numberOfSimulationDays << '|' << settings.dayStartHour << '|'
       << settings.minDelayBetweenCalls.count() << '|' << settings.maxDelayBetweenCalls.count() << '|'
       << settings.numberOfCalls << '|' << settings.stairsPatienceSeconds;

  std::ostringstream key;
  key << std::hex << std::setw(6) << std::setfill('0') << (Fnv1a(text.str()) & 0xFFFFFFU);
  return key.str();
}
