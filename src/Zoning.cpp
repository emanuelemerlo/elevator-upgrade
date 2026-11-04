#include "Zoning.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <sstream>
#include <stdexcept>

namespace
{
  constexpr unsigned int MaxCabins = 64U;

  std::string Trim(const std::string& text)
  {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
      return "";
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1U);
  }

  std::vector<std::string> Split(const std::string& text, const char separator)
  {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, separator))
    {
      part = Trim(part);
      if (!part.empty())
        parts.push_back(part);
    }
    return parts;
  }

  unsigned int ParseNumber(const std::string& text, const std::string& what)
  {
    if (text.empty() || text.size() > 6U || !std::all_of(text.begin(), text.end(), [](const unsigned char c) { return std::isdigit(c) != 0; }))
      throw std::invalid_argument("zones: '" + text + "' is not a valid " + what);
    return static_cast<unsigned int>(std::stoul(text));
  }

  struct ZoneSpec
  {
    unsigned int from{ 0U };
    unsigned int to{ 0U };
    unsigned int cabins{ 0U };
  };

  // "1-20:6" (a range) or "30:4" (a single floor, for the sky lobbies).
  ZoneSpec ParseSpec(const std::string& text, const bool allowRange)
  {
    const auto colon = text.find(':');
    if (colon == std::string::npos)
      throw std::invalid_argument("zones: '" + text + "' must be floors:cabins, e.g. 1-20:6");

    ZoneSpec spec;
    const auto floors = Trim(text.substr(0U, colon));
    spec.cabins = ParseNumber(Trim(text.substr(colon + 1U)), "number of cabins");

    const auto dash = floors.find('-');
    if (dash == std::string::npos)
    {
      spec.from = spec.to = ParseNumber(floors, "floor");
    }
    else
    {
      if (!allowRange)
        throw std::invalid_argument("zones: a sky lobby is a single floor, e.g. 30:4");
      spec.from = ParseNumber(Trim(floors.substr(0U, dash)), "floor");
      spec.to = ParseNumber(Trim(floors.substr(dash + 1U)), "floor");
    }

    if (spec.cabins == 0U)
      throw std::invalid_argument("zones: every group needs at least one cabin");
    if (spec.from > spec.to)
      throw std::invalid_argument("zones: '" + text + "' goes downwards");

    return spec;
  }

  std::string GroupName(const std::size_t index)
  {
    return std::string(1U, static_cast<char>('A' + index));
  }
}

std::vector<ElevatorGroup> Zoning::Groups(const Configuration::Settings& settings)
{
  const auto floors = settings.numberOfFloors;
  const auto top = floors - 1U;
  std::vector<ElevatorGroup> groups;

  if (Trim(settings.zones).empty())
  {
    if (!Trim(settings.skyLobbies).empty())
      throw std::invalid_argument("zones: sky lobbies need zones");

    ElevatorGroup group;
    group.from = 0U;
    group.to = top;
    group.cabins = settings.numberOfElevators;
    group.served.assign(floors, true);
    groups.push_back(group);
    return groups;
  }

  std::vector<ZoneSpec> skyLobbies;
  for (const auto& text : Split(settings.skyLobbies, ','))
    skyLobbies.push_back(ParseSpec(text, false));
  std::sort(skyLobbies.begin(), skyLobbies.end(), [](const ZoneSpec& a, const ZoneSpec& b) { return a.from < b.from; });

  std::vector<ZoneSpec> zones;
  for (const auto& text : Split(settings.zones, ','))
    zones.push_back(ParseSpec(text, true));
  std::sort(zones.begin(), zones.end(), [](const ZoneSpec& a, const ZoneSpec& b) { return a.from < b.from; });

  if (zones.empty())
    throw std::invalid_argument("zones: no zone given");

  for (const auto& sky : skyLobbies)
    if (sky.from == 0U || sky.from >= top)
      throw std::invalid_argument("zones: a sky lobby must be between the ground floor and the top floor");

  // Every floor above the ground floor belongs to exactly one zone, or is a sky lobby.
  std::vector<int> owner(floors, -1);
  for (std::size_t index = 0U; index < zones.size(); ++index)
  {
    if (zones[index].from == 0U || zones[index].to > top)
      throw std::invalid_argument("zones: zones go from floor 1 to floor " + std::to_string(top) + " (floor 0 is the lobby)");

    for (auto floor = zones[index].from; floor <= zones[index].to; ++floor)
    {
      if (owner[floor] != -1)
        throw std::invalid_argument("zones: floor " + std::to_string(floor) + " is in two zones");
      owner[floor] = static_cast<int>(index);
    }
  }

  for (const auto& sky : skyLobbies)
  {
    if (owner[sky.from] != -1)
      throw std::invalid_argument("zones: the sky lobby at floor " + std::to_string(sky.from) + " cannot be inside a zone");
    owner[sky.from] = -2;
  }

  for (auto floor = 1U; floor <= top; ++floor)
    if (owner[floor] == -1)
      throw std::invalid_argument("zones: floor " + std::to_string(floor) + " is not in any zone");

  for (std::size_t index = 0U; index < zones.size(); ++index)
  {
    ElevatorGroup group;
    group.name = GroupName(index);
    group.from = zones[index].from;
    group.to = zones[index].to;
    group.cabins = zones[index].cabins;

    // A zone is served from the highest sky lobby below it, or from the ground floor.
    for (const auto& sky : skyLobbies)
      if (sky.from < group.from)
        group.lobby = sky.from;

    group.served.assign(floors, false);
    group.served[group.lobby] = true;
    for (auto floor = group.from; floor <= group.to; ++floor)
      group.served[floor] = true;

    groups.push_back(group);
  }

  for (std::size_t index = 0U; index < skyLobbies.size(); ++index)
  {
    ElevatorGroup shuttle;
    shuttle.name = skyLobbies.size() == 1U ? "S" : "S" + std::to_string(index + 1U);
    shuttle.shuttle = true;
    shuttle.lobby = 0U;
    shuttle.from = shuttle.to = skyLobbies[index].from;
    shuttle.cabins = skyLobbies[index].cabins;
    shuttle.served.assign(floors, false);
    shuttle.served[0U] = true;
    shuttle.served[shuttle.from] = true;
    groups.push_back(shuttle);
  }

  unsigned int cabins = 0U;
  for (const auto& group : groups)
    cabins += group.cabins;
  if (cabins > MaxCabins)
    throw std::invalid_argument("zones: at most " + std::to_string(MaxCabins) + " cabins in all");

  // Zones are named A, B, C... and the shuttles S: at most 18 zones (A to R).
  if (zones.size() > 18U)
    throw std::invalid_argument("zones: at most 18 zones");

  return groups;
}

unsigned int Zoning::TotalCabins(const Configuration::Settings& settings)
{
  unsigned int cabins = 0U;
  for (const auto& group : Groups(settings))
    cabins += group.cabins;
  return cabins;
}

void Zoning::ApplyLayout(const std::string& layout, Configuration::Settings& settings)
{
  const auto text = Trim(layout);
  if (text.empty())
    throw std::invalid_argument("layout: empty");

  if (std::all_of(text.begin(), text.end(), [](const unsigned char c) { return std::isdigit(c) != 0; }))
  {
    settings.numberOfElevators = ParseNumber(text, "number of cabins");
    settings.zones.clear();
    settings.skyLobbies.clear();
    return;
  }

  const auto at = text.find('@');
  settings.zones = Trim(text.substr(0U, at));
  settings.skyLobbies = at == std::string::npos ? "" : Trim(text.substr(at + 1U));

  // Check it now, with a message about the layout rather than about the whole configuration.
  Groups(settings);
}

std::string Zoning::Layout(const Configuration::Settings& settings)
{
  if (Trim(settings.zones).empty())
    return std::to_string(settings.numberOfElevators);

  auto layout = Trim(settings.zones);
  if (!Trim(settings.skyLobbies).empty())
    layout += " @ " + Trim(settings.skyLobbies);
  return layout;
}

Zoning::Leg Zoning::Plan(const std::vector<ElevatorGroup>& groups, const Floors::FloorNumber from, const Floors::FloorNumber destination)
{
  Leg leg;

  // A direct group first; with several, the one whose zone contains the destination (a local cabin, not a shuttle).
  for (std::size_t index = 0U; index < groups.size(); ++index)
  {
    if (!groups[index].Serves(from) || !groups[index].Serves(destination))
      continue;

    if (leg.group == -1 || (groups[leg.group].shuttle && !groups[index].shuttle))
    {
      leg.group = static_cast<int>(index);
      leg.to = destination;
    }
  }

  if (leg.group != -1)
    return leg;

  // Breadth-first search on the groups: two groups are connected when they share a floor (a lobby).
  std::vector<int> previous(groups.size(), -2);
  std::deque<std::size_t> queue;
  for (std::size_t index = 0U; index < groups.size(); ++index)
    if (groups[index].Serves(from))
    {
      previous[index] = -1;
      queue.push_back(index);
    }

  int target = -1;
  while (!queue.empty() && target == -1)
  {
    const auto current = queue.front();
    queue.pop_front();

    if (groups[current].Serves(destination))
    {
      target = static_cast<int>(current);
      break;
    }

    for (std::size_t next = 0U; next < groups.size(); ++next)
    {
      if (previous[next] != -2)
        continue;

      for (Floors::FloorNumber floor = 0U; floor < groups[current].served.size(); ++floor)
      {
        if (groups[current].Serves(floor) && groups[next].Serves(floor))
        {
          previous[next] = static_cast<int>(current);
          queue.push_back(next);
          break;
        }
      }
    }
  }

  if (target == -1)
    return leg;

  // Walk back to the first group of the route and to the floor where it hands over to the second one.
  auto second = target;
  auto first = target;
  while (previous[first] != -1)
  {
    second = first;
    first = previous[first];
  }

  leg.group = first;
  for (Floors::FloorNumber floor = 0U; floor < groups[first].served.size(); ++floor)
  {
    // The change of cabin happens at a lobby: prefer the shared floor closest to the passenger's origin.
    if (groups[first].Serves(floor) && groups[second].Serves(floor) && floor != from)
    {
      const auto distance = [from](const Floors::FloorNumber f) { return f > from ? f - from : from - f; };
      if (!Floors::IsValid(leg.to) || distance(floor) < distance(leg.to))
        leg.to = floor;
    }
  }

  return leg;
}
