#include "People.h"
#include "Log.h"
#include "Statistics.h"
#include "Configuration.h"
#include "EventBus.h"

#include <map>
#include <sstream>

void People::EnterAndExit(
  People& waitingPeople, 
  const Floors::FloorNumber currentFloor, 
  const Direction currentDirection, 
  const std::string& elevatorId,
  std::vector<std::shared_ptr<Call>>* refusedPassengers,
  const bool allowBoarding)
{
  waitingPeople.Trace(currentFloor);
  Exit(currentFloor, elevatorId);

  if (allowBoarding)
    Enter(waitingPeople, currentFloor, currentDirection, elevatorId, refusedPassengers);
}

void People::Insert(const std::shared_ptr<Call>& call)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  call->MarkQueued();
  Statistics::RecordQueuedCall();

  push_front(call);

  EventBus::Publish("call", Json::Writer::Object()
    .Add("id", static_cast<unsigned long long>(call->GetId()))
    .Add("from", call->GetStartFloor())
    .Add("to", call->GetDestinationFloor())
    .Add("manual", call->IsManual()));
}

void People::InsertEvacuated(const std::shared_ptr<Call>& call)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  push_front(call);
}

std::vector<std::shared_ptr<Call>> People::TakeAll()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::shared_ptr<Call>> people(begin(), end());
  clear();
  return people;
}

std::vector<std::shared_ptr<Call>> People::ReleaseAssigned(const std::string& elevatorId)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<std::shared_ptr<Call>> released;
  for (const auto& person : *this)
  {
    if (person->GetAssignedElevator() == elevatorId)
    {
      person->SetAssignedElevator("?");
      released.push_back(person);
    }
  }

  return released;
}

std::vector<std::shared_ptr<Call>> People::RemoveImpatient(const long long nowMs)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<std::shared_ptr<Call>> impatient;
  for (auto person = begin(); person != end();)
  {
    if ((*person)->HasLostPatience(nowMs))
    {
      impatient.push_back(*person);
      person = erase(person);
      continue;
    }

    ++person;
  }

  return impatient;
}

void People::Clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  clear();
}

void People::Trace(const Floors::FloorNumber currentFloor)
{
  std::stringstream message;

  if (currentFloor != Floors::InvalidFloor)
  {
    message << "People waiting on floor " << std::to_string(currentFloor) << ": ";

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& person : *this)
      if ((*person).GetStartFloor() == currentFloor)
        message << (*person).ToString();
  }
  else
  {
    message << "People waiting on floors: ";

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& person : *this)
        message << (*person).ToString();
  }

  m_log.Trace(message);
}

bool People::Empty() 
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return empty();
}

std::size_t People::Count() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return size();
}

std::size_t People::CountByAssignedElevator(const std::string& elevatorId) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::size_t count = 0;
  for (const auto& person : *this)
    if (person->GetAssignedElevator() == elevatorId)
      ++count;

  return count;
}

std::size_t People::CountByStartFloor(const Floors::FloorNumber floor) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::size_t count = 0;
  for (const auto& person : *this)
    if (person->GetStartFloor() == floor)
      ++count;

  return count;
}

std::vector<std::pair<std::string, std::size_t>> People::CountByStartFloorAndElevator(const Floors::FloorNumber floor) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::map<std::string, std::size_t> counts;
  for (const auto& person : *this)
    if (person->GetStartFloor() == floor)
      ++counts[person->GetAssignedElevator()];

  return std::vector<std::pair<std::string, std::size_t>>(counts.begin(), counts.end());
}

std::vector<std::shared_ptr<Call>> People::Snapshot() const
{
  // Return a copy of the shared_ptr list so callers can iterate without holding
  // the People mutex while elevator threads continue to board or exit passengers.
  std::lock_guard<std::mutex> lock(m_mutex);
  return std::vector<std::shared_ptr<Call>>(begin(), end());
}

void People::Enter(
  People& waitingPeople, 
  const Floors::FloorNumber currentFloor, 
  const Direction currentDirection, 
  const std::string& elevatorId,
  std::vector<std::shared_ptr<Call>>* refusedPassengers)
{
  std::stringstream message;
  message << "People enter from floor " << std::to_string(currentFloor) << ": ";

  // Move matching calls atomically from the global waiting list into this cabin.
  // This prevents another elevator from boarding the same passenger concurrently.
  // Use std::lock + std::unique_lock to support toolsets without std::scoped_lock.
  std::unique_lock<std::mutex> lock1(m_mutex, std::defer_lock);
  std::unique_lock<std::mutex> lock2(waitingPeople.m_mutex, std::defer_lock);
  std::lock(lock1, lock2);
  auto person = waitingPeople.begin();

  while (person != waitingPeople.end())
  {
    if ((*person)->GetStartFloor() == currentFloor && (*person)->GetDirection() == currentDirection && (*person)->GetAssignedElevator() == elevatorId)
    {
      if (size() >= Configuration::Elevator::MaxPeople())
      {
        // The cabin is full: the passenger stays at the floor and must be dispatched again,
        // otherwise nobody would come back for him (his pickup stop has just been cleared).
        message << "FULL ";
        EventBus::Publish("refused", Json::Writer::Object()
          .Add("id", static_cast<unsigned long long>((*person)->GetId()))
          .Add("elevator", elevatorId)
          .Add("floor", currentFloor));
        if (refusedPassengers != nullptr)
          refusedPassengers->push_back(*person);
        ++person;
        continue;
      }

      message << (*person)->ToString();

      const auto waitingTime = (*person)->GetWaitingTime();
      if ((*person)->MarkBoarded())
        Statistics::RecordBoardedPassenger(waitingTime);

      EventBus::Publish("board", Json::Writer::Object()
        .Add("id", static_cast<unsigned long long>((*person)->GetId()))
        .Add("elevator", elevatorId)
        .Add("floor", currentFloor)
        .Add("waitMs", static_cast<long long>(waitingTime.count())));

      push_front(*person);
      Statistics::RecordCabinPeopleCount(size());

      person = waitingPeople.erase(person);
      continue;
    }
      
    ++person;
  }

  m_log.Trace(message);
}

void People::Exit(const Floors::FloorNumber currentFloor, const std::string& elevatorId)
{
  std::stringstream message;
  message << "People exit to floor " << std::to_string(currentFloor) << ": ";

  std::lock_guard<std::mutex> lock(m_mutex);
  auto person = begin();

  while(person != end())
  {
    if ((*person)->GetDestinationFloor() == currentFloor)
    {
      message << (*person)->ToString();
      const auto journeyTime = (*person)->GetWaitingTime();
      Statistics::RecordCompletedPassenger(journeyTime);

      EventBus::Publish("arrive", Json::Writer::Object()
        .Add("id", static_cast<unsigned long long>((*person)->GetId()))
        .Add("elevator", elevatorId)
        .Add("floor", currentFloor)
        .Add("journeyMs", static_cast<long long>(journeyTime.count())));
      person = erase(person);
      continue;
    }

    ++person;
  }

  m_log.Trace(message);
}
