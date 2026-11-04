/**********************************************************************************
*        File: TrafficProfile.h
* Description: Traffic of the building along the simulated day.
*              The intensity of the calls follows a daily profile (morning, lunch and evening peaks
*              over a base traffic) and, depending on the pattern, the peaks have a direction:
*              in an office building people arrive from the lobby in the morning and go back
*              down in the evening, in a residential building the other way round.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
**********************************************************************************/

#pragma once

#include "Configuration.h"
#include "Floors.h"

#include <random>
#include <utility>
#include <vector>

namespace TrafficProfile
{
  // Weights of the traffic components at a given hour of the day.
  struct Components
  {
    double base{ 0.0 };
    double morning{ 0.0 };
    double lunch{ 0.0 };
    double evening{ 0.0 };

    double Total() const { return base + morning + lunch + evening; }
  };

  enum class Wave { Morning, Lunch, Evening, Random };

  Components At(double hour);

  /** \brief Traffic intensity at the given hour compared to the daily average (1.0 = average). */
  double Intensity(double hour);

  /** \brief Picks the origin and destination of a call generated at the given hour. */
  std::pair<Floors::FloorNumber, Floors::FloorNumber> PickTrip(
    std::default_random_engine& generator, double hour, Configuration::CallsGenerator::Pattern pattern);

  /** \brief Picks the origin and destination of a call belonging to a given traffic wave. */
  std::pair<Floors::FloorNumber, Floors::FloorNumber> PickTrip(
    std::default_random_engine& generator, Wave wave, Configuration::CallsGenerator::Pattern pattern);

  /** \brief Intensity sampled along the day (samples evenly spaced from 00:00), for charts. */
  std::vector<double> DailyCurve(unsigned int samples);
}
