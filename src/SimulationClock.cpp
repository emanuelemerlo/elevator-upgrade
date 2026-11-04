#include "SimulationClock.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace
{
  typedef std::chrono::steady_clock RealClock;

  constexpr double NoPauseAt = std::numeric_limits<double>::infinity();

  // A thread that wakes up later than this (in real time) does not chain its next sleep to the previous one:
  // it has been doing something else (or waiting for an event) and its time line starts again from now.
  constexpr double CatchUpWindowRealMs = 30.0;

  std::mutex g_mutex;
  std::condition_variable g_changed;

  double g_baseSimulatedMs{ 0.0 };
  RealClock::time_point g_baseRealTime{ RealClock::now() };
  double g_speed{ 1.0 };
  bool g_paused{ false };
  double g_pauseAtMs{ NoPauseAt };   // Step(): the clock pauses by itself when it reaches this time
  bool g_aborted{ false };
  long long g_startTimeOfDayMs{ 0 };
  unsigned int g_generation{ 0U };   // incremented by Reset(): the thread time lines of the previous run are discarded

  struct ThreadTimeline
  {
    unsigned int generation{ std::numeric_limits<unsigned int>::max() };
    double lastTargetMs{ 0.0 };
  };

  thread_local ThreadTimeline t_timeline;
  thread_local const std::atomic_bool* t_abortFlag = nullptr;

  bool ThreadAborted() { return t_abortFlag != nullptr && t_abortFlag->load(); }

  double ClampSpeed(const double speed)
  {
    if (!(speed > 0.0))
      return SimulationClock::MinSpeed;

    return std::min(SimulationClock::MaxSpeed, std::max(SimulationClock::MinSpeed, speed));
  }

  // Requires g_mutex.
  double NowLocked()
  {
    if (g_paused)
      return g_baseSimulatedMs;

    const auto realElapsedMs = std::chrono::duration<double, std::milli>(RealClock::now() - g_baseRealTime).count();
    return std::min(g_pauseAtMs, g_baseSimulatedMs + realElapsedMs * g_speed);
  }

  // Requires g_mutex.
  bool PausedLocked(const double now)
  {
    return g_paused || now >= g_pauseAtMs;
  }

  // Requires g_mutex. Moves the base of the linear mapping to the present, before speed or pause state change.
  void RebaseLocked()
  {
    const auto now = NowLocked();

    g_baseSimulatedMs = now;
    g_baseRealTime = RealClock::now();

    if (now >= g_pauseAtMs)
    {
      g_paused = true;
      g_pauseAtMs = NoPauseAt;
    }
  }
}

void SimulationClock::Reset(const double speed, const long long startTimeOfDayMs)
{
  {
    std::lock_guard<std::mutex> lock(g_mutex);

    g_baseSimulatedMs = 0.0;
    g_baseRealTime = RealClock::now();
    g_speed = ClampSpeed(speed);
    g_paused = false;
    g_pauseAtMs = NoPauseAt;
    g_aborted = false;
    g_startTimeOfDayMs = startTimeOfDayMs;
    ++g_generation;
  }

  g_changed.notify_all();
}

long long SimulationClock::Now()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<long long>(NowLocked());
}

long long SimulationClock::TimeOfDayMs()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_startTimeOfDayMs + static_cast<long long>(NowLocked());
}

long long SimulationClock::StartTimeOfDayMs()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_startTimeOfDayMs;
}

double SimulationClock::Speed()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_speed;
}

void SimulationClock::SetSpeed(const double speed)
{
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    RebaseLocked();
    g_speed = ClampSpeed(speed);
  }

  g_changed.notify_all();
}

bool SimulationClock::Paused()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return PausedLocked(NowLocked());
}

void SimulationClock::Pause()
{
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    RebaseLocked();
    g_paused = true;
    g_pauseAtMs = NoPauseAt;
  }

  g_changed.notify_all();
}

void SimulationClock::Resume()
{
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    RebaseLocked();
    g_paused = false;
    g_pauseAtMs = NoPauseAt;
  }

  g_changed.notify_all();
}

void SimulationClock::Step(const long long simulatedMs)
{
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    RebaseLocked();
    g_pauseAtMs = g_baseSimulatedMs + static_cast<double>(std::max(0LL, simulatedMs));
    g_paused = false;
  }

  g_changed.notify_all();
}

void SimulationClock::Abort()
{
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    RebaseLocked();
    g_paused = true;   // the time line stops where it is: the final report shows the time of the stop
    g_pauseAtMs = NoPauseAt;
    g_aborted = true;
  }

  g_changed.notify_all();
}

bool SimulationClock::Aborted()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_aborted || ThreadAborted();
}

void SimulationClock::SetThreadAbortFlag(const std::atomic_bool* flag)
{
  t_abortFlag = flag;
}

void SimulationClock::WakeAll()
{
  {
    // Taking the mutex orders the wake-up after the flag change: a thread between its check and its wait cannot miss it.
    std::lock_guard<std::mutex> lock(g_mutex);
  }

  g_changed.notify_all();
}

bool SimulationClock::SleepUntil(const long long simulatedMs)
{
  const auto target = static_cast<double>(simulatedMs);
  std::unique_lock<std::mutex> lock(g_mutex);

  for (;;)
  {
    if (g_aborted || ThreadAborted())
      return false;

    const auto now = NowLocked();
    if (now >= target)
      return true;

    if (PausedLocked(now))
    {
      // Frozen time: only a resume, a step, a speed change or an abort can wake the thread.
      g_changed.wait(lock);
      continue;
    }

    // Real time needed to reach the target (or the end of the current step, whichever comes first).
    const auto realMs = (std::min(target, g_pauseAtMs) - now) / g_speed;
    const auto realWait = std::chrono::microseconds(static_cast<long long>(realMs * 1000.0) + 1);
    g_changed.wait_for(lock, realWait);
  }
}

bool SimulationClock::SleepFor(const std::chrono::milliseconds simulatedDuration)
{
  double target = 0.0;

  {
    std::lock_guard<std::mutex> lock(g_mutex);

    const auto now = NowLocked();
    auto start = now;

    // Chain the sleep to the previous one of the same thread when the thread is simply running late.
    const auto lag = now - t_timeline.lastTargetMs;
    if (t_timeline.generation == g_generation && lag >= 0.0 && lag <= CatchUpWindowRealMs * g_speed)
      start = t_timeline.lastTargetMs;

    target = start + static_cast<double>(simulatedDuration.count());

    t_timeline.generation = g_generation;
    t_timeline.lastTargetMs = target;
  }

  return SleepUntil(static_cast<long long>(std::ceil(target)));
}

void SimulationClock::RestartThreadTimeline()
{
  t_timeline.generation = std::numeric_limits<unsigned int>::max();
}
