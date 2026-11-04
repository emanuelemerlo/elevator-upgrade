#include "EventBus.h"

#include "SimulationClock.h"

#include <deque>
#include <mutex>

namespace
{
  constexpr std::size_t MaxStoredEvents = 4000U;

  std::mutex g_mutex;
  std::deque<SimulationEvent> g_events;
  std::uint64_t g_lastSequence{ 0U };
}

void EventBus::Publish(const std::string& type, const Json::Writer& data)
{
  SimulationEvent event;
  event.simulatedMs = SimulationClock::Now();
  event.type = type;
  event.data = data.Str();

  std::lock_guard<std::mutex> lock(g_mutex);

  event.sequence = ++g_lastSequence;
  g_events.push_back(std::move(event));

  while (g_events.size() > MaxStoredEvents)
    g_events.pop_front();
}

std::vector<SimulationEvent> EventBus::Since(const std::uint64_t sequence, const std::size_t maxEvents)
{
  std::lock_guard<std::mutex> lock(g_mutex);

  std::vector<SimulationEvent> events;
  for (const auto& event : g_events)
  {
    if (event.sequence <= sequence)
      continue;

    events.push_back(event);
    if (events.size() >= maxEvents)
      break;
  }

  return events;
}

std::uint64_t EventBus::LastSequence()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_lastSequence;
}

void EventBus::Clear()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  g_events.clear();
}

std::string EventBus::ToJson(const SimulationEvent& event)
{
  return Json::Writer::Object()
    .Add("seq", static_cast<unsigned long long>(event.sequence))
    .Add("t", event.simulatedMs)
    .Add("type", event.type)
    .Raw("data", event.data)
    .Str();
}
