/**********************************************************************************
*        File: BuildingContext.h
* Description: The state of one building: its settings, the passengers waiting on its floors,
*              its statistics and its stop flag.
*              The original model keeps these as global/static data (Configuration::Get(),
*              Floors::GetPeople(), Statistics). They still look global to the model code, but
*              they belong to the building of the calling thread: every thread of a building
*              (cabins, dispatcher, generator) runs inside its context, so two buildings can run
*              side by side on the same simulated clock without seeing each other.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes: A thread that never entered a context uses the default one, which is the single
*              building of the console version and of the original program.
**********************************************************************************/

#pragma once

#include "Configuration.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

class People;

namespace Statistics
{
  struct Store;
}

class BuildingContext final
{
public:
  explicit BuildingContext(std::string name);
  ~BuildingContext();

  BuildingContext(const BuildingContext&) = delete;
  BuildingContext& operator=(const BuildingContext&) = delete;

public:
  // Name of the building in the events ("" for the default building).
  const std::string& Name() const { return m_name; }

  Configuration::Settings settings;

  // Set when the run of this building is stopped: every simulated sleep of its threads returns at once.
  std::atomic_bool aborted{ false };

  People& GetWaitingPeople();
  Statistics::Store& GetStatistics() { return *m_statistics; }

private:
  std::string m_name;
  std::once_flag m_waitingCreated;
  std::unique_ptr<People> m_waiting;   // created on first use: People logs, and the log reads the settings
  std::unique_ptr<Statistics::Store> m_statistics;
};

namespace Context
{
  /** \brief The building of the calling thread (the default one if the thread never entered a context). */
  BuildingContext& Current();

  /** \brief The building of the console version and of the first building of the web interface. */
  BuildingContext& Default();

  /** \brief Runs the calling thread inside a building until the end of the scope (scopes can be nested). */
  class Scope final
  {
  public:
    explicit Scope(BuildingContext* context);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

  private:
    BuildingContext* m_previous;
  };
}
