#include "Statistics.h"
#include "BuildingContext.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>

namespace
{
  Statistics::Store& CurrentStore() { return Context::Current().GetStatistics(); }

  void RefreshElevatorAssignments()
  {
    // Keep the public snapshot compact while allowing cheap incremental updates
    // to the per-elevator assignment counters during the simulation.
    CurrentStore().snapshot.elevatorAssignments.clear();
    for (const auto& assignment : CurrentStore().assignments)
      CurrentStore().snapshot.elevatorAssignments.push_back({ assignment.first, assignment.second });
  }
}

double StatisticsSnapshot::AverageWaitTimeMs() const
{
  if (boardedPassengers == 0U)
    return 0.0;

  return static_cast<double>(totalWaitTimeMs) / static_cast<double>(boardedPassengers);
}

double StatisticsSnapshot::AverageJourneyTimeMs() const
{
  if (completedPassengers == 0U)
    return 0.0;

  return static_cast<double>(totalJourneyTimeMs) / static_cast<double>(completedPassengers);
}

double StatisticsSnapshot::AverageAssignmentTimeMs() const
{
  if (assignmentDecisions == 0U)
    return 0.0;

  return static_cast<double>(totalAssignmentTimeUs) / static_cast<double>(assignmentDecisions) / 1000.0;
}

void Statistics::Reset()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  CurrentStore().snapshot = StatisticsSnapshot();
  CurrentStore().assignments.clear();
  CurrentStore().waitSamples.clear();
}

void Statistics::RecordQueuedCall()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.queuedCalls;
}

void Statistics::RecordAssignedCall(const std::string& elevatorId)
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.assignedCalls;
  ++CurrentStore().assignments[elevatorId];
}

void Statistics::RecordAssignmentDecision(
  const std::chrono::microseconds assignmentTime,
  const bool forced,
  const bool availableElevatorFound)
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);

  // Dispatch timings are measured around the whole assignment critical section,
  // so spikes include both scoring work and contention on concurrent call bursts.
  ++CurrentStore().snapshot.assignmentDecisions;

  if (forced)
    ++CurrentStore().snapshot.forcedAssignments;

  if (!availableElevatorFound)
    ++CurrentStore().snapshot.noAvailableElevatorDecisions;

  const auto assignmentTimeUs = static_cast<std::uint64_t>(assignmentTime.count());
  CurrentStore().snapshot.totalAssignmentTimeUs += assignmentTimeUs;
  CurrentStore().snapshot.maxAssignmentTimeUs = std::max(CurrentStore().snapshot.maxAssignmentTimeUs, assignmentTimeUs);
}

void Statistics::RecordCapacityLimitedDecision()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.capacityLimitedDecisions;
}

void Statistics::RecordCommittedElevatorLoad(const std::size_t committedLoad)
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  CurrentStore().snapshot.maxCommittedElevatorLoad = std::max(
    CurrentStore().snapshot.maxCommittedElevatorLoad,
    static_cast<std::uint64_t>(committedLoad));
}

void Statistics::RecordCabinPeopleCount(const std::size_t cabinPeopleCount)
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  CurrentStore().snapshot.maxCabinPeople = std::max(
    CurrentStore().snapshot.maxCabinPeople,
    static_cast<std::uint64_t>(cabinPeopleCount));
}

void Statistics::RecordBoardedPassenger(const std::chrono::milliseconds waitTime)
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.boardedPassengers;
  const auto waitTimeMs = static_cast<std::uint64_t>(waitTime.count());
  CurrentStore().snapshot.totalWaitTimeMs += waitTimeMs;
  CurrentStore().snapshot.maxWaitTimeMs = std::max(CurrentStore().snapshot.maxWaitTimeMs, waitTimeMs);
  CurrentStore().waitSamples.push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(waitTimeMs, std::numeric_limits<std::uint32_t>::max())));
}

void Statistics::RecordCompletedPassenger(const std::chrono::milliseconds journeyTime)
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.completedPassengers;

  const auto journeyTimeMs = static_cast<std::uint64_t>(std::max<long long>(0, journeyTime.count()));
  CurrentStore().snapshot.totalJourneyTimeMs += journeyTimeMs;
  CurrentStore().snapshot.maxJourneyTimeMs = std::max(CurrentStore().snapshot.maxJourneyTimeMs, journeyTimeMs);
}

void Statistics::RecordRequeuedCall()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.requeuedCalls;
}

double StatisticsSnapshot::AverageTransferWaitMs() const
{
  if (transferBoardings == 0U)
    return 0.0;

  return static_cast<double>(totalTransferWaitMs) / static_cast<double>(transferBoardings);
}

void Statistics::RecordTransferWait(const std::chrono::milliseconds waitTime)
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.transferBoardings;
  CurrentStore().snapshot.totalTransferWaitMs += static_cast<std::uint64_t>(std::max<long long>(0, waitTime.count()));
}

void Statistics::RecordHappyPassenger()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.happyPassengers;
}

void Statistics::RecordTransfer()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.transfers;
}

void Statistics::RecordAbandonedCall()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  ++CurrentStore().snapshot.abandonedCalls;
}

std::uint64_t Statistics::ActiveCalls()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  const auto closed = CurrentStore().snapshot.completedPassengers + CurrentStore().snapshot.abandonedCalls;
  return CurrentStore().snapshot.queuedCalls > closed ? CurrentStore().snapshot.queuedCalls - closed : 0U;
}

WaitDistribution Statistics::GetWaitDistribution()
{
  std::vector<std::uint32_t> samples;

  {
    std::lock_guard<std::mutex> lock(CurrentStore().mutex);
    samples = CurrentStore().waitSamples;
  }

  WaitDistribution distribution;
  distribution.samples = samples.size();

  if (samples.empty())
    return distribution;

  std::sort(samples.begin(), samples.end());

  const auto percentile = [&samples](const double ratio)
  {
    const auto index = static_cast<std::size_t>(ratio * static_cast<double>(samples.size() - 1U) + 0.5);
    return static_cast<double>(samples[index]);
  };

  distribution.p50Ms = percentile(0.50);
  distribution.p95Ms = percentile(0.95);
  distribution.p99Ms = percentile(0.99);
  return distribution;
}

StatisticsSnapshot Statistics::GetSnapshot()
{
  std::lock_guard<std::mutex> lock(CurrentStore().mutex);
  RefreshElevatorAssignments();
  return CurrentStore().snapshot;
}
