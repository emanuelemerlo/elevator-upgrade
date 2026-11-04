/**********************************************************************************
*        File: Simulation.h
* Description: One run of the building: cabins, management, calls generator, clock and statistics.
*              It owns the lifecycle that main() used to do by hand, so that a run can also be
*              stopped and started again with new settings (new game from the web interface).
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes: The cabins, the waiting passengers and the statistics are global/static in the model,
*              so only one Simulation can run at a time.
**********************************************************************************/

#pragma once

#include "Configuration.h"
#include "Floors.h"
#include "TrafficProfile.h"

#include <memory>
#include <mutex>
#include <random>
#include <string>

class Management;
class PeopleCallsGenerator;
class Call;

class Simulation final
{
public:
  Simulation();
  ~Simulation();

  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;
  Simulation(Simulation&&) = delete;
  Simulation& operator=(Simulation&&) = delete;

public:
  /** \brief Starts a run with the current configuration (statistics, waiting passengers and clock start from zero). */
  void Start();
  /** \brief Stops the run: the clock is aborted and every thread is joined. Statistics are kept for the report. */
  void Stop();
  /** \brief Stops the current run, applies the new settings and starts a new run. Throws if the settings are invalid. */
  void Restart(const Configuration::Settings& settings);

  bool Running() const { return m_management != nullptr && !m_stopped; }
  /** \brief True when the generator is done and every passenger has reached the destination. */
  bool Finished() const;
  bool GeneratorFinished() const;
  unsigned int GeneratedCalls() const;

  // Interactive commands (web interface).
  std::shared_ptr<Call> AddCall(Floors::FloorNumber from, Floors::FloorNumber to);
  unsigned int AddWave(TrafficProfile::Wave wave, unsigned int count);
  bool SetOutOfOrder(const std::string& elevatorId, bool outOfOrder);

  /** \brief The running management, nullptr if no run has been started. Use under Lock(). */
  Management* GetManagement() { return m_management.get(); }

  /**
   * \brief Serializes the access to the run between threads that are not part of it (web requests, main):
   *        a restart destroys the cabins, so a reader must hold this lock while it uses them.
   */
  std::unique_lock<std::recursive_mutex> Lock() { return std::unique_lock<std::recursive_mutex>(m_mutex); }

private:
  void StopLocked();

private:
  std::recursive_mutex m_mutex;
  std::unique_ptr<Management> m_management;
  std::unique_ptr<PeopleCallsGenerator> m_generator;
  bool m_stopped{ true };
  std::default_random_engine m_random;
};
