/**********************************************************************************
*        File: SimulationClock.h
* Description: Simulated clock of the building.
*              The simulation runs on its own time line: a simulated day lasts 24 hours and every
*              duration of the model (floor travel, doors, call delays) is expressed in that time.
*              The clock maps it onto the wall clock with a speed factor that can be changed at
*              run time, and it can be paused, advanced by a fixed step or aborted.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes: Every thread of the simulation waits through SleepFor()/SleepUntil(), so pausing the
*              clock freezes the whole building and changing the speed changes it everywhere.
**********************************************************************************/

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace SimulationClock
{
  constexpr long long HourMs = 60LL * 60LL * 1000LL;
  constexpr long long DayMs = 24LL * HourMs;

  constexpr double MinSpeed = 0.1;
  constexpr double MaxSpeed = 2000.0;

  /**
   * \brief Restarts the clock from simulated time 0 (running, not aborted).
   * \param speed Simulated milliseconds per real millisecond.
   * \param startTimeOfDayMs Time of day of simulated time 0 (e.g. 6 * HourMs: the run starts at 06:00 of day 1).
   */
  void Reset(double speed, long long startTimeOfDayMs);

  /** \brief Simulated milliseconds elapsed since Reset(). */
  long long Now();
  /** \brief Simulated milliseconds since 00:00 of day 1 (Now() + start time of day). */
  long long TimeOfDayMs();
  long long StartTimeOfDayMs();

  double Speed();
  /** \brief Changes the speed (clamped to [MinSpeed, MaxSpeed]); sleeping threads adapt immediately. */
  void SetSpeed(double speed);

  bool Paused();
  void Pause();
  void Resume();
  /** \brief Runs the clock for the given simulated time, then pauses it again. */
  void Step(long long simulatedMs);

  /** \brief Wakes every sleeping thread and makes every current and future sleep return false at once. */
  void Abort();
  /** \brief True if the clock or the building of the calling thread has been aborted. */
  bool Aborted();

  /**
   * \brief Stop flag of the building the calling thread belongs to (see BuildingContext): when it is set, the sleeps
   *        of that thread return false, while the clock keeps running for the other buildings.
   */
  void SetThreadAbortFlag(const std::atomic_bool* flag);
  /** \brief Wakes every sleeping thread, so that it checks its building stop flag. */
  void WakeAll();

  /**
   * \brief Sleeps for a simulated duration.
   *        Consecutive sleeps of the same thread are chained on the simulated time line: when the thread
   *        wakes up a little late (operating system timers) the next sleep starts from the time it should
   *        have woken up, so at high speed the delays do not accumulate.
   * \return false if the clock has been aborted.
   */
  bool SleepFor(std::chrono::milliseconds simulatedDuration);

  /** \brief Sleeps until the given simulated time. \return false if the clock has been aborted. */
  bool SleepUntil(long long simulatedMs);

  /**
   * \brief Forgets the time line of the calling thread: the next SleepFor() starts from now.
   *        To be called after the thread has been waiting for an external event (e.g. an idle cabin
   *        waking up for a new call), whose time is not related to its previous sleep.
   */
  void RestartThreadTimeline();
}
