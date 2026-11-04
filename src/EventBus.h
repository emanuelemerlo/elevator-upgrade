/**********************************************************************************
*        File: EventBus.h
* Description: Stream of the simulation events (calls, assignments, boardings, faults...).
*              Events are kept in a bounded buffer with an increasing sequence number, so that
*              a viewer (the web interface) can ask for "everything after N" at its own pace.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
**********************************************************************************/

#pragma once

#include "Json.h"

#include <cstdint>
#include <string>
#include <vector>

struct SimulationEvent
{
  std::uint64_t sequence{ 0U };
  long long simulatedMs{ 0 };   // simulated time since the start of the run
  std::string building;          // name of the building of the thread that published it ("" = default building)
  std::string type;
  std::string data;             // JSON object with the details of the event
};

namespace EventBus
{
  void Publish(const std::string& type, const Json::Writer& data);

  /** \brief Events with a sequence number greater than 'sequence', oldest first, at most 'maxEvents'. */
  std::vector<SimulationEvent> Since(std::uint64_t sequence, std::size_t maxEvents = 500U);

  std::uint64_t LastSequence();

  /** \brief Forgets the stored events (the sequence keeps growing, so a viewer never mixes two runs). */
  void Clear();

  /** \brief Serializes one event as a JSON object. */
  std::string ToJson(const SimulationEvent& event);
}
