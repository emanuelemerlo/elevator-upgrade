/**********************************************************************************
*        File: Floors.h
* Description: Implements the building floors.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include "Configuration.h"
#include "Log.h"

#include <vector>
#include <mutex>

enum class Direction
{
  None = -1,
  Up,
  Down,
  Both,
};

typedef std::pair<bool, Direction> Stop;
typedef std::vector<Stop> FloorStops;

class Floors final
{
public: // Types, constants and static functions
    typedef unsigned int FloorNumber;

    static constexpr FloorNumber BottomFloor = 0U;
    static constexpr FloorNumber InvalidFloor = static_cast<FloorNumber>(-1);

    static FloorNumber TotalFloors();
    static FloorNumber TopFloor();
    static bool IsValid(FloorNumber floorNumber);

    // Pure next-stop rule (directional LOOK/SCAN) used by the real cabins and by the dispatcher
    // simulation, so that the predicted route is by construction the route the cabin will follow.
    // 'currentDirection' is updated when the sweep has to be reversed.
    static FloorNumber FindNextStop(const FloorStops& stops, FloorNumber currentFloor, Direction& currentDirection);

public:
  Floors();

public:
  bool SetStop(const class std::shared_ptr<class Call>& call, bool destinationOnly = false);
  // Flags only the pickup floor: the destination is flagged when the passenger boards, so that the
  // cabin never makes an empty stop at a destination floor before the passenger is on board.
  bool SetPickupStop(const class std::shared_ptr<class Call>& call);
  void ClearStop(const FloorNumber floor, const Direction direction);
  void ClearAllStops();
  FloorStops GetStops() const;

  FloorNumber GetNextStop(const FloorNumber currentFloor, Direction& currentDirection);
  std::size_t CountStops() const;
  std::size_t CountStopsBetween(FloorNumber firstFloor, FloorNumber lastFloor) const;

  static class People& GetPeople();

  void Trace(const FloorNumber currentFloor);

  void SetId(const std::string& id) { m_log.SetTraceId(id); }
  std::string GetId() const { return m_log.GetTraceId(); }

private:
  static FloorNumber Search(const FloorStops& stops, const FloorNumber startFloor, const Direction direction);

private:
  FloorStops m_stops;

  mutable std::mutex m_mutex;

  Log m_log;
};

