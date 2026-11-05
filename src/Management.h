/**********************************************************************************
*        File: Management.h
* Description: Elevator(s) management, manage the calls assignation. 
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include "Log.h"

#include <vector>
#include <memory>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <string>

#include "Elevator.h"
#include "Zoning.h"

class Call;
class Elevator;

class Management final 
{
public:
  explicit Management(const unsigned int numberOfElevators);

  Management(const Management&) = delete;
  Management(Management&&) = delete;

  ~Management();
  
  Management& operator=(const Management&) = delete;
  Management& operator=(Management&&) = delete;

public:
  bool AssignCall(const std::shared_ptr<Call>& call);
  // Internal assign used by pending worker. If enqueueIfCapacityLimited is true,
  // the call will be enqueued into the pending queue when all elevators are full.
  bool AssignCallInternal(const std::shared_ptr<Call>& call, bool enqueueIfCapacityLimited);

  // Asks every cabin to stop without waiting (Shutdown() waits for them).
  void RequestShutdown();
  void Shutdown();
  void TraceStatistics();
  // The final report as text lines (also used by the web interface).
  std::vector<std::string> BuildReport() const;

  // Puts a cabin out of service or back into service. Returns false if the id is unknown.
  bool SetOutOfOrder(const std::string& elevatorId, bool outOfOrder);
  // Calls waiting for a cabin with enough capacity (or for a cabin in service).
  std::size_t PendingCallsCount() const;

  struct EnergyTotals
  {
    double travelWh{ 0.0 };    // motors and doors of every cabin
    double standbyWh{ 0.0 };   // standby power of every cabin since the start of the run
    double TotalWh() const { return travelWh + standbyWh; }
  };

  EnergyTotals GetEnergy() const;

  // Groups of cabins (one without zones) and the group of every cabin, in the order of the snapshots.
  const std::vector<ElevatorGroup>& GetGroups() const { return m_groups; }
  const std::vector<int>& GetElevatorGroups() const { return m_elevatorGroup; }

  std::vector<ElevatorSnapshot> GetElevatorSnapshots() const;

private:
  void EnqueuePendingCall(const std::shared_ptr<Call>& call);
  void RequestPendingRetry();
  void SendImpatientToTheStairs();

  // Peak parking: recent assignments of every group, and the detection of a rush from its lobby.
  void RecordPeakTraffic(int group, const std::shared_ptr<Call>& call);
  void UpdatePeaks();

private:
  std::vector<std::unique_ptr<Elevator>> m_elevators;
  std::vector<ElevatorGroup> m_groups;
  std::vector<int> m_elevatorGroup;

  struct PeakTraffic
  {
    // Simulated time of every assignment of the group, and its kind: +1 up from the lobby, -1 down to the lobby, 0 other.
    std::deque<std::pair<long long, int>> recent;
    bool active{ false };
  };

  std::mutex m_peakMutex;
  std::vector<PeakTraffic> m_peaks;   // one per group
  long long m_lastPeakUpdateMs{ -1 };
  std::mutex m_assignMutex;
  bool m_shutdownCompleted{ false };

  // Pending calls queue: when all elevators are full the call is enqueued here
  // and a background worker retries assignments without blocking caller threads.
  std::deque<std::shared_ptr<Call>> m_pendingCalls;
  mutable std::mutex m_pendingMutex;
  std::condition_variable m_pendingCond;
  std::unique_ptr<std::thread> m_pendingThread;
  std::atomic_bool m_pendingThreadRunning{ false };
  bool m_retryRequested{ false };   // guarded by m_pendingMutex: a retry was requested by an event (capacity freed, call refused)

  Log m_log;
};
