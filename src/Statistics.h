/**********************************************************************************
*        File: Statistics.h
* Description: Collects simulation statistics.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>

struct ElevatorAssignmentStatistics
{
  std::string elevatorId;
  std::uint64_t assignedCalls{ 0U };
};

struct StatisticsSnapshot
{
  std::uint64_t queuedCalls{ 0U };
  std::uint64_t assignedCalls{ 0U };
  std::uint64_t boardedPassengers{ 0U };
  std::uint64_t completedPassengers{ 0U };
  std::uint64_t totalWaitTimeMs{ 0U };
  std::uint64_t maxWaitTimeMs{ 0U };
  std::uint64_t assignmentDecisions{ 0U };
  std::uint64_t forcedAssignments{ 0U };
  std::uint64_t noAvailableElevatorDecisions{ 0U };
  std::uint64_t capacityLimitedDecisions{ 0U };
  std::uint64_t maxCabinPeople{ 0U };
  std::uint64_t maxCommittedElevatorLoad{ 0U };
  std::uint64_t totalJourneyTimeMs{ 0U };   // call creation -> arrival at destination
  std::uint64_t maxJourneyTimeMs{ 0U };
  std::uint64_t requeuedCalls{ 0U };
  std::uint64_t abandonedCalls{ 0U };      // passengers who gave up waiting and took the stairs        // calls sent back to the dispatcher (e.g. boarding refused by a full cabin)
  std::uint64_t totalAssignmentTimeUs{ 0U };
  std::uint64_t maxAssignmentTimeUs{ 0U };
  std::vector<ElevatorAssignmentStatistics> elevatorAssignments;

  double AverageWaitTimeMs() const;
  double AverageAssignmentTimeMs() const;
  double AverageJourneyTimeMs() const;
};

// Distribution of the passenger waiting times (call creation -> boarding), in simulated milliseconds.
struct WaitDistribution
{
  std::uint64_t samples{ 0U };
  double p50Ms{ 0.0 };
  double p95Ms{ 0.0 };
  double p99Ms{ 0.0 };
};

namespace Statistics
{
  // Statistics of one building (see BuildingContext): every function below works on the building of the calling thread.
  struct Store
  {
    std::mutex mutex;
    StatisticsSnapshot snapshot;
    std::map<std::string, std::uint64_t> assignments;
    std::vector<std::uint32_t> waitSamples;
  };

  void Reset();
  void RecordQueuedCall();
  void RecordAssignedCall(const std::string& elevatorId);
  void RecordAssignmentDecision(std::chrono::microseconds assignmentTime, bool forced, bool availableElevatorFound);
  void RecordCapacityLimitedDecision();
  void RecordCabinPeopleCount(std::size_t cabinPeopleCount);
  void RecordCommittedElevatorLoad(std::size_t committedLoad);
  void RecordBoardedPassenger(std::chrono::milliseconds waitTime);
  void RecordCompletedPassenger(std::chrono::milliseconds journeyTime);
  void RecordRequeuedCall();
  void RecordAbandonedCall();
  // Cheap accessor (no copies) used on hot paths: calls created and not yet completed.
  std::uint64_t ActiveCalls();
  WaitDistribution GetWaitDistribution();
  StatisticsSnapshot GetSnapshot();
}
