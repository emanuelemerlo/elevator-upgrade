/**********************************************************************************
*        File: Elevator.h
* Description: Implements the elevator logic.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <condition_variable>
#include <cstddef>
#include <cstdint>

#include "Floors.h"
#include "People.h"

using namespace std::chrono_literals;

enum class ElevatorStatus
{
  OutOfOrder = -1,
  Idle = 0,
  PeopleEnterAndExit,
  Moving,
};

enum class DoorsStatus
{
  Open,
  Closed
};

enum class DoorsMotion
{
  None,
  Opening,
  Closing
};

// Everything a viewer needs to draw a cabin. Times are simulated milliseconds (SimulationClock::Now()).
struct ElevatorSnapshot
{
  std::string id;
  std::string name;
  Floors::FloorNumber currentFloor{ 0U };
  ElevatorStatus status{ ElevatorStatus::Idle };
  Direction direction{ Direction::None };
  std::size_t peopleCount{ 0U };

  bool outOfOrder{ false };            // requested by the user: the cabin is going (or has gone) out of service
  bool doorsOpen{ false };
  DoorsMotion doorsMotion{ DoorsMotion::None };
  long long doorsMotionStartMs{ 0 };
  // Floor transition in progress (status Moving): from 'currentFloor' to 'motionToFloor', started at 'motionStartMs'.
  Floors::FloorNumber motionToFloor{ Floors::InvalidFloor };
  long long motionStartMs{ 0 };
  Floors::FloorNumber targetFloor{ Floors::InvalidFloor };

  std::vector<std::shared_ptr<Call>> passengers;
  FloorStops stops;

  std::uint64_t travelledFloors{ 0U };
  std::uint64_t runs{ 0U };
  std::uint64_t doorCycles{ 0U };
  double travelEnergyWh{ 0.0 };        // motor and doors (standby excluded, see Energy.h)
};

// Consistent view of a cabin used by the dispatcher to predict its route.
struct ElevatorDispatchState
{
  Floors::FloorNumber floor{ 0U };
  Direction direction{ Direction::None };
  ElevatorStatus status{ ElevatorStatus::Idle };
  bool doorsOpen{ false };
  // Floor the cabin has already decided to go to (doors closing or moving), InvalidFloor if it is at a stop or idle.
  Floors::FloorNumber targetFloor{ Floors::InvalidFloor };
  std::vector<std::shared_ptr<Call>> onboard;
};

class Elevator final
{
public:
  explicit Elevator(const std::string& id = "");

  Elevator(const Elevator&) = delete;
  Elevator(Elevator&&) = delete;

  ~Elevator();

  Elevator& operator=(const Elevator&) = delete;
  Elevator& operator=(Elevator&& other) noexcept = delete;

public:
  bool AnswerToCall(const std::shared_ptr<Call>& call);

  // Two-phase shutdown: RequestShutdown() only asks the elevator thread to stop (non blocking),
  // ShutDown() requests it (if needed) and waits for the thread. Requesting shutdown on every
  // elevator first makes them stop in parallel instead of one after the other.
  void RequestShutdown();
  void ShutDown();

  // Puts the cabin out of service (it stops at the next floor, lets everybody out and gives its passengers
  // back to the dispatcher) or back into service. The dispatcher stops considering it immediately.
  void SetOutOfOrder(bool outOfOrder);

  // Floor where the cabin waits when it has nothing to do (InvalidFloor: it stays where it stopped).
  // Setting it wakes an idle cabin up, so that it goes there at once.
  void SetHomeFloor(Floors::FloorNumber floor);
  bool IsOutOfOrder() const { return m_outOfOrderRequested.load(); }

  void SetId(std::string id);
  std::string GetId() const { return m_elevatorId; }

  std::string GetElevatorName() const { return m_name; }

  Floors::FloorNumber GetCurrentFloor() const { return m_currentFloor.load(); }
  ElevatorStatus GetStatus() const { return m_status.load(); }
  Direction GetDirection() const { return m_currentDirection.load(); }
  ElevatorSnapshot GetSnapshot() const;
  ElevatorDispatchState GetDispatchState() const;

  // Called (from the cabin thread, with no lock held) when a passenger could not board because the cabin
  // was full: the dispatcher must assign the call again.
  void SetRefusedCallHandler(std::function<void(const std::shared_ptr<Call>&)> handler) { m_refusedCallHandler = std::move(handler); }
  // Called (from the cabin thread) when passengers have left the cabin, i.e. when capacity may have been freed.
  void SetCapacityFreedHandler(std::function<void()> handler) { m_capacityFreedHandler = std::move(handler); }
  std::size_t GetPeopleCount() const { return m_people.Count(); }
  std::size_t GetCommittedPeopleCount() const;
  std::size_t GetQueuedStopsCount() const { return m_floors.CountStops(); }
  std::size_t GetQueuedStopsBetween(Floors::FloorNumber firstFloor, Floors::FloorNumber lastFloor) const
    { return m_floors.CountStopsBetween(firstFloor, lastFloor); }
  std::uint64_t GetTotalFloorsTravelled() const { return m_totalFloorsTravelled.load(); }
  std::uint64_t GetRunCounter() const { return m_runCounter.load(); }
  std::uint64_t GetDoorCycles() const { return m_doorCycles.load(); }
  double GetTravelEnergyWh() const { return static_cast<double>(m_energyMilliWh.load()) / 1000.0; }

private:
  bool OpenDoors();
  bool CloseDoors();

  void PeopleEnterAndExit();
  void Move(Floors::FloorNumber requestedFloor);
  void Stop();

  void RestoreDestinationStops();
  void HandleOutOfOrder();
  void ParkIfIdle();
  void AddEnergy(double wh);
  void AddTravelEnergy(Direction direction);
  bool StopRequested() const;

private:
  void ElevatorThreadFunction();

private:
  std::atomic<Floors::FloorNumber> m_currentFloor{ 0 };
  Floors m_floors;

  People m_people;

  std::atomic<ElevatorStatus> m_status{ ElevatorStatus::Idle };
  std::atomic<Direction> m_currentDirection{ Direction::None };
  std::atomic<Direction> m_displayDirection{ Direction::None };
  std::atomic<Floors::FloorNumber> m_targetFloor{ Floors::InvalidFloor };

  std::atomic<DoorsStatus> m_doorsStatus{ DoorsStatus::Closed };
  std::atomic<DoorsMotion> m_doorsMotion{ DoorsMotion::None };
  std::atomic<long long> m_doorsMotionStartMs{ 0 };
  std::atomic<Floors::FloorNumber> m_motionToFloor{ Floors::InvalidFloor };
  std::atomic<long long> m_motionStartMs{ 0 };
  std::atomic_bool m_outOfOrderRequested{ false };
  std::atomic<Floors::FloorNumber> m_homeFloor{ Floors::InvalidFloor };

  // m_callPending is the wake-up predicate of m_go: it is written under m_goMutex so that a
  // call assigned while the thread is busy (or not yet waiting) is never lost.
  std::condition_variable m_go;
  std::mutex m_goMutex;
  bool m_callPending{ false };

  std::string m_elevatorId = "?";
  std::string m_name;

  std::unique_ptr<std::thread> m_thread;
  std::atomic_bool m_shutdownRequested{ false };
  std::atomic_bool m_working{ false };

  std::atomic<std::uint64_t> m_totalFloorsTravelled{ 0U };
  std::atomic<std::uint64_t> m_runCounter{ 0U };
  std::atomic<std::uint64_t> m_doorCycles{ 0U };
  std::atomic<std::uint64_t> m_energyMilliWh{ 0U };

  std::function<void(const std::shared_ptr<Call>&)> m_refusedCallHandler;
  std::function<void()> m_capacityFreedHandler;

  Log m_log;
};

