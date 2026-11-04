/**********************************************************************************
*        File: Zoning.h
* Description: Groups of cabins of a tall building.
*              Without zones every cabin serves every floor (one group, as in the original project).
*              With zones the floors above the lobby are split into ranges, each served by its own
*              group of cabins from a lobby; the cabins run express (without stops, faster) through
*              the floors they do not serve. With sky lobbies, shuttle groups run express from the
*              ground floor to an intermediate lobby, and the zones above it start from there.
*              A passenger whose origin and destination are not served by the same group changes
*              cabin at a lobby: the route is planned leg by leg (see Plan()).
*
*              Layout syntax (also used by the web interface and the comparisons):
*                "4"                              one group of 4 cabins serving every floor
*                "1-20:6, 21-40:6"                two zones served from the lobby, 6 cabins each
*                "1-29:8, 31-45:6, 46-59:6 @ 30:4" zones above floor 30 are served from the sky lobby
*                                                 at floor 30, reached by a shuttle group of 4 cabins
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
**********************************************************************************/

#pragma once

#include "Configuration.h"
#include "Floors.h"

#include <string>
#include <vector>

struct ElevatorGroup
{
  std::string name;               // "" for the single group of a building without zones, "A", "B"... "S" for shuttles
  Floors::FloorNumber lobby{ 0U };
  Floors::FloorNumber from{ 0U }; // range of floors served above the lobby (zones), or the sky lobby (shuttles)
  Floors::FloorNumber to{ 0U };
  unsigned int cabins{ 0U };
  bool shuttle{ false };
  std::vector<bool> served;       // served[floor]: the cabins of the group can stop there

  bool Serves(const Floors::FloorNumber floor) const { return floor < served.size() && served[floor]; }
};

namespace Zoning
{
  /**
   * \brief The groups of the given settings. Without zones: one group of numberOfElevators cabins serving every floor.
   *        Throws std::invalid_argument when the zones do not cover the building exactly once.
   */
  std::vector<ElevatorGroup> Groups(const Configuration::Settings& settings);

  /** \brief Total number of cabins of the given settings. */
  unsigned int TotalCabins(const Configuration::Settings& settings);

  /** \brief Reads a layout ("4", "1-20:6, 21-40:6", "... @ 30:4") into the settings. Throws std::invalid_argument. */
  void ApplyLayout(const std::string& layout, Configuration::Settings& settings);

  /** \brief The layout of the settings in the syntax above. */
  std::string Layout(const Configuration::Settings& settings);

  struct Leg
  {
    int group{ -1 };                               // index in the groups, -1 if the trip is impossible
    Floors::FloorNumber to{ Floors::InvalidFloor }; // where this leg ends: the destination, or a lobby to change cabin
  };

  /**
   * \brief The next leg of a trip from 'from' to 'destination': a group that serves both floors if there is one,
   *        otherwise the first leg of the route with the fewest changes of cabin (through the lobbies).
   */
  Leg Plan(const std::vector<ElevatorGroup>& groups, Floors::FloorNumber from, Floors::FloorNumber destination);
}
