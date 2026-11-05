/**********************************************************************************
*        File: PeopleCallsGenerator.h
* Description: Implements a random and a fixes calls generator.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes:
**********************************************************************************/

#pragma once

#include "Log.h"
#include "WorkerThread.h"
#include "Scenario.h"

#include <atomic>
#include <memory>
#include <vector>

class Management;
class BuildingContext;

class PeopleCallsGenerator final 
{
private:
  class RandomGeneratorThread final : public WorkerThread<PeopleCallsGenerator>
  {
  public:
    explicit RandomGeneratorThread(PeopleCallsGenerator* peopleCallsGenerator) :
      WorkerThread<PeopleCallsGenerator>(peopleCallsGenerator) {}

  protected:
    void CycleFunction(PeopleCallsGenerator* peopleCallsGenerator) override;
  };

  class FixedGeneratorThread final : public WorkerThread<PeopleCallsGenerator>
  {
  public:
    explicit FixedGeneratorThread(PeopleCallsGenerator* peopleCallsGenerator = nullptr) :
      WorkerThread<PeopleCallsGenerator>(peopleCallsGenerator) {}

  protected:
    void CycleFunction(PeopleCallsGenerator* peopleCallsGenerator) override;
  };

public:
  explicit PeopleCallsGenerator(Management& management);
  PeopleCallsGenerator() = delete;

  ~PeopleCallsGenerator();

  PeopleCallsGenerator(const PeopleCallsGenerator&) = delete;
  PeopleCallsGenerator& operator=(const PeopleCallsGenerator&) = delete;

  PeopleCallsGenerator(PeopleCallsGenerator&&) = delete;
  PeopleCallsGenerator operator=(PeopleCallsGenerator&&) = delete;

public:
  // Replays the scenario of the given seed (see Scenario).
  void StartRandom(unsigned int seed);
  void StartFixed();

  void Shutdown();

  // True when the generator has produced every call it had to produce (or has been stopped).
  bool Finished() const { return m_finished; }
  unsigned int GeneratedCalls() const { return m_generatedCalls; }
  unsigned int Seed() const { return m_seed; }
  std::size_t ScheduledCalls() const { return m_schedule.size(); }

private:
  Management& m_management;

  Log m_log;

  std::atomic_bool m_finished{ false };
  BuildingContext* m_context{ nullptr };   // building the generator belongs to (its thread runs in it)
  std::atomic_uint m_generatedCalls{ 0U };
  unsigned int m_seed{ 0U };
  std::vector<ScheduledCall> m_schedule;

  std::unique_ptr<WorkerThread<PeopleCallsGenerator>> m_generatorThread;
};


