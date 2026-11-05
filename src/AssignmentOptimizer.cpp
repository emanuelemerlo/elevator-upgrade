/**********************************************************************************
*        File: AssignmentOptimizer.cpp
* Description: Call assignment based on a route simulation.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#include "AssignmentOptimizer.h"

#include <algorithm>
#include <cmath>

namespace AssignmentOptimizer
{
  namespace
  {
    enum class State { Waiting, Onboard, Done };

    struct Item
    {
      Floors::FloorNumber from;
      Floors::FloorNumber to;
      Direction direction;
      State state;
      bool startedOnboard;
      double waitedMs;
      double pickupMs;
      double dropoffMs;
      bool isExtra;
    };

    Direction DirectionOf(const Floors::FloorNumber from, const Floors::FloorNumber to)
    {
      return from < to ? Direction::Up : Direction::Down;
    }

    void Flag(FloorStops& stops, const Floors::FloorNumber floor, const Direction direction)
    {
      auto& stop = stops[floor];
      stop.second = stop.first && stop.second != direction ? Direction::Both : direction;
      stop.first = true;
    }

    Item MakeItem(const Passenger& passenger, const bool isExtra)
    {
      return Item{
        passenger.from,
        passenger.to,
        DirectionOf(passenger.from, passenger.to),
        passenger.onboard ? State::Onboard : State::Waiting,
        passenger.onboard,
        passenger.waitedMs,
        0.0,
        0.0,
        isExtra };
    }

    double Distance(const Floors::FloorNumber a, const Floors::FloorNumber b)
    {
      return a > b ? static_cast<double>(a - b) : static_cast<double>(b - a);
    }
  }

  double TravelMs(const CabinState& cabin, const Floors::FloorNumber from, const Floors::FloorNumber to, const Timing& timing)
  {
    const auto* served = cabin.servedFloors;
    if (served == nullptr || served->empty() || timing.expressFloorMs <= 0.0)
      return Distance(from, to) * timing.floorMs;

    // Each step costs the time of the floor it reaches: a normal floor if the cabin serves it, an express one otherwise.
    double total = 0.0;
    for (auto floor = from; floor != to;)
    {
      floor = to > floor ? floor + 1U : floor - 1U;
      const auto isServed = floor < served->size() && (*served)[floor];
      total += isServed ? timing.floorMs : timing.expressFloorMs;
    }
    return total;
  }

  RouteResult SimulateRoute(
    const CabinState& cabin,
    const std::vector<Passenger>& committed,
    const Passenger* extra,
    const Timing& timing,
    const Weights& weights)
  {
    RouteResult result;

    std::vector<Item> items;
    items.reserve(committed.size() + 1U);

    for (const auto& passenger : committed)
      items.push_back(MakeItem(passenger, false));

    if (extra != nullptr)
      items.push_back(MakeItem(*extra, true));

    std::size_t onboardCount = 0U;
    std::size_t remaining = 0U;

    for (const auto& item : items)
    {
      if (item.state == State::Onboard)
        ++onboardCount;

      ++remaining;
    }

    result.peakLoad = onboardCount;

    // An idle cabin starts sweeping in the direction of the first call it receives (see Elevator::AnswerToCall).
    auto direction = cabin.direction;
    if (direction == Direction::None)
    {
      const auto first = std::find_if(items.begin(), items.end(),
        [](const Item& item) { return item.state == State::Waiting; });

      direction = first != items.end() ? first->direction : Direction::Up;
    }

    auto floor = cabin.floor;
    auto doorsOpen = cabin.doorsOpen;
    double now = 0.0;

    // Doors open means the cabin is in the middle of a stop: the people of this floor have already boarded
    // and exited, so the stop in progress must end (dwell time) before anybody else can get on.
    if (doorsOpen)
      now += timing.dwellMs;

    bool extraBoarded = extra == nullptr;

    // Every iteration serves at least one stop; the limit only protects against inconsistent input.
    const auto maxSteps = 4U * items.size() + 8U;

    for (std::size_t step = 0U; step < maxSteps && remaining > 0U; ++step)
    {
      FloorStops stops(Floors::TotalFloors(), Stop(false, Direction::None));

      for (const auto& item : items)
      {
        if (item.state == State::Waiting)
          Flag(stops, item.from, item.direction);
        else if (item.state == State::Onboard)
          Flag(stops, item.to, item.direction);
      }

      const auto directionBefore = direction;
      Floors::FloorNumber next = Floors::InvalidFloor;

      if (step == 0U && Floors::IsValid(cabin.targetFloor))
      {
        // Already decided by the cabin: new calls cannot change it, except that while travelling the cabin
        // stops at the nearest stop flagged for its own direction before the target (see Elevator::Move).
        next = cabin.targetFloor;

        if (next != floor && direction == (next > floor ? Direction::Up : Direction::Down))
        {
          const auto upwards = next > floor;
          for (auto candidate = upwards ? floor + 1U : floor - 1U; candidate != next; candidate = upwards ? candidate + 1U : candidate - 1U)
          {
            const auto& stop = stops[candidate];
            if (stop.first && (stop.second == direction || stop.second == Direction::Both))
            {
              next = candidate;
              break;
            }
          }
        }
      }
      else
        next = Floors::FindNextStop(stops, floor, direction);

      if (!Floors::IsValid(next))
        break;

      if (direction != directionBefore && !extraBoarded)
        result.reversedBeforeExtra = true;

      if (next != floor)
      {
        if (doorsOpen)
        {
          now += timing.doorMs;   // the doors close before leaving
          doorsOpen = false;
        }

        now += TravelMs(cabin, floor, next, timing);
        floor = next;
      }

      if (!doorsOpen)
      {
        now += timing.doorMs;     // the doors open on arrival
        doorsOpen = true;
      }

      for (auto& item : items)
      {
        if (item.state == State::Onboard && item.to == floor)
        {
          item.state = State::Done;
          item.dropoffMs = now;
          --onboardCount;
          --remaining;
        }
      }

      for (auto& item : items)
      {
        if (item.state == State::Waiting && item.from == floor && item.direction == direction)
        {
          item.state = State::Onboard;
          item.pickupMs = now;
          ++onboardCount;
          result.peakLoad = std::max(result.peakLoad, onboardCount);

          if (item.isExtra)
            extraBoarded = true;
        }
      }

      now += timing.dwellMs;
    }

    result.completed = remaining == 0U;

    // Weighted passenger time: waiting on the floor, then riding. Passengers that have already waited a
    // long time weigh more, so the dispatcher avoids postponing them further.
    for (const auto& item : items)
    {
      if (item.state != State::Done)
        continue; // not served (inconsistent input): the route is reported as not completed

      if (item.startedOnboard)
      {
        result.cost += weights.ride * item.dropoffMs;
      }
      else
      {
        const auto aging = 1.0 + std::min(weights.maxAging, item.waitedMs / weights.agingMs);
        result.cost += aging * (weights.wait * item.pickupMs + weights.ride * (item.dropoffMs - item.pickupMs));
      }

      if (item.isExtra)
      {
        result.extraPickupMs = item.pickupMs;
        result.extraDropoffMs = item.dropoffMs;
      }
    }

    return result;
  }

  Evaluation EvaluateAssignment(
    const CabinState& cabin,
    const std::vector<Passenger>& committed,
    const Passenger& newCall,
    const std::size_t capacity,
    const Timing& timing,
    const Weights& weights)
  {
    Evaluation evaluation;

    const auto without = SimulateRoute(cabin, committed, nullptr, timing, weights);
    const auto with = SimulateRoute(cabin, committed, &newCall, timing, weights);

    evaluation.peakLoad = with.peakLoad;

    // The call fits only if the cabin never carries more people than its capacity. A cabin whose planned
    // route already exceeds it (only possible if reality drifted from the prediction) takes no more calls.
    evaluation.feasible = with.completed && with.peakLoad <= capacity;
    evaluation.onTheWay = evaluation.feasible && !with.reversedBeforeExtra;
    evaluation.marginalCostMs = with.cost - without.cost;
    evaluation.pickupEtaMs = with.extraPickupMs;
    evaluation.dropoffEtaMs = with.extraDropoffMs;

    return evaluation;
  }
}
