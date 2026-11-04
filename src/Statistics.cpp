#include "Statistics.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>

namespace
{
  std::mutex g_mutex;
  StatisticsSnapshot g_statistics;
  std::map<std::string, std::uint64_t> g_elevatorAssignments;
  std::vector<std::uint32_t> g_waitSamplesMs;

  void RefreshElevatorAssignments()
  {
    // Keep the public snapshot compact while allowing cheap incremental updates
    // to the per-elevator assignment counters during the simulation.
    g_statistics.elevatorAssignments.clear();
    for (const auto& assignment : g_elevatorAssignments)
      g_statistics.elevatorAssignments.push_back({ assignment.first, assignment.second });
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
  std::lock_guard<std::mutex> lock(g_mutex);
  g_statistics = StatisticsSnapshot();
  g_elevatorAssignments.clear();
  g_waitSamplesMs.clear();
}

void Statistics::RecordQueuedCall()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_statistics.queuedCalls;
}

void Statistics::RecordAssignedCall(const std::string& elevatorId)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_statistics.assignedCalls;
  ++g_elevatorAssignments[elevatorId];
}

void Statistics::RecordAssignmentDecision(
  const std::chrono::microseconds assignmentTime,
  const bool forced,
  const bool availableElevatorFound)
{
  std::lock_guard<std::mutex> lock(g_mutex);

  // Dispatch timings are measured around the whole assignment critical section,
  // so spikes include both scoring work and contention on concurrent call bursts.
  ++g_statistics.assignmentDecisions;

  if (forced)
    ++g_statistics.forcedAssignments;

  if (!availableElevatorFound)
    ++g_statistics.noAvailableElevatorDecisions;

  const auto assignmentTimeUs = static_cast<std::uint64_t>(assignmentTime.count());
  g_statistics.totalAssignmentTimeUs += assignmentTimeUs;
  g_statistics.maxAssignmentTimeUs = std::max(g_statistics.maxAssignmentTimeUs, assignmentTimeUs);
}

void Statistics::RecordCapacityLimitedDecision()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_statistics.capacityLimitedDecisions;
}

void Statistics::RecordCommittedElevatorLoad(const std::size_t committedLoad)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  g_statistics.maxCommittedElevatorLoad = std::max(
    g_statistics.maxCommittedElevatorLoad,
    static_cast<std::uint64_t>(committedLoad));
}

void Statistics::RecordCabinPeopleCount(const std::size_t cabinPeopleCount)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  g_statistics.maxCabinPeople = std::max(
    g_statistics.maxCabinPeople,
    static_cast<std::uint64_t>(cabinPeopleCount));
}

void Statistics::RecordBoardedPassenger(const std::chrono::milliseconds waitTime)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_statistics.boardedPassengers;
  const auto waitTimeMs = static_cast<std::uint64_t>(waitTime.count());
  g_statistics.totalWaitTimeMs += waitTimeMs;
  g_statistics.maxWaitTimeMs = std::max(g_statistics.maxWaitTimeMs, waitTimeMs);
  g_waitSamplesMs.push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(waitTimeMs, std::numeric_limits<std::uint32_t>::max())));
}

void Statistics::RecordCompletedPassenger(const std::chrono::milliseconds journeyTime)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_statistics.completedPassengers;

  const auto journeyTimeMs = static_cast<std::uint64_t>(std::max<long long>(0, journeyTime.count()));
  g_statistics.totalJourneyTimeMs += journeyTimeMs;
  g_statistics.maxJourneyTimeMs = std::max(g_statistics.maxJourneyTimeMs, journeyTimeMs);
}

void Statistics::RecordRequeuedCall()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_statistics.requeuedCalls;
}

void Statistics::RecordAbandonedCall()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_statistics.abandonedCalls;
}

std::uint64_t Statistics::ActiveCalls()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto closed = g_statistics.completedPassengers + g_statistics.abandonedCalls;
  return g_statistics.queuedCalls > closed ? g_statistics.queuedCalls - closed : 0U;
}

WaitDistribution Statistics::GetWaitDistribution()
{
  std::vector<std::uint32_t> samples;

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    samples = g_waitSamplesMs;
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
  std::lock_guard<std::mutex> lock(g_mutex);
  RefreshElevatorAssignments();
  return g_statistics;
}
