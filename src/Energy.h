/**********************************************************************************
*        File: Energy.h
* Description: Simple energy model of a traction elevator with counterweight.
*              - Travel: the counterweight balances the empty cabin plus half of the rated load. Moving one
*                floor costs the work to lift the heavier side (|load - balance| * g * floor height, divided
*                by the motor efficiency) when the heavier side goes up, plus friction; when the heavier
*                side goes down gravity does the work and only friction is paid (no regenerative drive).
*              - Every run (start and stop of the motor) and every door cycle cost a fixed amount.
*              - Every cabin consumes a standby power (controller, lighting, ventilation) all day long,
*                in service or not: more cabins mean more standby energy.
*              The values are typical orders of magnitude, enough to compare strategies and configurations,
*              not to size a real installation.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
**********************************************************************************/

#pragma once

#include "Configuration.h"
#include "Floors.h"

#include <cmath>
#include <cstddef>

namespace Energy
{
  constexpr double PassengerMassKg = 75.0;
  constexpr double FloorHeightM = 3.0;
  constexpr double Gravity = 9.81;
  constexpr double BalanceRatio = 0.5;        // share of the rated load balanced by the counterweight
  constexpr double FrictionPerFloorWh = 1.5;
  constexpr double RunStartWh = 1.0;          // acceleration and braking of one run
  constexpr double DoorCycleWh = 0.15;        // one opening and closing
  constexpr double JoulesPerWh = 3600.0;

  /** \brief Energy to move one floor with 'onboard' passengers in the given direction (Wh). */
  inline double TravelPerFloorWh(const std::size_t onboard, const Direction direction)
  {
    const auto balance = BalanceRatio * static_cast<double>(Configuration::Elevator::MaxPeople());
    const auto imbalanceKg = (static_cast<double>(onboard) - balance) * PassengerMassKg;

    // The heavier side rises: the cabin when it goes up full, the counterweight when the cabin goes down empty.
    const auto heavierSideRises = direction == Direction::Up ? imbalanceKg > 0.0 : imbalanceKg < 0.0;
    if (!heavierSideRises)
      return FrictionPerFloorWh;

    const auto efficiency = Configuration::Get().motorEfficiency;
    return std::fabs(imbalanceKg) * Gravity * FloorHeightM / efficiency / JoulesPerWh + FrictionPerFloorWh;
  }

  /** \brief Standby energy of 'cabins' cabins over a simulated duration (Wh). */
  inline double StandbyWh(const std::size_t cabins, const long long simulatedMs)
  {
    const auto hours = static_cast<double>(simulatedMs) / 3600000.0;
    return Configuration::Get().standbyPowerW * hours * static_cast<double>(cabins);
  }
}
