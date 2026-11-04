#include "BuildingContext.h"

#include "People.h"
#include "SimulationClock.h"
#include "Statistics.h"

namespace
{
  thread_local BuildingContext* t_current = nullptr;
}

BuildingContext::BuildingContext(std::string name) :
  m_name(std::move(name)),
  m_statistics(std::make_unique<Statistics::Store>())
{
}

BuildingContext::~BuildingContext() = default;

People& BuildingContext::GetWaitingPeople()
{
  std::call_once(m_waitingCreated, [this]()
  {
    // Built inside this building, so that its log reads these settings.
    Context::Scope scope(this);
    m_waiting = std::make_unique<People>("Building");
  });

  return *m_waiting;
}

BuildingContext& Context::Default()
{
  // Never destroyed: threads and static objects of the log may still use it while the program exits.
  static auto* context = new BuildingContext("");
  return *context;
}

BuildingContext& Context::Current()
{
  return t_current != nullptr ? *t_current : Default();
}

Context::Scope::Scope(BuildingContext* context) : m_previous(t_current)
{
  t_current = context;
  SimulationClock::SetThreadAbortFlag(context != nullptr ? &context->aborted : nullptr);
}

Context::Scope::~Scope()
{
  t_current = m_previous;
  SimulationClock::SetThreadAbortFlag(m_previous != nullptr ? &m_previous->aborted : nullptr);
}
