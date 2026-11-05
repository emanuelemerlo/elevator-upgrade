#include "Management.h"

#include "Elevator.h"
#include "Configuration.h"
#include "ConsoleView.h"
#include "LogBase.h"
#include "LogFileSink.h"
#include "Statistics.h"
#include "AssignmentOptimizer.h"
#include "EventBus.h"
#include "SimulationClock.h"
#include "Energy.h"
#include "BuildingContext.h"
#include "Zoning.h"

#include <map>
#include <random>
#include <set>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace
{
  struct AssignmentCandidate
  {
    Elevator* elevator{ nullptr };
    AssignmentOptimizer::Evaluation evaluation;
    double wear{ 0.0 };
    double score{ std::numeric_limits<double>::max() };
    std::size_t committedLoad{ 0U };
    bool otherGroup{ false };   // the cabin belongs to a group that does not serve this leg
  };

  double WearScore(const Elevator& elevator)
  {
    // Wear is only a tie-breaker: it is normalized among the candidates and capped (see AssignCallInternal),
    // so that waiting time always dominates but long-term ties drift toward less-used cars.
    return
      static_cast<double>(elevator.GetTotalFloorsTravelled()) * 0.75 +
      static_cast<double>(elevator.GetRunCounter()) * 8.0 +
      static_cast<double>(elevator.GetDoorCycles()) * 12.0;
  }

  AssignmentOptimizer::Timing CurrentTiming()
  {
    AssignmentOptimizer::Timing timing;
    timing.floorMs = static_cast<double>(Configuration::Elevator::TimeToReachTheNextFloor().count());
    timing.expressFloorMs = static_cast<double>(Configuration::Elevator::TimeToPassAnExpressFloor().count());
    timing.doorMs = static_cast<double>(Configuration::Elevator::DoorTransitionTime().count());
    timing.dwellMs = static_cast<double>(Configuration::Elevator::EnterAndExitTime().count());
    return timing;
  }

  AssignmentOptimizer::Weights CurrentWeights(const AssignmentOptimizer::Timing& timing)
  {
    AssignmentOptimizer::Weights weights;

    // A passenger is considered "old" after waiting about four times the time of a full trip of the building.
    const auto fullTripMs = static_cast<double>(Floors::TotalFloors()) * timing.floorMs + 2.0 * timing.doorMs + timing.dwellMs;
    weights.agingMs = std::max(1.0, 4.0 * fullTripMs);
    return weights;
  }

  // Passengers already committed to every cabin: the ones inside plus the ones assigned and still waiting.
  std::vector<std::vector<AssignmentOptimizer::Passenger>> CollectCommittedPassengers(
    const std::vector<std::unique_ptr<Elevator>>& elevators,
    std::vector<ElevatorDispatchState>& states)
  {
    // The waiting list is read before the cabins: a passenger boarding in between is then seen twice
    // (and deduplicated below) instead of being missed.
    const auto waiting = Floors::GetPeople().Snapshot();

    std::map<std::string, std::vector<std::shared_ptr<Call>>> waitingByElevator;
    for (const auto& person : waiting)
      waitingByElevator[person->GetAssignedElevator()].push_back(person);

    std::vector<std::vector<AssignmentOptimizer::Passenger>> committed(elevators.size());
    states.clear();
    states.reserve(elevators.size());

    for (std::size_t index = 0U; index < elevators.size(); ++index)
    {
      states.push_back(elevators[index]->GetDispatchState());
      const auto& state = states.back();

      std::set<const Call*> onboard;
      for (const auto& person : state.onboard)
      {
        onboard.insert(person.get());
        committed[index].push_back({ person->GetStartFloor(), person->GetDestinationFloor(), true, 0.0 });
      }

      const auto assigned = waitingByElevator.find(elevators[index]->GetId());
      if (assigned == waitingByElevator.end())
        continue;

      for (const auto& person : assigned->second)
      {
        if (onboard.count(person.get()) != 0U)
          continue;

        const auto waitedMs = static_cast<double>(person->GetWaitingTime().count());
        committed[index].push_back({ person->GetStartFloor(), person->GetDestinationFloor(), false, waitedMs });
      }
    }

    return committed;
  }

  std::string Separator(const std::size_t width)
  {
    return std::string(width, '-');
  }

  std::size_t TableWidth(const std::initializer_list<std::size_t> columnWidths)
  {
    std::size_t width = 1U;
    for (const auto columnWidth : columnWidths)
      width += columnWidth + 3U;

    return width;
  }

  std::vector<std::string> WrapCell(const std::string& value, const std::size_t width)
  {
    std::vector<std::string> lines;
    std::string remaining = value;

    while (remaining.size() > width)
    {
      auto splitPosition = remaining.rfind(' ', width);
      if (splitPosition == std::string::npos || splitPosition == 0U)
        splitPosition = width;

      lines.push_back(remaining.substr(0U, splitPosition));

      const auto nextPosition = remaining.find_first_not_of(' ', splitPosition);
      remaining = nextPosition == std::string::npos ? "" : remaining.substr(nextPosition);
    }

    lines.push_back(remaining);
    return lines;
  }

  void AddTableRow(
    std::vector<std::string>& report,
    const std::vector<std::string>& cells,
    const std::vector<std::size_t>& widths,
    const std::vector<bool>& rightAlign)
  {
    std::vector<std::vector<std::string>> wrappedCells;
    wrappedCells.reserve(cells.size());

    std::size_t rowHeight = 1U;
    for (std::size_t index = 0U; index < cells.size(); ++index)
    {
      wrappedCells.push_back(WrapCell(cells[index], widths[index]));
      rowHeight = std::max(rowHeight, wrappedCells.back().size());
    }

    for (std::size_t lineIndex = 0U; lineIndex < rowHeight; ++lineIndex)
    {
      std::stringstream row;
      row << "|";

      for (std::size_t cellIndex = 0U; cellIndex < cells.size(); ++cellIndex)
      {
        const auto cellLine =
          lineIndex < wrappedCells[cellIndex].size() ? wrappedCells[cellIndex][lineIndex] : "";

        row << " ";
        if (rightAlign[cellIndex])
          row << std::right;
        else
          row << std::left;

        row << std::setw(static_cast<int>(widths[cellIndex])) << cellLine << " |";
      }

      report.push_back(row.str());
    }
  }

  void AddSeparator(std::vector<std::string>& report, const std::initializer_list<std::size_t> columnWidths)
  {
    report.push_back(Separator(TableWidth(columnWidths)));
  }

  std::string FormatSeconds(const double milliseconds)
  {
    std::stringstream value;
    value << std::fixed << std::setprecision(2) << milliseconds / 1000.0 << " s";
    return value.str();
  }

  std::string FormatMilliseconds(const double milliseconds)
  {
    std::stringstream value;
    value << std::fixed << std::setprecision(3) << milliseconds << " ms";
    return value.str();
  }

  std::string FormatRatio(const double ratio)
  {
    std::stringstream value;
    value << std::fixed << std::setprecision(2) << ratio << "x";
    return value.str();
  }

  std::string FormatPercent(const double ratio)
  {
    std::stringstream value;
    value << std::fixed << std::setprecision(1) << ratio * 100.0 << "%";
    return value.str();
  }

  std::uint64_t AssignedCallsForElevator(
    const StatisticsSnapshot& statistics,
    const std::string& elevatorId)
  {
    const auto assignment = std::find_if(
      statistics.elevatorAssignments.begin(),
      statistics.elevatorAssignments.end(),
      [&elevatorId](const ElevatorAssignmentStatistics& item) { return item.elevatorId == elevatorId; });

    return assignment == statistics.elevatorAssignments.end() ? 0U : assignment->assignedCalls;
  }

  std::uint64_t CountUnassignedElevators(
    const StatisticsSnapshot& statistics,
    const std::vector<std::unique_ptr<Elevator>>& elevators)
  {
    return static_cast<std::uint64_t>(std::count_if(
      elevators.begin(),
      elevators.end(),
      [&statistics](const std::unique_ptr<Elevator>& elevator)
      {
        return AssignedCallsForElevator(statistics, elevator->GetId()) == 0U;
      }));
  }

  struct FleetSizingRecommendation
  {
    unsigned int recommendedElevators{ 1U };
    std::string signal;
    std::string reason;
    double targetAverageWaitMs{ 0.0 };
    double forcedAssignmentRatio{ 0.0 };
    double noAvailableRatio{ 0.0 };
    std::uint64_t unassignedElevators{ 0U };
  };

  struct CabinSizingRecommendation
  {
    unsigned int recommendedMaxPeople{ 1U };
    std::string signal;
    std::string reason;
  };

  CabinSizingRecommendation BuildCabinSizingRecommendation(const StatisticsSnapshot& statistics)
  {
    CabinSizingRecommendation recommendation;
    const auto configuredMaxPeople = Configuration::Elevator::MaxPeople();
    const auto observedPeak = std::max(statistics.maxCabinPeople, statistics.maxCommittedElevatorLoad);

    recommendation.recommendedMaxPeople = std::max(1U, configuredMaxPeople);

    if (statistics.capacityLimitedDecisions > 0U)
    {
      recommendation.recommendedMaxPeople = std::max<unsigned int>(
        configuredMaxPeople + 1U,
        static_cast<unsigned int>(observedPeak + 1U));
      recommendation.signal = "Increase";
      recommendation.reason = "cabins reached full load";
    }
    else if (observedPeak > 0U && observedPeak < configuredMaxPeople * 0.5 && configuredMaxPeople > 1U)
    {
      recommendation.recommendedMaxPeople = std::max<unsigned int>(1U, static_cast<unsigned int>(observedPeak + 1U));
      recommendation.signal = "Can reduce";
      recommendation.reason = "observed load stayed low";
    }
    else
    {
      recommendation.signal = "Keep";
      recommendation.reason = "cabin capacity looks balanced";
    }

    return recommendation;
  }

  FleetSizingRecommendation BuildFleetSizingRecommendation(
    const StatisticsSnapshot& statistics,
    const std::vector<std::unique_ptr<Elevator>>& elevators,
    const std::uint64_t pendingBoarding)
  {
    FleetSizingRecommendation recommendation;
    const auto currentElevators = static_cast<unsigned int>(elevators.size());
    recommendation.recommendedElevators = std::max(1U, currentElevators);

    const auto assignmentDecisions = static_cast<double>(std::max<std::uint64_t>(1U, statistics.assignmentDecisions));
    recommendation.forcedAssignmentRatio = static_cast<double>(statistics.forcedAssignments) / assignmentDecisions;
    recommendation.noAvailableRatio = static_cast<double>(statistics.noAvailableElevatorDecisions) / assignmentDecisions;
    recommendation.unassignedElevators = CountUnassignedElevators(statistics, elevators);

    const auto travelMs = static_cast<double>(Configuration::Elevator::TimeToReachTheNextFloor().count());
    const auto stopMs = static_cast<double>(Configuration::Elevator::EnterAndExitTime().count());
    recommendation.targetAverageWaitMs = std::max(5000.0, travelMs * 2.0 + stopMs);

    unsigned int pressureScore = 0U;
    if (pendingBoarding > 0U)
      pressureScore += 2U;
    if (statistics.AverageWaitTimeMs() > recommendation.targetAverageWaitMs)
      ++pressureScore;
    if (statistics.maxWaitTimeMs > static_cast<std::uint64_t>(recommendation.targetAverageWaitMs * 3.0))
      ++pressureScore;
    // Reversing the sweep to serve a call is normal in a busy building, so the forced ratio is only reported:
    // the saturation signal is the share of decisions where every car had to reverse.
    if (recommendation.noAvailableRatio > 0.05)
      ++pressureScore;
    if (recommendation.noAvailableRatio > 0.20)
      ++pressureScore;

    const auto canConsiderReduction =
      currentElevators > 1U &&
      pendingBoarding == 0U &&
      statistics.noAvailableElevatorDecisions == 0U &&
      statistics.AverageWaitTimeMs() < recommendation.targetAverageWaitMs * 0.5 &&
      recommendation.unassignedElevators > 0U;

    // The recommendation is intentionally conservative: one simulation run is a
    // sample, not a capacity study. Increase on sustained pressure, reduce only
    // when service is comfortable and at least one elevator did no useful work.
    if (pressureScore >= 5U)
    {
      recommendation.recommendedElevators = currentElevators + 2U;
      recommendation.signal = "Increase strongly";
      recommendation.reason = "high wait/backlog pressure";
    }
    else if (pressureScore >= 3U)
    {
      recommendation.recommendedElevators = currentElevators + 1U;
      recommendation.signal = "Increase";
      recommendation.reason = "service pressure observed";
    }
    else if (canConsiderReduction)
    {
      recommendation.recommendedElevators = currentElevators - 1U;
      recommendation.signal = "Can reduce";
      recommendation.reason = "spare elevator capacity";
    }
    else
    {
      recommendation.signal = "Keep";
      recommendation.reason = "capacity looks balanced";
    }

    return recommendation;
  }

  void WriteFinalReportToFile(const std::vector<std::string>& report)
  {
    LogFileSink::WriteLine("");
    LogFileSink::WriteLines(report);
  }
}

Management::Management(const unsigned int numberOfElevators)
{
  // Groups of cabins: one group serving every floor, or the zones and shuttles of a tall building (see Zoning).
  m_groups = Zoning::Groups(Configuration::Get());

  for (std::size_t groupIndex = 0U; groupIndex < m_groups.size(); ++groupIndex)
  {
    const auto& group = m_groups[groupIndex];

    // Cabins of the floors it serves, in order: they give the home floors of the spread parking.
    std::vector<Floors::FloorNumber> servedFloors;
    for (Floors::FloorNumber floor = 0U; floor < group.served.size(); ++floor)
      if (group.served[floor])
        servedFloors.push_back(floor);

    for (auto cabin = 0U; cabin < group.cabins; ++cabin)
    {
      // A, B, C... as in the original program without zones; A1, A2... B1... S1... with zones.
      const auto elevatorId = group.name.empty()
        ? std::string(1U, static_cast<char>('A' + m_elevators.size()))
        : group.name + std::to_string(cabin + 1U);

      m_elevators.push_back(std::make_unique<Elevator>(
        elevatorId, group.name.empty() ? std::vector<bool>() : group.served, group.lobby, group.name));
      m_elevatorGroup.push_back(static_cast<int>(groupIndex));

      // A passenger refused by a full cabin must be dispatched again; freed capacity wakes up the pending calls.
      m_elevators.back()->SetRefusedCallHandler([this](const std::shared_ptr<Call>& call) { EnqueuePendingCall(call); });
      m_elevators.back()->SetCapacityFreedHandler([this]() { RequestPendingRetry(); });

      // Idle parking: where every cabin waits when it has nothing to do, within the floors of its group.
      switch (Configuration::Dispatch::IdleParking())
      {
      case Configuration::Dispatch::Parking::Lobby:
        m_elevators.back()->SetHomeFloor(group.lobby);
        break;

      case Configuration::Dispatch::Parking::Spread:
      {
        // Home floors in the middle of equal parts of the floors served: 4 cabins in 20 floors wait at 2, 7, 12, 17.
        const auto part = static_cast<double>(servedFloors.size()) / static_cast<double>(group.cabins);
        const auto position = std::min(servedFloors.size() - 1U, static_cast<std::size_t>((static_cast<double>(cabin) + 0.5) * part));
        m_elevators.back()->SetHomeFloor(servedFloors[position]);
        break;
      }

      case Configuration::Dispatch::Parking::Stay:
      default:
        break;
      }
    }
  }

  (void)numberOfElevators; // the cabins come from the groups (equal to the configured number without zones)

  m_peaks.resize(m_groups.size());

  m_log.SetTraceId("Management");

  // Start pending calls worker
  m_pendingThreadRunning = true;
  auto* context = &Context::Current();
  m_pendingThread = std::make_unique<std::thread>([this, context]() {
    Context::Scope scope(context);

    // Retry period (real time) when no event wakes the worker up earlier: retrying is cheap and harmless.
    // Short, because it is also the period of the patience check: at high speed 5 ms are several simulated seconds.
    const auto fallbackRetry = std::chrono::milliseconds(5);

    std::unique_lock<std::mutex> lock(m_pendingMutex);

    while (m_pendingThreadRunning)
    {
      m_pendingCond.wait_for(lock, fallbackRetry, [this]() { return m_retryRequested || !m_pendingThreadRunning; });
      m_retryRequested = false;

      // Passengers who have waited longer than their patience give up and take the stairs.
      SendImpatientToTheStairs();

      // Peak parking: start or end the rush of every lobby (without the pending lock: it wakes the cabins).
      if (Configuration::Dispatch::IdleParking() == Configuration::Dispatch::Parking::Peak)
      {
        lock.unlock();
        UpdatePeaks();
        lock.lock();
      }

      if (m_pendingCalls.empty() || !m_pendingThreadRunning)
        continue;

      // Work on a copy, oldest call first, without holding the pending mutex (producers must not be blocked).
      // Every call is tried: a call that does not fit must not hold back the ones that do.
      const std::vector<std::shared_ptr<Call>> attempt(m_pendingCalls.begin(), m_pendingCalls.end());
      lock.unlock();

      std::vector<std::shared_ptr<Call>> assigned;
      for (const auto& call : attempt)
      {
        if (!m_pendingThreadRunning)
          break;

        // The call is already in the queue: it must not be enqueued again if the cabins are still full.
        if (AssignCallInternal(call, false))
          assigned.push_back(call);
      }

      lock.lock();

      for (const auto& call : assigned)
        m_pendingCalls.erase(std::remove(m_pendingCalls.begin(), m_pendingCalls.end(), call), m_pendingCalls.end());
    }
  });
}

namespace
{
  // A rush from a lobby, over the assignments of the group in the last 10 simulated minutes: at least 5 went up from
  // its lobby, at least 40% of all of them, and at least twice as many as the ones going down to the lobby. The last
  // rule matters with zones: in a high zone every trip passes the lobby (also the changes of cabin), so the trips up
  // from the lobby are always many; only a clear imbalance means a rush (offices in the morning, homes in the
  // evening), not lunch or the ordinary traffic. The rush goes on while at least 2 go up, they are at least 25% of the
  // assignments and not fewer than the ones going down.
  constexpr long long PeakWindowMs = 10LL * 60LL * 1000LL;
  constexpr long long PeakUpdatePeriodMs = 30LL * 1000LL;
  constexpr std::size_t PeakStartCount = 5U;
  constexpr double PeakStartShare = 0.4;
  constexpr double PeakStartRatio = 2.0;
  constexpr std::size_t PeakKeepCount = 2U;
  constexpr double PeakKeepShare = 0.25;
  constexpr double PeakKeepRatio = 1.0;
}

void Management::RecordPeakTraffic(const int group, const std::shared_ptr<Call>& call)
{
  if (Configuration::Dispatch::IdleParking() != Configuration::Dispatch::Parking::Peak || group < 0)
    return;

  const auto lobby = m_groups[static_cast<std::size_t>(group)].lobby;
  int kind = 0;
  if (call->GetStartFloor() == lobby && call->GetDestinationFloor() > lobby)
    kind = 1;
  else if (call->GetDestinationFloor() == lobby && call->GetStartFloor() > lobby)
    kind = -1;

  std::lock_guard<std::mutex> lock(m_peakMutex);
  m_peaks[static_cast<std::size_t>(group)].recent.emplace_back(SimulationClock::Now(), kind);
}

void Management::UpdatePeaks()
{
  const auto now = SimulationClock::Now();

  std::vector<std::pair<std::size_t, bool>> changes;
  {
    std::lock_guard<std::mutex> lock(m_peakMutex);

    if (m_lastPeakUpdateMs >= 0 && now - m_lastPeakUpdateMs < PeakUpdatePeriodMs)
      return;
    m_lastPeakUpdateMs = now;

    for (std::size_t group = 0U; group < m_peaks.size(); ++group)
    {
      auto& peak = m_peaks[group];
      while (!peak.recent.empty() && now - peak.recent.front().first > PeakWindowMs)
        peak.recent.pop_front();

      const auto total = peak.recent.size();
      const auto up = static_cast<std::size_t>(std::count_if(peak.recent.begin(), peak.recent.end(),
        [](const std::pair<long long, int>& item) { return item.second > 0; }));
      const auto down = static_cast<std::size_t>(std::count_if(peak.recent.begin(), peak.recent.end(),
        [](const std::pair<long long, int>& item) { return item.second < 0; }));
      const auto share = total == 0U ? 0.0 : static_cast<double>(up) / static_cast<double>(total);
      const auto ratio = static_cast<double>(up) / static_cast<double>(std::max<std::size_t>(1U, down));

      const auto active = peak.active
        ? up >= PeakKeepCount && share >= PeakKeepShare && ratio >= PeakKeepRatio
        : up >= PeakStartCount && share >= PeakStartShare && ratio >= PeakStartRatio;

      if (active != peak.active)
      {
        peak.active = active;
        changes.emplace_back(group, active);
      }
    }
  }

  for (const auto& change : changes)
  {
    const auto& group = m_groups[change.first];

    // During the rush the idle cabins of the group wait at its lobby; afterwards they stay where they stop.
    for (std::size_t index = 0U; index < m_elevators.size(); ++index)
      if (m_elevatorGroup[index] == static_cast<int>(change.first))
        m_elevators[index]->SetHomeFloor(change.second ? group.lobby : Floors::InvalidFloor);

    m_log.Trace(std::string(change.second ? "Rush" : "End of the rush") + " at floor " + std::to_string(group.lobby) +
      (group.name.empty() ? "" : " for group " + group.name), Log::TraceLevel::Info);

    EventBus::Publish("peak", Json::Writer::Object()
      .Add("group", group.name)
      .Add("lobby", group.lobby)
      .Add("active", change.second));
  }
}

void Management::SendImpatientToTheStairs()
{
  // Called by the pending worker with m_pendingMutex held.
  const auto nowMs = SimulationClock::Now();

  for (const auto& call : Floors::GetPeople().RemoveImpatient(nowMs))
  {
    // A call still waiting for capacity must not be assigned any more. If a cabin was already coming,
    // it will stop at an empty landing, as it happens in real buildings.
    m_pendingCalls.erase(std::remove(m_pendingCalls.begin(), m_pendingCalls.end(), call), m_pendingCalls.end());
    Statistics::RecordAbandonedCall();

    m_log.Trace("Call " + call->ToString() + " took the stairs", Log::TraceLevel::Verbose);

    EventBus::Publish("stairs", Json::Writer::Object()
      .Add("id", static_cast<unsigned long long>(call->GetId()))
      .Add("floor", call->GetStartFloor())
      .Add("to", call->GetDestinationFloor())
      .Add("elevator", call->GetAssignedElevator())
      .Add("waitMs", static_cast<long long>(call->GetWaitingTime().count())));
  }
}

void Management::EnqueuePendingCall(const std::shared_ptr<Call>& call)
{
  {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingCalls.push_back(call);
    m_retryRequested = true;
  }

  m_pendingCond.notify_one();
}

void Management::RequestPendingRetry()
{
  {
    std::lock_guard<std::mutex> lock(m_pendingMutex);

    if (m_pendingCalls.empty())
      return;

    m_retryRequested = true;
  }

  m_pendingCond.notify_one();
}

Management::~Management()
{
  Shutdown();
}

void Management::Shutdown()
{
  if (m_shutdownCompleted || m_elevators.empty())
    return;

  m_log.Trace("Shutdown in progress...", Log::TraceLevel::Verbose);

  // Stop the pending calls worker first: it must not assign calls to elevators that are shutting down.
  {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingThreadRunning = false;
  }
  m_pendingCond.notify_all();
  if (m_pendingThread && m_pendingThread->joinable())
    m_pendingThread->join();
  m_pendingThread.reset();

  // Ask every elevator to stop first, then wait for them: they finish their current step in parallel.
  for (auto& elevator : m_elevators)
    elevator->RequestShutdown();

  for (auto& elevator : m_elevators)
    elevator->ShutDown();

  m_log.Trace("Shutdown completed", Log::TraceLevel::Verbose);
  m_shutdownCompleted = true;
}

void Management::RequestShutdown()
{
  for (auto& elevator : m_elevators)
    elevator->RequestShutdown();
}

bool Management::SetOutOfOrder(const std::string& elevatorId, const bool outOfOrder)
{
  const auto elevator = std::find_if(m_elevators.begin(), m_elevators.end(),
    [&elevatorId](const std::unique_ptr<Elevator>& item) { return item->GetId() == elevatorId; });

  if (elevator == m_elevators.end())
    return false;

  {
    // Under the assignment lock: a decision in progress either completes before the cabin is excluded
    // (and the cabin will hand that call back) or sees the cabin already out of order.
    std::lock_guard<std::mutex> lock(m_assignMutex);
    (*elevator)->SetOutOfOrder(outOfOrder);
  }

  if (!outOfOrder)
    RequestPendingRetry();

  return true;
}

Management::EnergyTotals Management::GetEnergy() const
{
  EnergyTotals totals;
  for (const auto& elevator : m_elevators)
    totals.travelWh += elevator->GetTravelEnergyWh();

  totals.standbyWh = Energy::StandbyWh(m_elevators.size(), SimulationClock::Now());
  return totals;
}

std::size_t Management::PendingCallsCount() const
{
  std::lock_guard<std::mutex> lock(m_pendingMutex);
  return m_pendingCalls.size();
}

bool Management::AssignCall(const std::shared_ptr<Call>& call)
{
  return AssignCallInternal(call, true);
}

bool Management::AssignCallInternal(const std::shared_ptr<Call>& call, bool enqueueIfCapacityLimited)
{
  const auto assignmentStartTime = std::chrono::steady_clock::now();

  // Assignment is serialized because calls are generated asynchronously and each
  // decision reads mutable elevator queues and writes the assigned elevator id.
  std::lock_guard<std::mutex> lock(m_assignMutex);

  // Route of the passenger: the group that takes him now, and where this leg ends (his destination, or a lobby
  // where he changes cabin in a zoned building).
  const auto leg = Zoning::Plan(m_groups, call->GetStartFloor(), call->GetFinalDestination());
  if (leg.group < 0)
    return false;

  call->SetDestinationFloor(leg.to);

  if (!call->IsValid())
    return false;

  const auto timing = CurrentTiming();
  const auto weights = CurrentWeights(timing);
  const auto capacity = static_cast<std::size_t>(Configuration::Elevator::MaxPeople());
  const AssignmentOptimizer::Passenger newPassenger{ call->GetStartFloor(), call->GetDestinationFloor(), false, 0.0 };

  std::vector<ElevatorDispatchState> states;
  const auto committed = CollectCommittedPassengers(m_elevators, states);

  std::vector<AssignmentCandidate> candidates;
  candidates.reserve(m_elevators.size());

  for (std::size_t index = 0U; index < m_elevators.size(); ++index)
  {
    AssignmentCandidate candidate;
    candidate.elevator = m_elevators[index].get();
    candidate.committedLoad = committed[index].size();
    candidate.otherGroup = m_elevatorGroup[index] != leg.group;

    if (states[index].status != ElevatorStatus::OutOfOrder && !candidate.otherGroup)
    {
      const AssignmentOptimizer::CabinState cabin{ states[index].floor, states[index].direction, states[index].doorsOpen,
        states[index].targetFloor, &m_elevators[index]->GetServedFloors() };
      candidate.evaluation = AssignmentOptimizer::EvaluateAssignment(cabin, committed[index], newPassenger, capacity, timing, weights);
      candidate.wear = WearScore(*candidate.elevator);
    }

    candidates.push_back(candidate);
  }

  // Wear leveling: among the cabins able to serve the call, the less worn ones get a tiny bonus,
  // never worth more than the time needed to travel one floor.
  double minWear = std::numeric_limits<double>::max();
  double maxWear = std::numeric_limits<double>::lowest();
  for (const auto& candidate : candidates)
  {
    if (!candidate.evaluation.feasible)
      continue;

    minWear = std::min(minWear, candidate.wear);
    maxWear = std::max(maxWear, candidate.wear);
  }

  AssignmentCandidate* best = nullptr;
  bool onTheWayFound = false;
  const auto strategy = Configuration::Dispatch::DispatchStrategy();

  for (std::size_t index = 0U; index < candidates.size(); ++index)
  {
    auto& candidate = candidates[index];
    if (!candidate.evaluation.feasible)
      continue;

    const auto wearRatio = maxWear > minWear ? (candidate.wear - minWear) / (maxWear - minWear) : 0.0;
    const auto distance = static_cast<double>(std::max(states[index].floor, call->GetStartFloor()) -
      std::min(states[index].floor, call->GetStartFloor()));

    switch (strategy)
    {
    case Configuration::Dispatch::Strategy::Nearest:
      // Closest cabin, whatever it is doing; wear only breaks the ties.
      candidate.score = distance + wearRatio * 0.01;
      break;

    case Configuration::Dispatch::Strategy::Collective:
      // Closest among the idle cabins and those already sweeping towards the call; a cabin that has to turn around
      // first counts as if it were a whole building height further away.
      candidate.score = distance + (candidate.evaluation.onTheWay ? 0.0 : 2.0 * static_cast<double>(Floors::TotalFloors())) +
        wearRatio * 0.01;
      break;

    case Configuration::Dispatch::Strategy::Optimized:
    default:
      candidate.score = candidate.evaluation.marginalCostMs + wearRatio * timing.floorMs;
      break;
    }

    onTheWayFound = onTheWayFound || candidate.evaluation.onTheWay;

    if (best == nullptr || candidate.score < best->score)
      best = &candidate;
  }

  if (best == nullptr)
  {
    // Every cabin would exceed its capacity (or is out of order): the call waits in the pending queue.
    if (enqueueIfCapacityLimited)
    {
      std::size_t inService = 0U;
      for (std::size_t index = 0U; index < states.size(); ++index)
        if (states[index].status != ElevatorStatus::OutOfOrder && m_elevatorGroup[index] == leg.group)
          ++inService;

      // The event is counted once per call: retries made by the pending worker are not new events.
      // With every cabin out of order the wait is not caused by the capacity.
      if (inService > 0)
        Statistics::RecordCapacityLimitedDecision();

      EnqueuePendingCall(call);

      EventBus::Publish("pending", Json::Writer::Object()
        .Add("id", static_cast<unsigned long long>(call->GetId()))
        .Add("reason", inService > 0 ? "full" : "noService"));
    }

    return false;
  }

  std::stringstream message;
  message << "Call " << call->ToString() << " assigned to elevator: " << best->elevator->GetId()
          << " (pickup in " << std::fixed << std::setprecision(1) << best->evaluation.pickupEtaMs / 1000.0 << " s"
          << (best->evaluation.onTheWay ? ", on the way" : ", after reversing") << ")";
  m_log.Trace(message);

  if (Configuration::Log::TraceLevel() == ILog::TraceLevel::Debug)
  {
    // Everything the prediction was based on, to compare it with what the cabin really does.
    const auto index = static_cast<std::size_t>(best - candidates.data());
    std::stringstream details;
    details << "Dispatch state of " << best->elevator->GetId() << ": floor " << states[index].floor
            << " dir " << static_cast<int>(states[index].direction) << " doors " << (states[index].doorsOpen ? "open" : "closed")
            << " committed:";
    for (const auto& passenger : committed[index])
      details << " [" << passenger.from << ">" << passenger.to << (passenger.onboard ? " in" : " wait") << "]";
    m_log.Trace(details, Log::TraceLevel::Debug);
  }

  call->SetAssignedElevator(best->elevator->GetId());
  Statistics::RecordCommittedElevatorLoad(best->committedLoad + 1U);
  const auto callAssigned = best->elevator->AnswerToCall(call);

  if (callAssigned)
  {
    Statistics::RecordAssignedCall(best->elevator->GetId());
    RecordPeakTraffic(leg.group, call);

    // The whole decision is published: the cost of every cabin shows why this one has been chosen.
    auto candidatesJson = Json::Writer::Array();
    for (std::size_t index = 0U; index < candidates.size(); ++index)
    {
      const auto& candidate = candidates[index];
      if (candidate.otherGroup)
        continue; // only the cabins of the group that serves this leg are candidates

      auto item = Json::Writer::Object();
      item.Add("elevator", candidate.elevator->GetId());

      if (states[index].status == ElevatorStatus::OutOfOrder)
        item.Add("state", "outOfOrder");
      else if (!candidate.evaluation.feasible)
        item.Add("state", "full");
      else
        item.Add("state", "ok")
            .Add("score", candidate.score, 2)
            .Add("costMs", candidate.evaluation.marginalCostMs, 0)
            .Add("pickupMs", candidate.evaluation.pickupEtaMs, 0)
            .Add("onTheWay", candidate.evaluation.onTheWay);

      candidatesJson.Push(item);
    }

    EventBus::Publish("assign", Json::Writer::Object()
      .Add("id", static_cast<unsigned long long>(call->GetId()))
      .Add("from", call->GetStartFloor())
      .Add("to", call->GetDestinationFloor())
      .Add("elevator", best->elevator->GetId())
      .Add("pickupMs", best->evaluation.pickupEtaMs, 0)
      .Add("dropoffMs", best->evaluation.dropoffEtaMs, 0)
      .Add("onTheWay", best->evaluation.onTheWay)
      .Add("retry", !enqueueIfCapacityLimited)
      .Add("strategy", Configuration::Dispatch::StrategyName(strategy))
      .Add("finalTo", call->GetFinalDestination())
      .Add("group", m_groups[static_cast<std::size_t>(leg.group)].name)
      .Add("candidates", candidatesJson));
  }

  // "Forced": no cabin could pick the call up without reversing its sweep first.
  Statistics::RecordAssignmentDecision(
    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - assignmentStartTime),
    !best->evaluation.onTheWay,
    onTheWayFound);

  return callAssigned;
}

void Management::TraceStatistics()
{
  const auto report = BuildReport();

  LogBase::FlushPendingMessages();

  if (Configuration::Log::WriteToScreen())
    ConsoleView::Instance().WriteFinalReport(report);

  if (Configuration::Log::WriteToFile())
    WriteFinalReportToFile(report);
}

std::vector<std::string> Management::BuildReport() const
{
  const auto statistics = Statistics::GetSnapshot();
  const auto waitDistribution = Statistics::GetWaitDistribution();
  std::vector<std::string> report;

  // Build the final report as a complete buffer and render it outside the rolling
  // log window, otherwise long tables can be truncated by the normal 15-line log.
  report.push_back("Simulation statistics");
  AddSeparator(report, { 22U, 17U });
  AddTableRow(report, { "Metric", "Value" }, { 22U, 17U }, { false, false });
  AddSeparator(report, { 22U, 17U });

  const auto addGlobalStat = [&report](const std::string& metric, const std::string& value)
  {
    AddTableRow(report, { metric, value }, { 22U, 17U }, { false, true });
  };

  addGlobalStat("Queued calls", std::to_string(statistics.queuedCalls));
  addGlobalStat("Assigned calls", std::to_string(statistics.assignedCalls));
  addGlobalStat("Boarded passengers", std::to_string(statistics.boardedPassengers));
  addGlobalStat("Completed passengers", std::to_string(statistics.completedPassengers));
  addGlobalStat("Served quickly", std::to_string(statistics.happyPassengers));
  addGlobalStat("Took the stairs", std::to_string(statistics.abandonedCalls));
  addGlobalStat("Changes of cabin", std::to_string(statistics.transfers));
  addGlobalStat("Wait at the change", FormatSeconds(statistics.AverageTransferWaitMs()));
  addGlobalStat("Simulation time", Configuration::Simulation::CurrentDayTimeLabel());
  addGlobalStat("Average wait", FormatSeconds(statistics.AverageWaitTimeMs()));
  addGlobalStat("Max wait", FormatSeconds(static_cast<double>(statistics.maxWaitTimeMs)));
  addGlobalStat("Wait p50", FormatSeconds(waitDistribution.p50Ms));
  addGlobalStat("Wait p95", FormatSeconds(waitDistribution.p95Ms));
  addGlobalStat("Wait p99", FormatSeconds(waitDistribution.p99Ms));
  addGlobalStat("Average journey", FormatSeconds(statistics.AverageJourneyTimeMs()));
  addGlobalStat("Max journey", FormatSeconds(static_cast<double>(statistics.maxJourneyTimeMs)));

  const auto energy = GetEnergy();
  const auto formatEnergy = [](const double value, const char* unit)
  {
    std::stringstream text;
    text << std::fixed << std::setprecision(2) << value << " " << unit;
    return text.str();
  };

  addGlobalStat("Energy, travel", formatEnergy(energy.travelWh / 1000.0, "kWh"));
  addGlobalStat("Energy, standby", formatEnergy(energy.standbyWh / 1000.0, "kWh"));
  addGlobalStat("Energy per trip", statistics.completedPassengers == 0U ? "n/a" :
    formatEnergy(energy.TotalWh() / static_cast<double>(statistics.completedPassengers), "Wh"));

  AddSeparator(report, { 22U, 17U });
  report.push_back("");

  report.push_back("Algorithm anomaly indicators");
  AddSeparator(report, { 29U, 17U, 5U, 18U });
  AddTableRow(report, { "Indicator", "Value", "Level", "Notes" }, { 29U, 17U, 5U, 18U }, { false, false, false, false });
  AddSeparator(report, { 29U, 17U, 5U, 18U });

  const auto addIndicator = [&report](
    const std::string& indicator,
    const std::string& value,
    const std::string& level,
    const std::string& notes)
  {
    AddTableRow(report, { indicator, value, level, notes }, { 29U, 17U, 5U, 18U }, { false, true, false, false });
  };

  // Passengers who took the stairs are not waiting any more.
  const auto closedBeforeBoarding = statistics.boardedPassengers + statistics.abandonedCalls;
  const auto pendingBoarding =
    statistics.queuedCalls > closedBeforeBoarding ? statistics.queuedCalls - closedBeforeBoarding : 0U;
  const auto pendingCompletion =
    statistics.boardedPassengers > statistics.completedPassengers ? statistics.boardedPassengers - statistics.completedPassengers : 0U;
  const auto maxWaitRatio =
    statistics.AverageWaitTimeMs() <= 0.0 ? 0.0 : static_cast<double>(statistics.maxWaitTimeMs) / statistics.AverageWaitTimeMs();
  const auto fleetSizing = BuildFleetSizingRecommendation(statistics, m_elevators, pendingBoarding);
  const auto cabinSizing = BuildCabinSizingRecommendation(statistics);

  // These indicators are intentionally heuristic. They are meant to point the
  // operator toward suspicious behavior, not to prove that the dispatch is wrong.
  // Calls still waiting on the floors when the report is printed: the oldest one tells whether somebody is starving.
  std::uint64_t oldestWaitingMs = 0U;
  for (const auto& person : Floors::GetPeople().Snapshot())
    oldestWaitingMs = std::max(oldestWaitingMs, static_cast<std::uint64_t>(std::max(0LL, static_cast<long long>(person->GetWaitingTime().count()))));

  const auto decisions = static_cast<double>(std::max<std::uint64_t>(1U, statistics.assignmentDecisions));
  const auto noAvailableRatio = static_cast<double>(statistics.noAvailableElevatorDecisions) / decisions;

  // Reversing the sweep to serve a call is normal in a busy building: it is context (INFO), not an anomaly.
  addIndicator(
    "Forced assignments",
    std::to_string(statistics.forcedAssignments),
    statistics.forcedAssignments == 0U ? "OK" : "INFO",
    "needed a reversal");

  addIndicator(
    "No available elevator",
    std::to_string(statistics.noAvailableElevatorDecisions),
    noAvailableRatio <= 0.10 ? (statistics.noAvailableElevatorDecisions == 0U ? "OK" : "INFO") : "WARN",
    "all cars reversed");

  addIndicator(
    "Requeued calls",
    std::to_string(statistics.requeuedCalls),
    statistics.requeuedCalls == 0U ? "OK" : "WARN",
    "boarding refused");

  addIndicator(
    "Oldest waiting call",
    FormatSeconds(static_cast<double>(oldestWaitingMs)),
    pendingBoarding == 0U ? "OK" : "WARN",
    "still on a floor");

  addIndicator(
    "Capacity limit waits",
    std::to_string(statistics.capacityLimitedDecisions),
    statistics.capacityLimitedDecisions == 0U ? "OK" : "WARN",
    "full cabins");

  addIndicator(
    "Pending boarding",
    std::to_string(pendingBoarding),
    pendingBoarding == 0U ? "OK" : "WARN",
    "calls waiting");

  addIndicator(
    "Pending completion",
    std::to_string(pendingCompletion),
    pendingCompletion == 0U ? "OK" : "INFO",
    "people in transit");

  addIndicator(
    "Avg assignment time",
    FormatMilliseconds(statistics.AverageAssignmentTimeMs()),
    statistics.AverageAssignmentTimeMs() < 10.0 ? "OK" : "WARN",
    "dispatch timing");

  addIndicator(
    "Max assignment time",
    FormatMilliseconds(static_cast<double>(statistics.maxAssignmentTimeUs) / 1000.0),
    statistics.maxAssignmentTimeUs < 50000U ? "OK" : "WARN",
    "dispatch spike");

  addIndicator(
    "Max wait / avg wait",
    statistics.boardedPassengers == 0U ? "n/a" : FormatRatio(maxWaitRatio),
    maxWaitRatio <= 2.5 ? "OK" : "WARN",
    "tail latency");

  AddSeparator(report, { 29U, 17U, 5U, 18U });
  report.push_back("");

  report.push_back("Fleet sizing recommendation");
  AddSeparator(report, { 29U, 20U, 18U });
  AddTableRow(report, { "Parameter", "Value", "Notes" }, { 29U, 20U, 18U }, { false, false, false });
  AddSeparator(report, { 29U, 20U, 18U });

  const auto addSizingParameter = [&report](
    const std::string& parameter,
    const std::string& value,
    const std::string& notes)
  {
    AddTableRow(report, { parameter, value, notes }, { 29U, 20U, 18U }, { false, true, false });
  };

  addSizingParameter("Configured elevators", std::to_string(Configuration::Building::NumberOfElevators()), "config input");
  addSizingParameter("Recommended elevators", std::to_string(fleetSizing.recommendedElevators), fleetSizing.signal);
  addSizingParameter("Sizing reason", fleetSizing.reason, "heuristic");
  addSizingParameter("Target avg wait", FormatSeconds(fleetSizing.targetAverageWaitMs), "service target");
  addSizingParameter("Observed avg wait", FormatSeconds(statistics.AverageWaitTimeMs()), "simulation");
  addSizingParameter("Forced assignment rate", FormatPercent(fleetSizing.forcedAssignmentRatio), "info only");
  addSizingParameter("No-available rate", FormatPercent(fleetSizing.noAvailableRatio), "saturation");
  addSizingParameter("Unassigned elevators", std::to_string(fleetSizing.unassignedElevators), "possible spare");

  AddSeparator(report, { 29U, 20U, 18U });
  report.push_back("");

  report.push_back("Cabin sizing recommendation");
  AddSeparator(report, { 29U, 20U, 18U });
  AddTableRow(report, { "Parameter", "Value", "Notes" }, { 29U, 20U, 18U }, { false, false, false });
  AddSeparator(report, { 29U, 20U, 18U });

  addSizingParameter("Configured max people", std::to_string(Configuration::Elevator::MaxPeople()), "config input");
  addSizingParameter("Recommended max people", std::to_string(cabinSizing.recommendedMaxPeople), cabinSizing.signal);
  addSizingParameter("Sizing reason", cabinSizing.reason, "heuristic");
  addSizingParameter("Peak people on board", std::to_string(statistics.maxCabinPeople), "observed");
  addSizingParameter("Peak committed load", std::to_string(statistics.maxCommittedElevatorLoad), "assigned+boarded");
  addSizingParameter("Capacity limit waits", std::to_string(statistics.capacityLimitedDecisions), "pressure");

  AddSeparator(report, { 29U, 20U, 18U });
  report.push_back("");

  report.push_back("Elevator statistics");
  report.push_back(Separator(103));
  report.push_back("| Elevator | Assigned calls | Travelled floors | Runs | Door cycles | Queued stops | People on board |");
  report.push_back(Separator(103));

  for (const auto& elevator : m_elevators)
  {
    std::stringstream row;
    row << "| " << std::left << std::setw(8) << elevator->GetId()
        << " | " << std::right << std::setw(14) << AssignedCallsForElevator(statistics, elevator->GetId())
        << " | " << std::right << std::setw(16) << elevator->GetTotalFloorsTravelled()
        << " | " << std::setw(4) << elevator->GetRunCounter()
        << " | " << std::setw(11) << elevator->GetDoorCycles()
        << " | " << std::setw(12) << elevator->GetQueuedStopsCount()
        << " | " << std::setw(15) << elevator->GetPeopleCount()
        << " |";
    report.push_back(row.str());
  }

  report.push_back(Separator(103));
  return report;
}


std::vector<ElevatorSnapshot> Management::GetElevatorSnapshots() const
{
  std::vector<ElevatorSnapshot> snapshots;
  snapshots.reserve(m_elevators.size());

  for (const auto& elevator : m_elevators)
    snapshots.push_back(elevator->GetSnapshot());

  // In the order of creation: the cabins of a group are next to each other (A1, A2... A10, B1...).

  return snapshots;
}
