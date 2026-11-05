/**********************************************************************************
*        File: AssignmentOptimizer.h
* Description: Call assignment based on a route simulation.
*              For every candidate cabin the route it is going to follow is simulated twice,
*              without and with the new call, using the very same stop-selection rule of the
*              real cabins (Floors::FindNextStop). The cost of a candidate is the extra passenger
*              time the new call causes: its own wait + ride, plus the delay it inflicts on
*              the passengers that were already assigned to that cabin.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes: Pure functions (no access to the cabins): they are covered by tests/AssignmentOptimizerTests.cpp.
**********************************************************************************/

#pragma once

#include "Floors.h"

#include <cstddef>
#include <vector>

namespace AssignmentOptimizer
{
  // A passenger as seen by the dispatcher: either still waiting at 'from' or already inside the cabin.
  struct Passenger
  {
    Floors::FloorNumber from{ 0U };
    Floors::FloorNumber to{ 0U };
    bool onboard{ false };
    double waitedMs{ 0.0 };   // how long a waiting passenger has already waited (simulated ms)
  };

  struct CabinState
  {
    Floors::FloorNumber floor{ 0U };
    Direction direction{ Direction::None };   // None = idle
    bool doorsOpen{ false };
    // Floor the cabin is already heading to (it does not re-evaluate its destination before arriving).
    Floors::FloorNumber targetFloor{ Floors::InvalidFloor };
    // Floors where the cabin can stop (zoned buildings): it passes the others express, in Timing::expressFloorMs.
    // nullptr or empty: every floor is served.
    const std::vector<bool>* servedFloors{ nullptr };
  };

  // Durations in simulated milliseconds.
  struct Timing
  {
    double floorMs{ 0.0 };   // time to move by one floor
    double doorMs{ 0.0 };    // time of one door transition (opening or closing)
    double dwellMs{ 0.0 };   // time spent at a stop for people to enter and exit
    double expressFloorMs{ 0.0 };   // time to pass a floor the cabin does not serve (0 = floorMs)
  };

  /** \brief Travel time from one floor to another, floor by floor, as the cabin moves (see Elevator::TimeToReach). */
  double TravelMs(const CabinState& cabin, Floors::FloorNumber from, Floors::FloorNumber to, const Timing& timing);

  struct Weights
  {
    double wait{ 2.0 };        // a second spent waiting on the floor hurts more than a second spent inside the cabin
    double ride{ 1.0 };
    double agingMs{ 16000.0 }; // waiting time after which the delay inflicted to a passenger counts one more unit...
    double maxAging{ 3.0 };    // ...up to this many extra units (anti-starvation: long waits are protected)
  };

  struct RouteResult
  {
    bool completed{ false };            // every passenger has been delivered by the simulated route
    std::size_t peakLoad{ 0U };         // maximum number of people inside the cabin during the route
    bool reversedBeforeExtra{ false };  // the cabin had to change sweep direction before picking up the extra call
    double cost{ 0.0 };                 // weighted passenger time of the whole route
    double extraPickupMs{ 0.0 };
    double extraDropoffMs{ 0.0 };
  };

  // Simulates the route of one cabin. 'extra' (optional) is the call being evaluated.
  RouteResult SimulateRoute(
    const CabinState& cabin,
    const std::vector<Passenger>& committed,
    const Passenger* extra,
    const Timing& timing,
    const Weights& weights);

  struct Evaluation
  {
    bool feasible{ false };        // the cabin can take the call without exceeding its capacity
    bool onTheWay{ false };        // the call is picked up without the cabin reversing its sweep first
    double marginalCostMs{ 0.0 };  // extra weighted passenger time caused by the call
    double pickupEtaMs{ 0.0 };
    double dropoffEtaMs{ 0.0 };
    std::size_t peakLoad{ 0U };
  };

  Evaluation EvaluateAssignment(
    const CabinState& cabin,
    const std::vector<Passenger>& committed,
    const Passenger& newCall,
    std::size_t capacity,
    const Timing& timing,
    const Weights& weights);
}
