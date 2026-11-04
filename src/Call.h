/**********************************************************************************
*        File: Call.h
* Description: Implements the people elevator calls.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include "Floors.h"
#include "SimulationClock.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>
#include <string>

class Call final 
{
public:
  explicit Call(const Floors::FloorNumber startFloor = 0, const Floors::FloorNumber destinationFloor = 0, const bool manual = false) :
    m_id(NextId()), m_manual(manual), m_startFloor(startFloor), m_destinationFloor(destinationFloor)
  {
  }

  // Identifier of the passenger, unique within a run (see ResetIds()).
  std::uint64_t GetId() const { return m_id; }
  // True if the passenger has been created by the user (web interface) instead of the generator.
  bool IsManual() const { return m_manual; }

  static void ResetIds() { Counter() = 0U; }

  ~Call() = default;

  Call(const Call&) = delete;
  Call& operator=(const Call&) = delete;

  Call(Call&&) = delete;
  Call& operator=(Call&&) = delete;

public:
  Floors::FloorNumber GetStartFloor() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_startFloor;
  }

  Floors::FloorNumber GetDestinationFloor() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_destinationFloor;
  }

  void SetStartFloor(const Floors::FloorNumber startFloor)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_startFloor = startFloor;
  }

  void SetDestinationFloor(const Floors::FloorNumber destinationFloor)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_destinationFloor = destinationFloor;
  }

  std::string GetAssignedElevator() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_assignedElevator;
  }

  void SetAssignedElevator(std::string assignedElevator)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assignedElevator = std::move(assignedElevator);
  }

  Direction GetDirection() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_startFloor < m_destinationFloor ? Direction::Up : Direction::Down;
  }

  void MarkQueued()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queuedMs = SimulationClock::Now();
  }

  // Simulated time elapsed since the call was queued (the passenger pressed the button).
  std::chrono::milliseconds GetWaitingTime() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::chrono::milliseconds(SimulationClock::Now() - m_queuedMs);
  }

  long long GetQueuedTimeMs() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queuedMs;
  }

  // Marks the first boarding: returns true only the first time, so that a passenger who had to leave a cabin
  // put out of order and boards another one is not counted twice in the statistics.
  bool MarkBoarded()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto first = !m_boarded;
    m_boarded = true;
    return first;
  }

  bool HasBoarded() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_boarded;
  }

  // The passenger had to leave a cabin at 'floor' (out of order): he waits there for another cabin,
  // keeping his destination and his original queue time.
  void Evacuate(const Floors::FloorNumber floor)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_startFloor = floor;
    m_assignedElevator = "?";
    ++m_evacuations;
  }

  unsigned int GetEvacuations() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_evacuations;
  }

  bool IsValid() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return Floors::IsValid(m_startFloor) && Floors::IsValid(m_destinationFloor) && m_startFloor != m_destinationFloor;
  }

  std::string ToString() const 
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return "[" + m_assignedElevator + " " + std::to_string(m_startFloor) + ", " + std::to_string(m_destinationFloor) + "]";
  }

private:
  static std::atomic<std::uint64_t>& Counter()
  {
    static std::atomic<std::uint64_t> counter{ 0U };
    return counter;
  }

  static std::uint64_t NextId() { return ++Counter(); }

  const std::uint64_t m_id;
  const bool m_manual;

  // Calls are observed by the generator, management, elevator threads and the UI.
  // Protecting the small mutable state avoids races on assignment and wait timing.
  mutable std::mutex m_mutex;

  Floors::FloorNumber m_startFloor = 0;
  Floors::FloorNumber m_destinationFloor = 0;

  std::string m_assignedElevator = "?";
  long long m_queuedMs{ SimulationClock::Now() };
  bool m_boarded{ false };
  unsigned int m_evacuations{ 0U };
};

