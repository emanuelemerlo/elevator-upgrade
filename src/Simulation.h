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

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
  void Restart(const Configuration::Settings& settings, const std::string& batchId = "");

  bool Running() const { return m_management != nullptr && !m_stopped; }
  /** \brief True when the generator is done and every passenger has reached the destination. */
  bool Finished() const;
  bool GeneratorFinished() const;
  unsigned int GeneratedCalls() const;

  // Scenario of the current run: the seed actually used (also when the configured one is 0) and its key.
  unsigned int Seed() const { return m_seed; }
  const std::string& ScenarioKey() const { return m_scenarioKey; }
  // User actions that changed the run (own calls, waves, faults): such a run is not comparable with the others.
  unsigned int Interventions() const { return m_interventions; }

  /**
   * \brief When every passenger of the run has arrived, the run is archived (once) and the clock is paused.
   * \return true the first time it detects the end of the run.
   */
  bool CheckFinished();
  /** \brief True when the current run has been archived (by whoever detected its end first). */
  bool Archived();

  // Comparison of several variants of the same scenario, run one after the other at full speed in a background thread.
  struct Variant
  {
    Configuration::Settings settings;
    std::string label;
  };

  struct BatchStatus
  {
    bool running{ false };
    std::string id;
    std::size_t index{ 0U };   // variant in progress, 0-based
    std::size_t total{ 0U };
    std::string label;
  };

  /** \brief Starts a comparison. Throws if one is already running or a variant is not valid. Returns its id. */
  std::string StartBatch(const std::vector<Variant>& variants);
  void CancelBatch();
  BatchStatus GetBatchStatus();

  // Interactive commands (web interface).
  std::shared_ptr<Call> AddCall(Floors::FloorNumber from, Floors::FloorNumber to);
  unsigned int AddWave(TrafficProfile::Wave wave, unsigned int count);
  bool SetOutOfOrder(const std::string& elevatorId, bool outOfOrder);

  /**
   * \brief The settings chosen by the user (config.json, then the last run started by hand). The runs of a
   *        comparison change the current configuration, not these: new runs and comparisons start from here.
   */
  Configuration::Settings UserSettings();

  /** \brief The running management, nullptr if no run has been started. Use under Lock(). */
  Management* GetManagement() { return m_management.get(); }

  /**
   * \brief Serializes the access to the run between threads that are not part of it (web requests, main):
   *        a restart destroys the cabins, so a reader must hold this lock while it uses them.
   */
  std::unique_lock<std::recursive_mutex> Lock() { return std::unique_lock<std::recursive_mutex>(m_mutex); }

private:
  void StopLocked();
  void Archive();
  void BatchThread(std::vector<Variant> variants, std::string batchId);

private:
  std::recursive_mutex m_mutex;
  std::unique_ptr<Management> m_management;
  std::unique_ptr<PeopleCallsGenerator> m_generator;
  bool m_stopped{ true };
  std::default_random_engine m_random;

  unsigned int m_seed{ 0U };
  std::string m_scenarioKey;
  std::atomic_uint m_interventions{ 0U };
  bool m_archived{ false };
  std::string m_batchId;
  Configuration::Settings m_userSettings;   // batch the current run belongs to, empty for a run started by hand

  std::mutex m_batchMutex;
  BatchStatus m_batch;
  std::atomic_bool m_batchCancel{ false };
  std::unique_ptr<std::thread> m_batchThread;
};
