/**********************************************************************************
*        File: Scenario.h
* Description: The passengers of a run, decided before the run starts.
*              With the same seed and the same traffic settings the list is identical, so two runs
*              that differ only in the strategy, the parking or the cabins face exactly the same
*              people at exactly the same simulated times: their results can be compared.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
**********************************************************************************/

#pragma once

#include "Configuration.h"
#include "Floors.h"

#include <string>
#include <vector>

struct ScheduledCall
{
  long long atMs{ 0 };            // simulated time since the start of the run
  Floors::FloorNumber from{ 0U };
  Floors::FloorNumber to{ 0U };
  long long patienceMs{ 0 };      // 0 = the passenger never takes the stairs
};

namespace Scenario
{
  /** \brief The configured seed, or a new random one when it is 0 (the seed used is recorded with the run). */
  unsigned int ResolveSeed(unsigned int configuredSeed);

  /** \brief Every call of the run, ordered by time. Uses the current configuration of the building and traffic. */
  std::vector<ScheduledCall> Build(unsigned int seed);

  /** \brief Patience of a passenger who is not part of the scenario (e.g. a traffic wave), from the given random value 0-1. */
  long long PatienceFor(double uniform01);

  /**
   * \brief Identifier of the scenario: equal for runs with the same passengers (seed, floors, traffic settings),
   *        whatever the cabins and the dispatch strategy are.
   */
  std::string Key(unsigned int seed);
}
