#include "Elevator.h"
#include "Log.h"
#include "Watchdog.h"
#include "Configuration.h"
#include "Statistics.h"
#include "EventBus.h"
#include "SimulationClock.h"
#include "Energy.h"
#include "BuildingContext.h"

Elevator::Elevator(const std::string& id)
{
  SetId(id);

  // The cabin thread runs in the building that creates the cabin (see BuildingContext).
  auto* context = &Context::Current();
  m_thread = std::make_unique<std::thread>([this, context]()
  {
    Context::Scope scope(context);
    ElevatorThreadFunction();
  });
}

Elevator::~Elevator()
{
  ShutDown();
}

void Elevator::Stop()
{
  m_status = ElevatorStatus::Idle;
  m_log.Trace("Stopped");

  OpenDoors();
}

bool Elevator::AnswerToCall(const std::shared_ptr<Call>& call)
{
  m_log.Trace("Call received");
  m_log.Trace("Current floor: " + std::to_string(m_currentFloor.load()));

  if (!call->IsValid())
  {
    m_log.Trace("*** INVALID CALL ***", Log::TraceLevel::Warning);
    return false;
  }

  m_floors.SetPickupStop(call);
  m_floors.Trace(m_currentFloor.load());

  {
    std::lock_guard<std::mutex> lock(m_goMutex);

    if (m_currentDirection.load() == Direction::None)
      m_currentDirection = call->GetDirection();

    m_callPending = true;
  }

  m_go.notify_all();
  return true;
}

void Elevator::ElevatorThreadFunction()
{
  m_log.Trace("Working", Log::TraceLevel::Verbose);
  m_working = true;

  do
  {
    CloseDoors();
    m_log.Trace("Waiting...");

    if (!m_people.Empty())
      m_log.Trace("** ERROR ** The elevator is in idle but there are still people inside", Log::TraceLevel::Error);

    if (StopRequested())
      break;

    // Nothing to do: the cabin may go and wait on its home floor (idle parking).
    ParkIfIdle();

    {
      // Wait with a predicate: a notification sent before the thread starts waiting
      // (or while it is serving other stops) must not be lost, and spurious
      // wake-ups must be ignored. The lock is released before moving the cabin.
      std::unique_lock<std::mutex> lock(m_goMutex);
      m_go.wait(lock, [this]() { return m_callPending || m_shutdownRequested.load(); });
      m_callPending = false;
    }

    if (StopRequested())
      break;

    if (m_outOfOrderRequested)
    {
      HandleOutOfOrder();
      continue;
    }

    // The cabin has been waiting for a call: its next movements start from now.
    SimulationClock::RestartThreadTimeline();

    auto currentDirection = m_currentDirection.load();
    auto nextFloor = m_floors.GetNextStop(m_currentFloor.load(), currentDirection);
    m_currentDirection = currentDirection;
    m_targetFloor = nextFloor;

    // Continue while there are stops, unless a shutdown is requested or the cabin is put out of order.
    while (Floors::IsValid(nextFloor) && !StopRequested() && !m_outOfOrderRequested)
    {
      currentDirection = m_currentDirection.load();
      m_log.Trace(currentDirection == Direction::Up ? "Current direction: UP" : "Current direction: DOWN");

      if (nextFloor != m_currentFloor.load())
      {
        Move(nextFloor);
      }
      else
      {
        m_floors.ClearStop(m_currentFloor.load(), m_currentDirection.load());
      }

      PeopleEnterAndExit();

      currentDirection = m_currentDirection.load();
      nextFloor = m_floors.GetNextStop(m_currentFloor.load(), currentDirection);
      m_currentDirection = currentDirection;
      m_targetFloor = nextFloor;
    }

    if (m_outOfOrderRequested && !StopRequested())
    {
      HandleOutOfOrder();
      continue;
    }

    {
      // Reset the direction only if no call has been assigned in the meantime:
      // AnswerToCall() sets the direction under the same mutex.
      std::lock_guard<std::mutex> lock(m_goMutex);

      if (!m_callPending)
      {
        m_currentDirection = Direction::None;
        m_displayDirection = Direction::None;
      }
    }
  } while (!StopRequested());

  m_working = false;
  m_log.Trace("Thread exit", ILog::TraceLevel::Debug);
}

bool Elevator::OpenDoors()
{
  if (m_status.load() != ElevatorStatus::Idle)
  {
    m_log.Trace("OpenDoors error: elevator is not idle", Log::TraceLevel::Error);
    return false;
  }

  if (m_doorsStatus.load() == DoorsStatus::Closed)
  {
    m_doorsMotionStartMs = SimulationClock::Now();
    m_doorsMotion = DoorsMotion::Opening;
    Configuration::Simulation::SleepFor(Configuration::Elevator::DoorTransitionTime());
    m_doorsStatus = DoorsStatus::Open;
    m_doorsMotion = DoorsMotion::None;
    m_log.Trace("Doors open", Log::TraceLevel::Verbose);
  }

  return true;
}

bool Elevator::CloseDoors()
{
  if (m_status.load() != ElevatorStatus::Idle)
  {
    m_log.Trace("CloseDoors error: elevator is not idle", Log::TraceLevel::Error);
    return false;
  }

  if (m_doorsStatus.load() == DoorsStatus::Open)
  {
    m_doorsMotionStartMs = SimulationClock::Now();
    m_doorsMotion = DoorsMotion::Closing;
    Configuration::Simulation::SleepFor(Configuration::Elevator::DoorTransitionTime());
    m_doorsStatus = DoorsStatus::Closed;
    m_doorsMotion = DoorsMotion::None;
    ++m_doorCycles;
    AddEnergy(Energy::DoorCycleWh);
    m_log.Trace("Doors closed", Log::TraceLevel::Verbose);
  }

  return true;
}

void Elevator::PeopleEnterAndExit()
{
  // People can enter/exit only with the doors open. After a Move() the doors are already
  // open (Stop()), but when the cabin is already at the requested floor they are still closed.
  OpenDoors();
  m_targetFloor = Floors::InvalidFloor; // the cabin has arrived: the next destination will be chosen after this stop

  const auto previousStatus = m_status.load();
  m_status = ElevatorStatus::PeopleEnterAndExit;

  m_log.Trace("People Enter and Exit...", Log::TraceLevel::Verbose);

  const auto peopleBefore = m_people.Count();
  std::vector<std::shared_ptr<Call>> refusedPassengers;

  // A cabin going out of order lets its passengers out but does not take anybody in.
  m_people.EnterAndExit(Floors::GetPeople(), m_currentFloor.load(), m_currentDirection.load(), m_elevatorId,
    &refusedPassengers, !m_outOfOrderRequested);

  RestoreDestinationStops();

  Configuration::Simulation::SleepFor(Configuration::Elevator::EnterAndExitTime());

  m_status = previousStatus;

  // Passengers refused by a full cabin are given back to the dispatcher (no lock is held here).
  for (const auto& passenger : refusedPassengers)
  {
    passenger->SetAssignedElevator("?");
    Statistics::RecordRequeuedCall();

    if (m_refusedCallHandler)
      m_refusedCallHandler(passenger);
  }

  if (m_capacityFreedHandler && (m_people.Count() < peopleBefore || !refusedPassengers.empty()))
    m_capacityFreedHandler();
}

/**
 * \brief This function ensures that every time a person (call) enters the elevator,
 * the destination is correctly (re)set in the floors stops array.
 */
void Elevator::RestoreDestinationStops()
{
  // This is a workaround.
  // When a call is assigned the stops are set in the floor array and 
  // cleared once reached the start or destination floor; this mechanism can fail in some cases:
  // imagine that the elevator is managing a first call from floor 6 to 1
  // and while is moving from 6 to 1, another call arrives from floor 8 to 1;
  // once reached the floor 1 the stop is cleared, but the second call is still to be managed,
  // so in the next step the elevator go to floor 8, but the destination has been lost and 
  // people remain in the elevator.
  // An alternative solution could be to implement a sort of reference counting of the floors stops
  // but this is the simplest solution.

  // Refresh the stops based on the people inside
  const auto currentFloor = m_currentFloor.load();
  for (auto& person : m_people.Snapshot())
  {
    if (person->GetDestinationFloor() != currentFloor)
    {
      m_log.Trace("Restored stop " + person->ToString(), Log::TraceLevel::Debug);
      m_floors.SetStop(person, true);
    }
  }
}

void Elevator::Move(const Floors::FloorNumber firstRequestedFloor)
{
  auto requestedFloor = firstRequestedFloor;

  if (requestedFloor != m_currentFloor.load())
  {
    CloseDoors();
    ++m_runCounter;
    AddEnergy(Energy::RunStartWh);

    do
    {
      m_status = ElevatorStatus::Moving;

      const auto currentFloor = m_currentFloor.load();

      if (requestedFloor > currentFloor && currentFloor < Floors::TopFloor())
      {
        m_displayDirection = Direction::Up;
        m_log.Trace("Moving Up [" + std::to_string(currentFloor) + "]", Log::TraceLevel::Verbose);
        m_motionStartMs = SimulationClock::Now();
        m_motionToFloor = currentFloor + 1;
        if (!Configuration::Simulation::SleepFor(Configuration::Elevator::TimeToReachTheNextFloor()))
          break; // simulation aborted
        m_currentFloor = currentFloor + 1;
        ++m_totalFloorsTravelled;
        AddTravelEnergy(Direction::Up);
      }
      else if (requestedFloor < currentFloor && currentFloor > 0)
      {
        m_displayDirection = Direction::Down;
        m_log.Trace("Moving Down [" + std::to_string(currentFloor) + "]", Log::TraceLevel::Verbose);
        m_motionStartMs = SimulationClock::Now();
        m_motionToFloor = currentFloor - 1;
        if (!Configuration::Simulation::SleepFor(Configuration::Elevator::TimeToReachTheNextFloor()))
          break; // simulation aborted
        m_currentFloor = currentFloor - 1;
        ++m_totalFloorsTravelled;
        AddTravelEnergy(Direction::Down);
      }

      // A call can be assigned while the cabin is moving (the dispatcher considers an elevator
      // "on the way" for it): if a stop closer than the current target appeared in the travel
      // direction, stop there instead of ignoring it until the target is reached.
      const auto reachedFloor = m_currentFloor.load();
      const auto travelDirection = requestedFloor > reachedFloor ? Direction::Up : Direction::Down;

      if (reachedFloor != requestedFloor && travelDirection == m_currentDirection.load())
      {
        auto searchDirection = travelDirection;
        const auto nearestStop = m_floors.GetNextStop(reachedFloor, searchDirection);

        const auto isCloser = travelDirection == Direction::Up
          ? nearestStop >= reachedFloor && nearestStop < requestedFloor
          : nearestStop <= reachedFloor && nearestStop > requestedFloor;

        if (Floors::IsValid(nearestStop) && searchDirection == travelDirection && isCloser)
        {
          m_log.Trace("New closer stop: floor " + std::to_string(nearestStop), Log::TraceLevel::Debug);
          requestedFloor = nearestStop;
          m_targetFloor = nearestStop;
        }
      }

      // Out of order: the cabin cannot stop between two floors, it stops at the floor it has just reached.
    } while (m_currentFloor.load() != requestedFloor && !StopRequested() && !m_outOfOrderRequested);

    m_motionToFloor = Floors::InvalidFloor;
    m_displayDirection = m_currentDirection.load();

    const std::string message = "Arrived on the floor " + std::to_string(m_currentFloor.load());
    m_log.Trace(message);
  }

  m_floors.ClearStop(m_currentFloor.load(), m_currentDirection.load());
  Stop();
}

void Elevator::RequestShutdown()
{
  {
    std::lock_guard<std::mutex> lock(m_goMutex);
    m_shutdownRequested = true;
  }

  m_go.notify_all();
}

void Elevator::ShutDown()
{
  if (m_thread == nullptr)
    return; // never started or already shut down

  m_log.Trace("Shutdown in progress...", Log::TraceLevel::Verbose);

  const auto callback = std::bind(
    [this](const std::string&) -> void { m_log.Trace("** SHUTDOWN IS TAKING TOO LONG **", ILog::TraceLevel::Warning); },
    std::placeholders::_1);

  Watchdog watchdog(m_name, 20s, callback);

  RequestShutdown();

  if (m_thread->joinable())
    m_thread->join();

  watchdog.Stop();

  m_thread.reset();

  m_log.Trace("Shutdown completed", Log::TraceLevel::Verbose);
}

void Elevator::SetId(std::string id)
{
  m_elevatorId = std::move(id);
  m_name = "Elevator " + m_elevatorId;

  m_people.SetId(m_name);
  m_floors.SetId(m_name);
  m_log.SetTraceId(m_name);
}

void Elevator::SetHomeFloor(const Floors::FloorNumber floor)
{
  {
    std::lock_guard<std::mutex> lock(m_goMutex);
    m_homeFloor = floor;
    m_callPending = true; // an idle cabin wakes up, finds no stop and goes to its home floor
  }

  m_go.notify_all();
}

void Elevator::AddEnergy(const double wh)
{
  m_energyMilliWh += static_cast<std::uint64_t>(std::llround(wh * 1000.0));
}

void Elevator::AddTravelEnergy(const Direction direction)
{
  AddEnergy(Energy::TravelPerFloorWh(m_people.Count(), direction));
}

void Elevator::ParkIfIdle()
{
  const auto home = m_homeFloor.load();
  if (!Floors::IsValid(home) || home == m_currentFloor.load())
    return;

  const auto callArrived = [this]()
  {
    std::lock_guard<std::mutex> lock(m_goMutex);
    return m_callPending || m_outOfOrderRequested.load();
  };

  if (callArrived())
    return;

  // While parking the dispatcher sees an idle cabin (no direction, no decided destination): a call stops the
  // parking at the next floor, which is what the route simulation of an idle cabin assumes.
  ++m_runCounter;
  AddEnergy(Energy::RunStartWh);
  m_status = ElevatorStatus::Moving;
  m_displayDirection = home > m_currentFloor.load() ? Direction::Up : Direction::Down;
  m_log.Trace("Parking to floor " + std::to_string(home), Log::TraceLevel::Verbose);

  while (m_currentFloor.load() != home && !StopRequested() && !callArrived())
  {
    const auto currentFloor = m_currentFloor.load();
    const auto nextFloor = home > currentFloor ? currentFloor + 1 : currentFloor - 1;

    m_motionStartMs = SimulationClock::Now();
    m_motionToFloor = nextFloor;
    if (!Configuration::Simulation::SleepFor(Configuration::Elevator::TimeToReachTheNextFloor()))
      break;

    m_currentFloor = nextFloor;
    ++m_totalFloorsTravelled;
    AddTravelEnergy(nextFloor > currentFloor ? Direction::Up : Direction::Down);
  }

  m_motionToFloor = Floors::InvalidFloor;
  m_status = ElevatorStatus::Idle;
  m_displayDirection = Direction::None;
}

bool Elevator::StopRequested() const
{
  return m_shutdownRequested.load() || SimulationClock::Aborted();
}

void Elevator::SetOutOfOrder(const bool outOfOrder)
{
  {
    std::lock_guard<std::mutex> lock(m_goMutex);

    if (m_outOfOrderRequested.load() == outOfOrder)
      return;

    m_outOfOrderRequested = outOfOrder;
    m_callPending = true; // wakes the cabin if it is idle
  }

  m_log.Trace(outOfOrder ? "Out of order requested" : "Back in service requested", Log::TraceLevel::Info);
  m_go.notify_all();
}

void Elevator::HandleOutOfOrder()
{
  const auto floor = m_currentFloor.load();
  m_targetFloor = Floors::InvalidFloor;
  m_status = ElevatorStatus::Idle; // doors can be operated only by an idle cabin

  // Everybody inside gets out here and waits for another cabin, keeping destination and queue time.
  const auto onboard = m_people.TakeAll();
  if (!onboard.empty())
    OpenDoors();

  for (const auto& passenger : onboard)
  {
    passenger->Evacuate(floor);
    Floors::GetPeople().InsertEvacuated(passenger);

    EventBus::Publish("evacuate", Json::Writer::Object()
      .Add("id", static_cast<unsigned long long>(passenger->GetId()))
      .Add("elevator", m_elevatorId)
      .Add("floor", floor));
  }

  // Passengers still waiting for this cabin go back to the dispatcher. Boarding happens only on this
  // thread, so nobody can board this cabin between the release and the out of order state.
  const auto waiting = Floors::GetPeople().ReleaseAssigned(m_elevatorId);
  m_floors.ClearAllStops();

  CloseDoors();

  m_status = ElevatorStatus::OutOfOrder;
  m_currentDirection = Direction::None;
  m_displayDirection = Direction::None;

  m_log.Trace("Out of order at floor " + std::to_string(floor) + ": " + std::to_string(onboard.size()) +
    " passengers evacuated, " + std::to_string(waiting.size()) + " calls reassigned", Log::TraceLevel::Warning);

  EventBus::Publish("fault", Json::Writer::Object()
    .Add("elevator", m_elevatorId)
    .Add("floor", floor)
    .Add("evacuated", static_cast<unsigned long long>(onboard.size()))
    .Add("reassigned", static_cast<unsigned long long>(waiting.size())));

  if (m_refusedCallHandler)
  {
    for (const auto& passenger : onboard)
      m_refusedCallHandler(passenger);

    for (const auto& passenger : waiting)
      m_refusedCallHandler(passenger);
  }

  {
    std::unique_lock<std::mutex> lock(m_goMutex);
    m_go.wait(lock, [this]() { return !m_outOfOrderRequested.load() || m_shutdownRequested.load(); });
    m_callPending = false;
  }

  m_status = ElevatorStatus::Idle;

  if (m_shutdownRequested)
    return;

  m_log.Trace("Back in service", Log::TraceLevel::Info);
  EventBus::Publish("repair", Json::Writer::Object().Add("elevator", m_elevatorId).Add("floor", m_currentFloor.load()));

  // The cabin can take calls again: the ones waiting for capacity may now fit.
  if (m_capacityFreedHandler)
    m_capacityFreedHandler();
}

std::size_t Elevator::GetCommittedPeopleCount() const
{
  return m_people.Count() + Floors::GetPeople().CountByAssignedElevator(m_elevatorId);
}

ElevatorDispatchState Elevator::GetDispatchState() const
{
  ElevatorDispatchState state;
  state.floor = m_currentFloor.load();
  state.direction = m_currentDirection.load();
  // A cabin going out of order must not receive calls any more, even before it has stopped.
  state.status = m_outOfOrderRequested.load() ? ElevatorStatus::OutOfOrder : m_status.load();
  state.doorsOpen = m_doorsStatus.load() == DoorsStatus::Open;
  state.targetFloor = m_targetFloor.load();
  state.onboard = m_people.Snapshot();
  return state;
}

ElevatorSnapshot Elevator::GetSnapshot() const
{
  ElevatorSnapshot snapshot;
  snapshot.id = m_elevatorId;
  snapshot.name = m_name;
  snapshot.currentFloor = m_currentFloor.load();
  snapshot.status = m_status.load();
  snapshot.direction = m_displayDirection.load();
  snapshot.outOfOrder = m_outOfOrderRequested.load();
  snapshot.doorsOpen = m_doorsStatus.load() == DoorsStatus::Open;
  snapshot.doorsMotion = m_doorsMotion.load();
  snapshot.doorsMotionStartMs = m_doorsMotionStartMs.load();
  snapshot.motionToFloor = snapshot.status == ElevatorStatus::Moving ? m_motionToFloor.load() : Floors::InvalidFloor;
  snapshot.motionStartMs = m_motionStartMs.load();
  snapshot.targetFloor = m_targetFloor.load();
  snapshot.passengers = m_people.Snapshot();
  snapshot.peopleCount = snapshot.passengers.size();
  snapshot.stops = m_floors.GetStops();
  snapshot.travelledFloors = m_totalFloorsTravelled.load();
  snapshot.runs = m_runCounter.load();
  snapshot.doorCycles = m_doorCycles.load();
  snapshot.travelEnergyWh = GetTravelEnergyWh();
  return snapshot;
}
