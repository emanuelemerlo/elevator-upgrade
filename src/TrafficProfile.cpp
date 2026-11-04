#include "TrafficProfile.h"

#include <algorithm>
#include <cmath>

namespace
{
  using Pattern = Configuration::CallsGenerator::Pattern;
  using Trip = std::pair<Floors::FloorNumber, Floors::FloorNumber>;

  double Peak(const double hour, const double center, const double width, const double weight)
  {
    const auto distance = hour - center;
    return weight * std::exp(-(distance * distance) / (2.0 * width * width));
  }

  double AverageWeight()
  {
    static const double average = []()
    {
      constexpr unsigned int SamplesPerDay = 96U;

      double total = 0.0;
      for (auto sample = 0U; sample < SamplesPerDay; ++sample)
        total += TrafficProfile::At(static_cast<double>(sample) / SamplesPerDay * 24.0).Total();

      return total / SamplesPerDay;
    }();

    return average;
  }

  Floors::FloorNumber RandomUpperFloor(std::default_random_engine& generator)
  {
    std::uniform_int_distribution<Floors::FloorNumber> floor(Floors::BottomFloor + 1U, Floors::TopFloor());
    return floor(generator);
  }

  Trip RandomTrip(std::default_random_engine& generator)
  {
    std::uniform_int_distribution<Floors::FloorNumber> floor(Floors::BottomFloor, Floors::TopFloor());

    Trip trip{ floor(generator), floor(generator) };
    while (trip.first == trip.second)
      trip.second = floor(generator);

    return trip;
  }

  // Trips between the lobby (floor 0) and the upper floors, with a share of trips between upper floors.
  Trip ArrivalTrip(std::default_random_engine& generator) { return { Floors::BottomFloor, RandomUpperFloor(generator) }; }
  Trip DepartureTrip(std::default_random_engine& generator) { return { RandomUpperFloor(generator), Floors::BottomFloor }; }

  bool Chance(std::default_random_engine& generator, const double probability)
  {
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    return uniform(generator) < probability;
  }

  Trip WaveTrip(std::default_random_engine& generator, const TrafficProfile::Wave wave, const Pattern pattern)
  {
    using TrafficProfile::Wave;

    if (pattern == Pattern::Uniform || wave == Wave::Random || Floors::TotalFloors() < 3U)
      return RandomTrip(generator);

    // In the morning an office fills up from the lobby, a residential building empties towards it.
    const auto morningIsArrival = pattern == Pattern::Office;

    switch (wave)
    {
    case Wave::Morning:
      if (Chance(generator, 0.85))
        return morningIsArrival ? ArrivalTrip(generator) : DepartureTrip(generator);
      return RandomTrip(generator);

    case Wave::Evening:
      if (Chance(generator, 0.85))
        return morningIsArrival ? DepartureTrip(generator) : ArrivalTrip(generator);
      return RandomTrip(generator);

    case Wave::Lunch:
    default:
      if (Chance(generator, 0.8))
        return Chance(generator, 0.5) ? ArrivalTrip(generator) : DepartureTrip(generator);
      return RandomTrip(generator);
    }
  }
}

TrafficProfile::Components TrafficProfile::At(const double hour)
{
  // Same daily profile of the original generator: base traffic plus three peaks.
  Components components;
  components.base = 0.25;
  components.morning = Peak(hour, 8.25, 1.25, 1.00);
  components.lunch = Peak(hour, 12.50, 1.00, 0.70);
  components.evening = Peak(hour, 19.00, 1.35, 0.75);
  return components;
}

double TrafficProfile::Intensity(const double hour)
{
  return std::max(0.1, At(hour).Total() / AverageWeight());
}

std::pair<Floors::FloorNumber, Floors::FloorNumber> TrafficProfile::PickTrip(
  std::default_random_engine& generator, const double hour, const Pattern pattern)
{
  if (pattern == Pattern::Uniform)
    return RandomTrip(generator);

  // The component the call belongs to is drawn in proportion to its weight at this hour.
  const auto components = At(hour);
  std::uniform_real_distribution<double> uniform(0.0, components.Total());
  auto draw = uniform(generator);

  if ((draw -= components.morning) < 0.0)
    return WaveTrip(generator, Wave::Morning, pattern);

  if ((draw -= components.lunch) < 0.0)
    return WaveTrip(generator, Wave::Lunch, pattern);

  if ((draw -= components.evening) < 0.0)
    return WaveTrip(generator, Wave::Evening, pattern);

  return RandomTrip(generator);
}

std::pair<Floors::FloorNumber, Floors::FloorNumber> TrafficProfile::PickTrip(
  std::default_random_engine& generator, const Wave wave, const Pattern pattern)
{
  return WaveTrip(generator, wave, pattern);
}

std::vector<double> TrafficProfile::DailyCurve(const unsigned int samples)
{
  std::vector<double> curve;
  curve.reserve(samples);

  for (auto sample = 0U; sample < samples; ++sample)
    curve.push_back(Intensity(static_cast<double>(sample) / samples * 24.0));

  return curve;
}
