/**********************************************************************************
* Tests of the route simulation used by the dispatcher and of the floors stop logic.
* No framework: every check prints a line and the process exits with a failure code
* if at least one of them fails. Run with: make test
**********************************************************************************/

#include "AssignmentOptimizer.h"
#include "Configuration.h"
#include "Floors.h"
#include "Call.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>

namespace
{
  int g_failures = 0;
  int g_checks = 0;

  void Check(const bool condition, const std::string& description)
  {
    ++g_checks;
    if (!condition)
      ++g_failures;

    std::cout << (condition ? "  [ok]   " : "  [FAIL] ") << description << std::endl;
  }

  bool Near(const double a, const double b) { return std::fabs(a - b) < 0.001; }

  using namespace AssignmentOptimizer;

  const Timing kTiming{ 200.0, 1000.0, 20.0 };
  const Weights kWeights;

  Passenger Waiting(const unsigned int from, const unsigned int to, const double waitedMs = 0.0) { return { from, to, false, waitedMs }; }
  Passenger Onboard(const unsigned int from, const unsigned int to) { return { from, to, true, 0.0 }; }

  void TimingOfAnIdleCabin()
  {
    std::cout << "Idle cabin" << std::endl;

    const CabinState idleAtGround{ 0U, Direction::None, false };

    const Passenger firstCall = Waiting(3, 7);
    auto route = SimulateRoute(idleAtGround, {}, &firstCall, kTiming, kWeights);
    Check(route.completed, "the call is served");
    // 3 floors * 200 + door opening 1000
    Check(Near(route.extraPickupMs, 1600.0), "pickup ETA = travel + door opening (1600 ms)");
    // + dwell 20 + door closing 1000 + 4 floors * 200 + door opening 1000
    Check(Near(route.extraDropoffMs, 4420.0), "drop-off ETA = pickup + dwell + close + travel + open (4420 ms)");
    Check(!route.reversedBeforeExtra, "an idle cabin does not need to reverse");

    const Passenger sameFloor = Waiting(0, 4);
    route = SimulateRoute(idleAtGround, {}, &sameFloor, kTiming, kWeights);
    Check(Near(route.extraPickupMs, 1000.0), "call on the cabin floor with closed doors: only the door opening");

    const CabinState doorsOpen{ 0U, Direction::None, true };
    route = SimulateRoute(doorsOpen, {}, &sameFloor, kTiming, kWeights);
    Check(Near(route.extraPickupMs, 20.0), "call on the cabin floor with open doors: boards after the stop in progress ends (dwell)");
  }

  void DirectionHandling()
  {
    std::cout << "Sweep direction" << std::endl;

    const CabinState movingUp{ 5U, Direction::Up, false };
    const std::vector<Passenger> committed{ Onboard(1, 8) };

    const auto onTheWay = EvaluateAssignment(movingUp, committed, Waiting(6, 9), 8U, kTiming, kWeights);
    Check(onTheWay.feasible && onTheWay.onTheWay, "same direction and still ahead: on the way");

    const auto behind = EvaluateAssignment(movingUp, committed, Waiting(4, 1), 8U, kTiming, kWeights);
    Check(behind.feasible && !behind.onTheWay, "opposite direction: the cabin has to reverse first");
    Check(behind.pickupEtaMs > onTheWay.pickupEtaMs, "a call that needs a reversal is picked up later");
    Check(behind.marginalCostMs > onTheWay.marginalCostMs, "a call that needs a reversal costs more");
  }

  void SharingAStopIsCheaper()
  {
    std::cout << "Shared stops" << std::endl;

    const CabinState idle{ 0U, Direction::None, false };
    const std::vector<Passenger> committed{ Waiting(5, 9) };

    const auto sharedPickup = EvaluateAssignment(idle, committed, Waiting(5, 8), 8U, kTiming, kWeights);
    const auto newStops = EvaluateAssignment(idle, committed, Waiting(3, 7), 8U, kTiming, kWeights);

    Check(sharedPickup.marginalCostMs < newStops.marginalCostMs,
      "boarding together at an already scheduled stop costs less than adding two new stops");

    // An empty cabin far away is worse than a busy one that is already passing by.
    const CabinState farAway{ 9U, Direction::None, false };
    const auto far = EvaluateAssignment(farAway, {}, Waiting(2, 6), 8U, kTiming, kWeights);
    const auto near = EvaluateAssignment(CabinState{ 1U, Direction::Up, false }, { Onboard(0, 8) }, Waiting(2, 6), 8U, kTiming, kWeights);
    Check(near.pickupEtaMs < far.pickupEtaMs, "a cabin passing by picks up before an idle cabin far away");
  }

  void Capacity()
  {
    std::cout << "Capacity" << std::endl;

    const CabinState movingUp{ 0U, Direction::Up, false };

    const auto full = EvaluateAssignment(movingUp, { Onboard(0, 9), Onboard(0, 9) }, Waiting(3, 6), 2U, kTiming, kWeights);
    Check(!full.feasible, "a cabin that would carry 3 people with capacity 2 cannot take the call");

    const auto roomy = EvaluateAssignment(movingUp, { Onboard(0, 9), Onboard(0, 9) }, Waiting(3, 6), 3U, kTiming, kWeights);
    Check(roomy.feasible && roomy.peakLoad == 3U, "with capacity 3 the same call fits");

    // The two passengers leave at floor 2, before the new one boards at floor 3: they are never together.
    const auto sequential = EvaluateAssignment(movingUp, { Onboard(0, 2), Onboard(0, 2) }, Waiting(3, 6), 2U, kTiming, kWeights);
    Check(sequential.feasible && sequential.peakLoad == 2U,
      "passengers that never share the cabin do not count together (no false 'full')");

    // Many people waiting at different floors, none of them inside yet.
    const CabinState idle{ 0U, Direction::None, false };
    const std::vector<Passenger> manyWaiting{ Waiting(1, 2), Waiting(3, 4), Waiting(5, 6), Waiting(7, 8) };
    const auto spread = EvaluateAssignment(idle, manyWaiting, Waiting(8, 9), 2U, kTiming, kWeights);
    Check(spread.feasible, "assigned-but-not-boarded passengers whose rides do not overlap do not block the cabin");
  }

  void DecidedTargetIsNotReevaluated()
  {
    std::cout << "Decided destination" << std::endl;

    // The cabin is closing its doors at floor 1 and has already decided to go down to floor 0.
    const std::vector<Passenger> committed{ Waiting(0, 6) };
    const CabinState free{ 1U, Direction::Up, true, Floors::InvalidFloor };
    const CabinState decided{ 1U, Direction::Up, true, 0U };

    const auto reevaluated = EvaluateAssignment(free, committed, Waiting(1, 4), 8U, kTiming, kWeights);
    const auto forced = EvaluateAssignment(decided, committed, Waiting(1, 4), 8U, kTiming, kWeights);
    Check(forced.pickupEtaMs > reevaluated.pickupEtaMs, "the pickup comes after the trip to the floor the cabin already chose");

    const auto tight = EvaluateAssignment(decided, { Waiting(0, 6), Waiting(0, 7), Waiting(1, 5) }, Waiting(1, 4), 3U, kTiming, kWeights);
    Check(!tight.feasible, "the load accumulated on the forced trip is counted against the capacity");
  }

  void StopsOnTheWayToTheDecidedTarget()
  {
    std::cout << "Closer stops while travelling" << std::endl;

    // Full cabin (capacity 4) at floor 0, already going to floor 2: a new call at floor 1 would be
    // picked up on the way, when the cabin is full, so it cannot be accepted.
    const CabinState decided{ 0U, Direction::Up, true, 2U };
    const std::vector<Passenger> inside{ Onboard(0, 6), Onboard(0, 5), Onboard(0, 2), Onboard(0, 5) };

    const auto evaluation = EvaluateAssignment(decided, inside, Waiting(1, 6), 4U, kTiming, kWeights);
    Check(!evaluation.feasible, "a call between the cabin and its decided target is picked up there, so the load counts");
  }

  void WaitingTimeWeighsMore()
  {
    std::cout << "Anti-starvation" << std::endl;

    const CabinState idle{ 0U, Direction::None, false };
    const Passenger extra = Waiting(5, 9);

    const auto fresh = SimulateRoute(idle, { Waiting(1, 3, 0.0) }, &extra, kTiming, kWeights);
    const auto old = SimulateRoute(idle, { Waiting(1, 3, 60000.0) }, &extra, kTiming, kWeights);
    Check(old.cost > fresh.cost, "the same route costs more when it delays a passenger who has waited a long time");
  }

  void PickupOnlyStop()
  {
    std::cout << "Floors" << std::endl;

    Floors floors;
    const auto call = std::make_shared<Call>(2U, 5U);
    floors.SetPickupStop(call);

    Check(floors.CountStops() == 1U, "a new assignment flags only the pickup floor (no empty stop at the destination)");

    auto direction = Direction::None;
    const auto next = floors.GetNextStop(0U, direction);
    Check(next == 2U && direction == Direction::Up, "the next stop is the pickup floor, sweeping up");
  }
}

namespace
{
  void ExpressRuns()
  {
    std::cout << "Express runs (zoned buildings)" << std::endl;

    // A high zone cabin: it serves the lobby and floors 6-9, passes 1-5 express.
    const std::vector<bool> highZone{ true, false, false, false, false, false, true, true, true, true };
    const Timing timing{ 200.0, 1000.0, 20.0, 50.0 };

    const CabinState atLobby{ 0U, Direction::None, false, Floors::InvalidFloor, &highZone };
    Check(Near(TravelMs(atLobby, 0U, 8U, timing), 5 * 50.0 + 3 * 200.0), "lobby to floor 8: 5 express floors, then 3 normal ones");
    Check(Near(TravelMs(atLobby, 8U, 0U, timing), 2 * 200.0 + 5 * 50.0 + 200.0), "the same time downwards: floors 7 and 6, 5 express, then the lobby");
    Check(Near(TravelMs(atLobby, 9U, 6U, timing), 3 * 200.0), "inside the zone: normal floors");

    const CabinState everyFloor{ 0U, Direction::None, false };
    Check(Near(TravelMs(everyFloor, 0U, 8U, timing), 8 * 200.0), "without zones every floor takes the normal time");

    // The pickup estimate of a call at the lobby for a cabin parked at floor 8 of the high zone.
    const CabinState parkedHigh{ 8U, Direction::None, false, Floors::InvalidFloor, &highZone };
    const Passenger toNine = Waiting(0, 9);
    const auto route = SimulateRoute(parkedHigh, {}, &toNine, timing, kWeights);
    Check(Near(route.extraPickupMs, 2 * 200.0 + 5 * 50.0 + 200.0 + 1000.0), "the pickup estimate uses the express floors (1850 ms)");

    const auto legacy = SimulateRoute(CabinState{ 8U, Direction::None, false }, {}, &toNine, timing, kWeights);
    Check(route.extraPickupMs < legacy.extraPickupMs, "and is shorter than counting every floor as a normal one");
  }
}

int main()
{
  {
    std::ofstream config("tests_config.json");
    config << "{ \"numberOfElevators\": 2, \"numberOfFloors\": 10, \"maxPeople\": 8 }";
  }

  Configuration::LoadFromFile("tests_config.json");

  TimingOfAnIdleCabin();
  DirectionHandling();
  SharingAStopIsCheaper();
  Capacity();
  DecidedTargetIsNotReevaluated();
  StopsOnTheWayToTheDecidedTarget();
  WaitingTimeWeighsMore();
  PickupOnlyStop();
  ExpressRuns();

  std::cout << std::endl << (g_checks - g_failures) << "/" << g_checks << " checks passed" << std::endl;
  return g_failures == 0 ? 0 : 1;
}
