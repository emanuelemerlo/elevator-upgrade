/**********************************************************************************
*        File: People.h
* Description: Collection of people calls.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include <list>
#include <cstddef>
#include <utility>
#include <vector>

#include "Call.h"

class People final : std::list<std::shared_ptr<Call>> 
{
public:
  explicit People(const std::string& id = "") { SetId(id); }
  ~People() noexcept = default;

  People(const People&) = delete;
  People& operator=(const People&) = delete;

  People(const People&&) = delete;
  People& operator=(People&&) = delete;

public:
  void Insert(const std::shared_ptr<Call>& call);
  // Puts back on a floor a passenger who already queued (evacuated from a cabin): no new call is counted.
  void InsertEvacuated(const std::shared_ptr<Call>& call);
  // Removes and returns every passenger (used to evacuate a cabin put out of order).
  std::vector<std::shared_ptr<Call>> TakeAll();
  // Unassigns the waiting passengers assigned to the given elevator and returns them, atomically.
  std::vector<std::shared_ptr<Call>> ReleaseAssigned(const std::string& elevatorId);
  void Clear();
  // Removes and returns the waiting passengers whose patience is over (they take the stairs).
  std::vector<std::shared_ptr<Call>> RemoveImpatient(long long nowMs);

  void EnterAndExit(
    People& waitingPeople, 
    const Floors::FloorNumber currentFloor, 
    const Direction currentDirection, 
    const std::string& elevatorId,
    std::vector<std::shared_ptr<Call>>* refusedPassengers = nullptr,
    bool allowBoarding = true);

  void Trace(const Floors::FloorNumber currentFloor = Floors::InvalidFloor);

  bool Empty();
  std::size_t Count() const;
  std::size_t CountByAssignedElevator(const std::string& elevatorId) const;
  std::size_t CountByStartFloor(Floors::FloorNumber floor) const;
  std::vector<std::pair<std::string, std::size_t>> CountByStartFloorAndElevator(Floors::FloorNumber floor) const;

  void SetId(const std::string& id) { m_log.SetTraceId(id); }
  std::string GetId() const { return m_log.GetTraceId(); }

  std::vector<std::shared_ptr<Call>> Snapshot() const;

private:
  void Enter(
    People& waitingPeople, 
    const Floors::FloorNumber currentFloor, 
    const Direction currentDirection, 
    const std::string& elevatorId,
    std::vector<std::shared_ptr<Call>>* refusedPassengers);

  void Exit(const Floors::FloorNumber currentFloor, const std::string& elevatorId);

private:
  mutable std::mutex m_mutex;

  Log m_log;
};



