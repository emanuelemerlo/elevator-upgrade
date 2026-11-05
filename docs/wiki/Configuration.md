# Configuration

*[Leggi in italiano](Configurazione)*

The simulation reads `config.json` at start (or the file given with `--config`). It is a flat JSON object: the
groups below (*building*, *traffic*...) only organise this page. Missing keys keep their default; invalid values stop
the program at start with a message that names the key and the accepted values. In the web interface most settings
can also be changed for a new run (*New run…*), without touching the file.

All durations are in **simulated** time unless the name says otherwise: a day lasts 24 simulated hours whatever the
speed.

## Building and cabins

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `numberOfFloors` | `10` | Floors, from `2` to `60`. Floor `0` is the lobby, so `10` means floors 0-9. |
| `numberOfElevators` | `4` | Cabins of a building without zones, from `1` to `26` (named A-Z). With `zones` the cabins are the ones of the zones. |
| `maxPeople` | `12` | People per cabin. The dispatcher never assigns a call that would overload a cabin. |
| `timeToReachTheNextFloorMs` | `2000` | Time to move by one floor. |
| `doorTransitionTimeMs` | `1500` | Time to open, and to close, the doors. |
| `enterAndExitTimeMs` | `3000` | Time the doors stay open for people to get in and out. |

## Zones of tall buildings

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `zones` | `""` | Empty: one group serving every floor. Otherwise the zones and their cabins, e.g. `"1-20:6, 21-39:6"`. |
| `skyLobbies` | `""` | Intermediate lobbies and the cabins of their shuttle from the ground floor, e.g. `"30:4"`. |
| `expressFloorFactor` | `0.35` | Time to pass a floor the cabin does not serve, as a share of `timeToReachTheNextFloorMs` (0-1). |

In the web interface the cabins and the zones are a single *Cabins and zones* field:

| Layout | Meaning |
| --- | --- |
| `8` | One group of 8 cabins serving every floor. |
| `1-20:6, 21-39:6` | Two zones served from the lobby: 6 cabins for floors 1-20, 6 for floors 21-39. |
| `1-29:8, 31-45:6, 46-59:6 @ 30:4` | A sky lobby at floor 30 reached from the ground floor by 4 shuttle cabins; the zones above it start from there. |

The zones must cover every floor above the lobby exactly once (a sky lobby is not in a zone); at most 18 zones and 64
cabins in all. The cabins are named after their group: `A1`, `A2`... `B1`... `S1` for the shuttles.

## Traffic

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `type` | `"Random"` | `Random`: the scenario of the day (below). `Fixed`: replays a short predefined list of calls, for debugging. |
| `trafficPattern` | `"Office"` | `Office`: arrivals from the lobby in the morning, lunch both ways, departures in the evening. `Residential`: the opposite. `Uniform`: random trips all day (the original model). |
| `averageCallsPerDay` | `2000` | Trips in a simulated day. The intensity follows a daily profile with morning, lunch and evening peaks. |
| `numberOfSimulationDays` | `1` | Days of the run. |
| `dayStartHour` | `6.0` | Hour of day 1 at which the run starts (`7.5` = 07:30). |
| `numberOfCalls` | `4294967295` | Stops the scenario after this many calls (the maximum means "no limit"). |
| `maxConcurrentCalls` | `200` | Population limit: when this many calls are active, new ones wait until somebody arrives. |
| `minDelayBetweenCallsMs` | `500` | Shortest delay between two calls. |
| `maxDelayBetweenCallsMs` | `900000` | Longest delay between two calls (15 minutes: the night is quiet but not dead). |
| `randomSeed` | `0` | Seed of the scenario. `0` picks a new one at every run; the seed used is shown and archived, so a run can be repeated. |

## Passengers

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `stairsPatienceSeconds` | `120` | Average patience: every passenger gets between half and one and a half times this value; who waits longer takes the stairs. `0` = nobody gives up. Your own passengers never give up. |
| `happyWaitSeconds` | `20` | A passenger who waited at most this long in all arrives "served quickly" (👍). Only an evaluation, it does not change the scenario. |

## Dispatch

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `dispatchStrategy` | `"Optimized"` | `Optimized`: route simulation, least extra passenger time. `Nearest`: closest cabin. `Collective`: closest among the idle cabins and those already going towards the call. |
| `idleParking` | `"Stay"` | Where an idle cabin waits. `Stay`: where it stopped. `Lobby`: at the lobby of its group. `Spread`: at a home floor, evenly spaced. `Peak`: at the lobby during a rush detected in the traffic, where it stopped otherwise. |

## Energy

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `standbyPowerW` | `200` | Standby power of every cabin, watts, for the whole run. |
| `motorEfficiency` | `0.75` | Efficiency of motor and gears (0-1). |

The model: the counterweight balances the empty cabin plus half the rated load (75 kg per person); moving one floor
(3 m) costs the work to lift the heavier side when it goes up, plus friction; 1 Wh per start of the motor, 0.15 Wh per
door cycle; no energy is recovered going down.

## Time and run

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `dayDurationSeconds` | `1440` | Real seconds for 24 simulated hours: the initial speed (`1440` = a day in 24 minutes = 60x; `86400` = real time; minimum `43.2` = 2000x). It can be changed while the run goes on. |
| `autoStop` | `true` | Console: stop and print the report when the run is over. |

## Log and files

| Key | Shipped value | Meaning |
| --- | --- | --- |
| `traceLevel` | `"Info"` | `Debug`, `Verbose`, `Info`, `Warning`, `Error`. `Debug` also shows what every dispatch decision was based on. |
| `defaultLogType` | `"Screen"` | Console: `Screen`, `File` (no dashboard, report in the file), `ScreenAndFile` (alias `Both`). The web mode always logs to the file only. |
| `filePath` | `"elevator.log"` | Log file. |
| `runsFilePath` | `"runs.jsonl"` | Archive of the completed runs. |

## Keys of the original project

`simulationDayDurationMs` and `timeScale` have been replaced by `dayDurationSeconds`: a configuration that still uses
them is refused with an explanation, instead of being silently misread.

## Example: a 40-floor office with two zones, idle cabins back to the lobby at rush hours

```json
{
  "numberOfFloors": 40,
  "zones": "1-20:6, 21-39:6",
  "maxPeople": 16,
  "timeToReachTheNextFloorMs": 1000,
  "trafficPattern": "Office",
  "averageCallsPerDay": 9600,
  "dayStartHour": 7.0,
  "idleParking": "Peak",
  "randomSeed": 2026
}
```
