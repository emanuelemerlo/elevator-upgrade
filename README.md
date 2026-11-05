# elevator playground

A fork of [elevator](https://github.com/emanuelemerlo/elevator) that keeps its C++ simulation (threads, dispatcher,
statistics, console dashboard) and adds a browser interface to watch it and poke at it: the building runs through a
simulated day, you can call lifts yourself, send waves of people, put cabins out of order. It stays a simulation:
the point is to compare dispatch strategies and building configurations on exactly the same passengers, with waits,
energy and the people who gave up and took the stairs.

```sh
make
./Elevator.exe --web        # then open http://127.0.0.1:8080/
./Elevator.exe              # the original console simulation
```

Ready-made packages for Windows, Linux and macOS are on the
[releases page](https://github.com/emanuelemerlo/elevator-upgrade/releases): unpack and start `start-web`.

### Releasing a version

1. Add the section `## [1.1.0] - date` to `CHANGELOG.md`, written for the people who download the program: it becomes
   the release notes (also attached as `RELEASE_NOTES.md` and included in every package).
2. Push a tag (`git tag v1.1.0 && git push origin v1.1.0`), or start *Actions → release → Run workflow* with the
   version.
3. The `release` workflow builds and tests on Windows (Visual Studio, static runtime), Linux (Ubuntu 22.04, static
   libstdc++) and macOS (universal binary), packages each platform with the web interface, `config.json`, the
   licenses and a `start-web` launcher, starts every package and checks its web interface, then publishes the
   release. A version with a suffix (`1.1.0-beta.1`) is published as a pre-release. Without its section in
   `CHANGELOG.md` the workflow stops before building.

The [wiki](https://github.com/emanuelemerlo/elevator-upgrade/wiki) (Italian and English) has the history of the
project, a user manual and a developer guide. See also [Web interface](#web-interface), [Comparing strategies](#comparing-strategies), [Energy](#energy) and
[Simulated day](#simulated-day) below; the rest of this file is the
documentation of the original project, updated where the fork changed something.

---

A simple C++ text-based simulation of one or more elevators.

In the 80s an italian magazine called "Commodore Computer Club" launched a competition to create a program that could simulate an elevator on the Commodore 64. I was a child at the time and my computer skills were zero, but I always had the idea of trying it out as a game, so I decided to try again today with C ++ and STL. 

The simulated model requires the user to call the elevator by simply selecting the floor he wants to go to. The "management" receives the call and assigns it to the most appropriate elevator. This model is the one that allows more optimizations than traditional internal choice elevator and is exactly what is installed in the building where I work.

"Elevator" is intended as an educational project, it was a small gym to practice with threading, synchronization, collections and modeling.

This is a Windows/Linux multiplatform project.

## Running the simulation

Build the program from the repository root. With the provided `makefile`, run:

```sh
make
```

On Windows, open `Elevator.sln` with Visual Studio 2022 or 2026: the project uses the toolset of the Visual Studio
that opens it (v143 or v145), so it does not need to be retargeted. The VS Code build task produces `Elevator.exe` too. The task compiles all
source files in `src`, including the statistics module.

Start the program: by default it stops by itself as soon as every call has been generated and
served (`simulation.autoStop`). You can still press `Enter` at any time to stop it earlier. At the end,
the screen is replaced by a final report with global statistics, anomaly indicators
and one row for each configured elevator.

To compare two dispatching strategies or two configurations quickly and repeatably, combine
`simulation.dayDurationSeconds` (a short day runs faster than real time), `callsGenerator.randomSeed` (same calls every
run) and `log.defaultLogType: "File"` (no dashboard, the final report is written to the log file).

Command line options:

| Option | Meaning |
| --- | --- |
| *(none)* | Console simulation with the symbolic dashboard. |
| `--web` | Runs the simulation behind the web interface. Press Enter in the console to quit. |
| `--port N` | Port of the web interface (default `8080`). |
| `--host ADDRESS` | Address to listen on (default `127.0.0.1`; `0.0.0.0` lets other computers of the network connect). |
| `--web-root DIR` | Folder with the files of the web interface (default `./web`, so start the program from the repository folder). |
| `--config FILE` | Configuration file (default `config.json`). |
| `--version` | Prints the version and exits. |

### Tests

```sh
make test
```

builds and runs every program in `tests/`:

- `AssignmentOptimizerTests.cpp` checks the route simulation used by the dispatcher (arrival times, sweep direction,
  capacity, shared stops, anti-starvation weight) and the stop logic of the floors;
- `SimulationTests.cpp` checks the simulated clock (pause, step, speed changes, abort, no drift at high speed) and whole
  runs at high speed: a run ends by itself with every passenger delivered, a cabin put out of order while travelling
  stops at the next floor and its passenger completes the trip with another cabin, a restart applies new settings,
  the same seed gives the same passengers, every strategy serves the same scenario and is archived with its energy,
  idle cabins reach their home floors, impatient passengers take the stairs, two buildings run side by side without
  sharing passengers, statistics or faults, zone layouts are validated, trips are routed through the lobbies and a
  zoned building with a sky lobby serves everybody with its cabins stopping only on their floors.

## Web interface

`Elevator.exe --web` starts the simulation and a small web server (based on
[cpp-httplib](https://github.com/yhirose/cpp-httplib), a single header in `third_party/`). Open the printed address
in a browser. The page shows the cross-section of the building with its sky following the simulated hour; the
cabins move, open their doors, and the people waiting on each floor change colour while they wait (green, then amber
after 20 s, then red after 45 s).

What you can do:

| Action | How |
| --- | --- |
| Pause, resume | Round button or Space |
| Advance one simulated minute and pause | `+1 min` or S |
| Change speed (from real time to a whole day in 43 seconds) | Slider, presets, + and − keys |
| Call a lift | Click a floor, then the floor to go to (Esc cancels). Your passengers have a dark ring and are followed in the feed. |
| Put a cabin out of order, repair it | Click the cabin. It stops at the next floor, everybody gets out and is given back to the dispatcher; the passengers waiting for it are reassigned. |
| Send a wave of people | Morning rush, lunch break, evening rush or random crowd: the direction of the trips follows the building type |
| New run | Number of cabins and floors, capacity, trips per day, building type, start hour, length of a day, days, seed, cabin timing |
| Report | The same final report of the console version, at any moment; it opens by itself when everybody has arrived |
| Compare strategies | Runs several variants of the same scenario one after the other at full speed (see [Comparing strategies](#comparing-strategies)) |
| Results | Table and charts of the completed runs of a scenario, CSV export, *Watch* to run a variant again live |
| Two buildings | Two variants side by side, live, on the same passengers and the same clock (see [Two buildings](#two-buildings)) |

The *Dispatcher* panel shows the last assignment decision with the cost of every cabin (the extra passenger time the
call adds to its route), so you can see why a cabin has been chosen. *What happened* lists the important events
(faults, waves, your own passengers); tick *every passenger* to see all of them.

The interface uses a small JSON API, handy for scripts too:

| Request | Meaning |
| --- | --- |
| `GET /api/state?since=N` | Clock, cabins (floor, doors, current floor transition, passengers, stops), waiting passengers, statistics and the events after sequence `N` |
| `GET /api/config` | Current settings and the daily traffic profile |
| `GET /api/report` | Final report as text lines |
| `POST /api/control` | `action=pause`, `resume`, `step` (`ms=...`) or `speed` (`value=` simulated seconds per real second) |
| `POST /api/call` | `from=F&to=T` |
| `POST /api/wave` | `kind=morning\|lunch\|evening\|random&count=N` |
| `POST /api/elevator` | `id=A&outOfOrder=true\|false` |
| `POST /api/restart` | New run; the parameters have the names of `config.json` (`numberOfElevators`, `numberOfFloors`, `dayDurationSeconds`, `dispatchStrategy`, ...), plus `layout` for cabins and zones in one (`8`, `1-20:6, 21-39:6 @ ...`) |
| `GET /api/runs` | Archive of the completed runs: one flat object per run with settings, scenario and results |
| `POST /api/runs/clear` | Empties the archive |
| `POST /api/batch` | Comparison: scenario settings (same names as above) plus `strategies=Optimized,Nearest`, `parkings=Stay,Lobby`, `layouts=8\|1-20:6, 21-39:6` (layouts separated by `\|`) |
| `POST /api/batch/cancel` | Stops the comparison after the current variant |
| `POST /api/twin` | Two buildings: scenario settings plus `leftDispatchStrategy`, `leftIdleParking`, `leftLayout`, `leftMaxPeople` and the same with `right` |
| `POST /api/twin/stop` | Back to one building |

In a twin run `GET /api/state` has a `twin` member with the cabins, passengers, statistics and run of the right
building; `POST /api/elevator` and `GET /api/report` take `building=twin` for it, and every event has a `b` field
(`""` for the main building, `"twin"` for the other).

In web mode the log goes to its file only, from `Warning` up, because hours of simulated traffic at high speed would
produce a very large file.

## Comparing strategies

### Strategies and idle cabins

`dispatchStrategy` decides which cabin answers a call:

| Strategy | Rule |
| --- | --- |
| `Optimized` (default) | The dispatcher of the original project: the route of every cabin is simulated with and without the call, the call goes where it adds the least passenger time (see [Elevator assignment](#elevator-assignment)). |
| `Nearest` | The closest cabin in service with room for the passenger, whatever it is doing. |
| `Collective` | The classic directional collective control: the closest among the idle cabins and those already sweeping towards the call in its direction; a cabin that has to turn around first counts as if it were two building heights further away. |

All three respect the cabin capacity and skip the cabins out of order. `idleParking` decides where a cabin with
nothing to do waits: `Stay` where it stopped, `Lobby` at floor 0, `Spread` at a home floor in the middle of an equal
zone of the building (4 cabins in 20 floors wait at 2, 7, 12 and 17), `Peak` at the lobby of its group only during a
rush from that lobby, where it stopped otherwise.

`Peak` reads the traffic, it does not need hours: over the assignments of a group in the last 10 simulated minutes,
a rush starts when at least 5 went up from its lobby, they are at least 40% of all of them and at least twice as many
as the trips down to the lobby (offices in the morning, homes in the evening; lunch is balanced and is not a rush).
It ends when the imbalance is gone. Each group has its own lobby (a sky lobby for the zones above it), and the start
and the end of every rush appear in *What happened*. Measured on full days (office unless noted):

| Building | Stay: wait / journey / energy per trip | Peak: wait / journey / energy per trip |
| --- | --- | --- |
| 20 floors, 6 cabins | 9.3 s / 29.6 s / 50 Wh | 8.0 s / 27.5 s / 60 Wh |
| 20 floors, 6 cabins, homes | 9.5 s / 29.6 s / 51 Wh | 8.2 s / 27.7 s / 61 Wh |
| 60 floors, 3 zones of 8 | 7.4 s / 35.0 s / 180 Wh | 6.8 s / 33.0 s / 223 Wh |
| 60 floors, sky lobby at 30 | 6.4 s / 38.9 s / 161 Wh | 6.0 s / 36.8 s / 192 Wh |

It shortens waits and journeys everywhere, for 20-25% more energy (the empty cabins going back to the lobby);
`Lobby` all day costs more energy still and does worse than `Stay` outside the rush hours. A first version that only
counted the trips up from the lobby stayed in rush all day in the high zones, where every trip passes the lobby: there
it made things worse; the imbalance rule fixed it. A parked cabin answers a call from the floor it
has reached, so parking costs travel and energy but can shorten the next wait.

### Scenarios

The passengers of a run are decided before it starts, from the seed (`randomSeed`, a new random one when it is 0)
and the traffic settings: floors, trips per day, building type, days, start hour, call delays, patience. Two runs with
the same scenario face **exactly the same people at the same simulated times**, whatever their cabins, strategy or
parking: their results can be compared. Every run shows its scenario key and seed; the archive groups runs by
scenario.

Calls, waves and faults made by hand make a run *hands-on*: it is still archived, but kept apart from the comparable
runs (the results table hides it unless asked).

### Comparisons and results

*Compare strategies* takes a scenario (by default the passengers of the run on screen) and a set of variants:
strategies × idle cabins rules × layouts of cabins and zones (one per line), at most 24. The variants run one after the other at the highest
speed (2000x: a whole day in about 40 seconds) and the results open by themselves at the end. During a comparison
the interface does not accept commands that would change the runs; you can still watch, change the speed or stop it.

Every run that ends (its last passenger has arrived) is saved in `runs.jsonl` (`runsFilePath`), one JSON object per
line, read again at the next start. *Results* shows the runs of a scenario with the best value of every column,
charts of the main metrics and a CSV export of the whole archive. The console version adds its completed runs to the
same file.

An example on the default building (10 floors, office, 2000 trips per day, 4 cabins, one day from 06:00, same
scenario for every row):

| Strategy | Idle cabins | Average wait | 95% wait | Took the stairs | Energy | Wh per trip |
| --- | --- | --- | --- | --- | --- | --- |
| Optimized | Stay | 7.1 s | 17.9 s | 0 | 59.6 kWh | 33.8 |
| Optimized | Spread | 4.6 s | 9.9 s | 0 | 78.3 kWh | 44.4 |
| Optimized | Lobby | 7.9 s | 19.9 s | 0 | 85.4 kWh | 48.5 |
| Collective | Stay | 9.6 s | 35.0 s | 0 | 57.0 kWh | 32.4 |
| Nearest | Stay | 11.1 s | 39.8 s | 4 | 56.2 kWh | 32.0 |

Spreading the idle cabins halves the waits of the optimized dispatcher but costs about 30% more energy; sending them
back to the lobby all day costs even more and does not help an office outside the morning peak.

### Two buildings

*Two buildings…* shows two variants live, side by side: same floors and passengers (same seed and traffic), on the
same simulated clock, with their own strategy, idle cabin rule, number of cabins and capacity. The panel compares
waits, stairs and energy as they happen (green is the better side), the history draws both queues, the feed and the
dispatcher say which side an event comes from. A passenger you call or a wave you send appears in both buildings at
once; a fault only hits the cabin you click. When both buildings are done the two results are archived as a pair and
the results open on their scenario.

Under the hood the original model keeps its building as global data (`Configuration::Get()`, `Floors::GetPeople()`,
`Statistics`). The fork keeps that interface but moves the data into a `BuildingContext`: every thread of a building
(cabins, dispatcher, generator) runs inside its context, so the model code is unchanged and two buildings do not see
each other. The simulated clock stays shared, and each building has its own stop flag, so one can be stopped while the
other keeps running. The test suite also runs clean under ThreadSanitizer.

### Large buildings

Measured on a 2-core machine, office traffic, one day from 07:00 at 2000x (a single group of cabins serving every floor):

| Building | Cabins | Trips | Average wait | 95% wait | Stairs | Energy per trip | Dispatch decision (avg / max) | CPU |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 20 floors | 4 / 6 / 8 | 3983 | 11.2 / 9.2 / 8.8 s | 28.6 / 24.5 / 24.5 s | 6 / 0 / 0 | 48 / 50 / 52 Wh | 0.07 / 6 ms | 11% |
| 40 floors | 8 / 12 / 16 | 7965 | 11.3 / 10.4 / 10.3 s | 30.8 / 29.8 / 29.9 s | 5 / 0 / 0 | 88 / 89 / 91 Wh | 0.1 / 13 ms | 23% |
| 60 floors | 12 / 18 / 24 | 11883 | 12.1 / 11.4 / 11.3 s | 34.1 / 33.3 / 33.2 s | 5 / 1 / 3 | 111 / 113 / 115 Wh | 0.16 / 15 ms | 38% |

The program scales well: one thread per cabin, a dispatch decision well under a millisecond on average even with
24 cabins, a full day of a 60-floor building in about 30 seconds. Beyond the first extra cabins the waits barely
improve, while the energy per trip keeps growing (standby of every cabin): the comparison shows where adding
cabins stops paying off. With 24 cabins `Nearest` instead of `Optimized` doubles the average wait (26 s) and sends
about 1250 people to the stairs: the strategy matters more than the number of cabins.

At 2000x the averages match a slower run within a few percent, but the operating system timers sometimes delay a
cabin and the tail (longest wait, stairs) can come out worse: one run out of four gave 52 stairs instead of 0-3. For
the tails, repeat the run or use a lower speed (360x gives the same averages and no such outliers).

A single group of 24 cabins over 60 floors is a stress test, not a real design: real towers split their floors into
zones (next section). Without zones the cabin limit is 26 (identifiers A-Z), with zones 64; the floor limit is 60.
Above about 40 floors or 16 cabins the drawing becomes small (floors of 12 px on a laptop screen) but stays usable.

### Zones and sky lobbies

Tall buildings split their floors into zones, each served by its own group of cabins. The layout of the cabins is
one setting (`zones` and `skyLobbies` in `config.json`, a single *Cabins and zones* field in the interface):

| Layout | Meaning |
| --- | --- |
| `8` | One group of 8 cabins serving every floor (the original model). |
| `1-20:6, 21-39:6` | Two zones served from the lobby (floor 0): 6 cabins for floors 1-20, 6 for floors 21-39. |
| `1-29:8, 31-45:6, 46-59:6 @ 30:4` | A sky lobby at floor 30, reached from the ground floor by a shuttle group of 4 cabins; the zones above it start from there. |

The zones must cover every floor above the lobby exactly once (a sky lobby floor is not in a zone). The cabins of a
group are named after it (`A1`, `A2`... `B1`... `S1` for the shuttles) and only stop on its floors: on the others they
run express, faster (`expressFloorFactor`, 0.35 of the time of a normal floor). The dispatcher plans every trip leg by
leg: a passenger whose origin and destination are not served by the same group goes to a lobby (the ground floor or
a sky lobby), gets off, and waits there for a cabin of the next group, keeping his original queue time. The
statistics count the changes of cabin and the wait at the change; the journey time includes everything. On screen
the floors a cabin passes express are darker, each group has its colour, and passengers waiting after a change have a
blue ring.

Measured on the same scenarios (office, one day from 07:00, 2000x):

| Building | Layout | Wait | Journey | 95% wait | Changes | Wait at the change | Energy per trip |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 40 floors, 12 cabins | one group | 10.4 s | 36.4 s | 29.8 s | 0 | | 89 Wh |
| 40 floors, 12 cabins | 2 zones of 6 | 8.4 s | 34.1 s | 21.9 s | 1835 | 4.5 s | 112 Wh |
| 60 floors, 24 cabins | one group | 11.2 s | 41.7 s | 33.1 s | 0 | | 116 Wh |
| 60 floors, 24 cabins | 3 zones of 8 | 7.8 s | 35.9 s | 22.2 s | 3727 | 3.7 s | 168 Wh |
| 60 floors, 24 cabins | sky lobby at 30 | 6.5 s | 39.0 s | 17.0 s | 9282 | 4.2 s | 158 Wh |

Zones shorten waits and journeys, but in this model they cost energy: the cabins of the high zones travel the whole
express section, mostly empty on the way down (no regenerative drive is modelled). At 60 floors the sky lobby gives
the shortest waits but longer journeys than conventional zoning, because most trips need a change: its real
advantage, fewer shafts and more floor area in very tall towers, is not part of the model. The dispatcher
predicts every route floor by floor, with the express time on the floors a cabin passes without stopping: its pickup
estimates are within about half a second of the real waits in every group (before, it counted express floors as
normal ones and was 4-7 s pessimistic for shuttles and high zones). Being exact did not make it better: on 60 floors
with 3 zones the average wait went from 6.85 to 7.15 s and the journey from 36.3 to 36.1 s (three runs each). The
dispatcher decides one call at a time, and the old pessimism probably worked as an accidental rule that kept the
cabins of the high zones from being sent down to the lobby for a single passenger (not verified); a rule for the
morning peak at the lobby would be the way to get that effect on purpose.

### The stairs

### The stairs

A small tribute to a classic of the early '90s, where little walkers followed each other in single file: every
passenger of the scenario has a patience (on average `stairsPatienceSeconds`, between half and one and a half times
that value; 0 = infinite). Who waits longer gives up, leaves the queue and walks to the door of the stairs at the end
of the landing, one after the other. They are counted as *took the stairs* in the statistics, the report and the
comparisons: a strategy that leaves people waiting pays for it. Your own passengers are patient and never give up.
A cabin that was coming for somebody who left stops at an empty landing, as in real buildings.

### Served quickly

The other side of the stairs: a passenger who waited on the floors at most `happyWaitSeconds` in all (default 20 s,
the time after which the waiting people turn from green to amber; with changes of cabin every wait counts) arrives
happy. On screen he steps out of the cabin, jumps with his arms up and a 👍 rises over him (at high speed only some
of them are drawn, the count is exact), as a 🤬 rises over the ones walking to the stairs. The share of passengers served quickly is in the panel, in the twin table,
in the report and in the results, where more is better. The threshold only evaluates the runs: it does not change
the scenario.

## Energy

A simple model of a traction elevator with counterweight (`src/Energy.h`), enough to compare strategies and
configurations, not to size a real installation:

| Part | Model |
| --- | --- |
| Travel | The counterweight balances the empty cabin plus half of the rated load (75 kg per person). Moving one floor (3 m) costs the work to lift the heavier side, divided by `motorEfficiency`, when that side goes up (a full cabin going up, an empty cabin going down), plus 1.5 Wh of friction; otherwise only the friction (no regenerative drive). |
| Runs and doors | 1 Wh for every start and stop of the motor, 0.15 Wh for every door cycle. |
| Standby | `standbyPowerW` (default 200 W) for every cabin, for the whole run, in service or not: one more cabin always costs energy, even when idle. |

The interface shows the energy live (hover for travel and standby), the report and the archive keep travel, standby,
total and energy per delivered trip.

## Simulated day

The whole building runs on a simulated clock (`SimulationClock`): a day lasts 24 simulated hours and every duration of
the model (floor travel, doors, boarding, delays between calls) is expressed in that same time, so the proportions of
a real day are kept at any speed. `dayDurationSeconds` only says how long a day lasts on the wall clock (`1440` = 24
minutes, i.e. 60 times faster than real time); the speed can be changed while the simulation runs, and the clock can
be paused or advanced by a step. Every thread waits through the clock, so pausing freezes the whole building.

At high speed the operating system timers wake the threads a little late; consecutive waits of the same thread are
chained on the simulated time line, so these delays do not accumulate (a test checks the drift at 2000x).

The run starts at `dayStartHour` of day 1 and the last passenger of the scenario appears before midnight of the last
day; the run ends when every passenger has arrived (by lift or by the stairs). The intensity of the calls follows the daily profile of the original project (morning,
lunch and evening peaks over a base traffic); arrivals are random around that intensity (a Poisson process). With
`trafficPattern` the peaks also have a direction:

| Pattern | Morning peak | Lunch | Evening peak | Rest of the day |
| --- | --- | --- | --- | --- |
| `Office` (default) | from the lobby (floor 0) up | to and from the lobby | down to the lobby | random trips |
| `Residential` | down to the lobby | to and from the lobby | from the lobby up | random trips |
| `Uniform` | random trips (the original behaviour) | | | |

## Configuration

Simulation parameters are loaded from `config.json` at startup, so values such as the
number of elevators, floors, call generation, timing and log verbosity can be changed
without recompiling.

Configuration fields. In the table below the dotted names (for example
`building.numberOfElevators`) only indicate the logical group of each setting: the shipped
`config.json` is a flat object and uses just the last part (`numberOfElevators`). Keys are looked up
by name, so a nested layout with the same key names is accepted too. Invalid values (negative,
out of range, unknown enum names) stop the program at startup with an explanatory message.

| Setting | What it changes | How to choose a value |
| --- | --- | --- |
| `building.numberOfElevators` | How many elevators are available in the building. | Use `1` or more. Higher values make the system handle calls more easily. |
| `building.numberOfFloors` | How many floors the building has. | From `2` to `60`. The first floor is `0`, so `5` means floors from `0` to `4`. |
| `callsGenerator.type` | How passenger calls are created. | Use `Random` for automatic random calls, or `Fixed` to replay a predefined set of calls. |
| `callsGenerator.numberOfCalls` | How many random calls are generated before the generator stops. | Used only with `Random`. Use values like `5`, `20` or `100`; use `4294967295` only when you want calls to continue until the program is stopped. |
| `callsGenerator.averageCallsPerDay` | Average number of calls generated in one simulated day (24 hours): it sets the base rate of the daily traffic profile. | Used only with `Random`. The shipped configuration uses `2000` (a 10-floor office). The resulting delay between two calls is always kept between `minDelayBetweenCallsMs` and `maxDelayBetweenCallsMs`. |
| `callsGenerator.maxConcurrentCalls` | Maximum number of active calls/passengers in the simulated building. | Use a finite population-like limit such as `600`. When the limit is reached, the generator pauses until passengers complete their trips. |
| `callsGenerator.numberOfSimulationDays` | How many simulated days the random generator should run. | Use `1` or more. The generator stops at midnight of the last day, or earlier if `numberOfCalls` is reached. |
| `callsGenerator.trafficPattern` | Who travels when (see [Simulated day](#simulated-day)). | `Office` (default), `Residential` or `Uniform` (random trips, as in the original project). |
| `callsGenerator.stairsPatienceSeconds` | Average patience of a passenger before taking the stairs, simulated seconds. | Default `120`; every passenger gets between half and one and a half times this value. `0` = nobody ever gives up. |
| `callsGenerator.happyWaitSeconds` | A passenger who waited at most this long in all arrives served quickly (👍). | Default `20`. Only an evaluation: it does not change the scenario. |
| `dispatch.dispatchStrategy` | Which cabin answers a call (see [Comparing strategies](#comparing-strategies)). | `Optimized` (default), `Nearest` or `Collective`. |
| `dispatch.idleParking` | Where an idle cabin waits. | `Stay` (default), `Lobby`, `Spread` (within the floors of its group) or `Peak` (at the lobby during a rush detected in the traffic). |
| `zoning.zones` | Zones of the building and their cabins (see [Zones and sky lobbies](#zones-and-sky-lobbies)). | Empty (default): one group of `numberOfElevators` cabins. E.g. `"1-20:6, 21-39:6"`; the cabins are then the ones of the zones. |
| `zoning.skyLobbies` | Sky lobbies and the cabins of their shuttle from the ground floor. | Empty (default). E.g. `"30:4"`. |
| `zoning.expressFloorFactor` | Time to pass a floor the cabin does not serve, as a share of `timeToReachTheNextFloorMs`. | Default `0.35`. |
| `energy.standbyPowerW` | Standby power of every cabin, watts. | Default `200`. |
| `energy.motorEfficiency` | Efficiency of the motor and gears, 0-1. | Default `0.75`. |
| `results.runsFilePath` | File of the archive of the completed runs. | Default `runs.jsonl`, next to the program. |
| `callsGenerator.minDelayBetweenCallsMs` | The shortest wait between two generated calls, in simulated time. | Default `500`. This value must not be higher than `maxDelayBetweenCallsMs`. |
| `callsGenerator.maxDelayBetweenCallsMs` | The longest wait between two generated calls, in simulated time. | Default `900000` (15 minutes): at night the building is quiet but not dead. |
| `elevator.timeToReachTheNextFloorMs` | How long an elevator takes to move from one floor to the next. | Time is expressed in milliseconds. Use a value greater than `0`; higher values make elevators slower. |
| `elevator.enterAndExitTimeMs` | How long doors stay open while people enter and exit. | Time is expressed in milliseconds. Higher values make every stop last longer. |
| `elevator.doorTransitionTimeMs` | How long the doors take to open, and to close (each transition). | Time is expressed in milliseconds. Default `1000`. Every stop costs one opening and one closing, so with fast floors this value dominates the trip time. It is used both by the cabins and by the dispatcher prediction. |
| `elevator.maxPeople` | Maximum number of people allowed in a cabin. | The dispatcher simulates the route of every cabin and never assigns a call that would make more than `maxPeople` people ride together. Passengers whose rides do not overlap do not count against each other. |
| `simulation.dayDurationSeconds` | How long a simulated day (24 hours) lasts on the wall clock, in seconds: it sets the initial speed. | Default `1440` (24 minutes, 60 times faster than real time); `86400` is real time, the minimum `43.2` is a day in 43 seconds (2000x). All the other durations stay expressed in simulated time and the report is in simulated time, so results are comparable between speeds. Replaces `simulationDayDurationMs` and `timeScale` of the original project, which are now refused with an explanation. |
| `simulation.dayStartHour` | Hour of day 1 at which the simulation starts. | Default `6` (06:00), so that a run does not begin with a night. Decimals allowed (`7.5` = 07:30). |
| `callsGenerator.randomSeed` | Seed of the random calls generator. | `0` (default) picks a different seed at every run. Use a fixed value to get the same sequence of floors and delays. The seed used is written to the log. The threads timing is not deterministic, so results of two runs are close but not identical. |
| `simulation.autoStop` | Stops the program when the simulation is over. | `true` (default): when all calls have been generated and served the final report is printed and the program exits. `false`: the program runs until `Enter` is pressed. |
| `log.traceLevel` | How much detail is printed in the log. | Use `Debug` for the most detail, then `Verbose`, `Info`, `Warning`, or `Error` for progressively quieter output. |
| `log.defaultLogType` | Where log messages are written. | Use `Screen` for console only, `File` for file only, or `ScreenAndFile`/`Both` for both outputs. |
| `log.filePath` | File used when file logging is enabled. | Relative paths are resolved from the program working directory. The default is `elevator.log`. |

## Logging

The simulator can write logs to different destinations through `log.defaultLogType`:

| Value | Behavior |
| --- | --- |
| `Screen` | Default mode. Shows the dashboard and rolling log in the console. |
| `File` | Writes log messages only to `log.filePath`; the console dashboard is not drawn. Useful for unattended or repeatable runs. |
| `ScreenAndFile` | Shows the console UI and also writes the same log stream to file. |
| `Both` | Alias for `ScreenAndFile`. |

Example:

```json
"log": {
  "traceLevel": "Info",
  "defaultLogType": "ScreenAndFile",
  "filePath": "elevator.log"
}
```

When file logging is enabled, the log file is recreated at the first log write of
the run and then appended during the same execution. The final statistics report is
also written to the file after pending asynchronous log messages are flushed.

## Elevator assignment

When a passenger call is generated, the management system decides which elevator takes it by
**simulating the route of every cabin**, with and without the new call, and choosing the cabin for which the
call costs the least total passenger time.

The simulation uses the same stop-selection rule as the real cabins (`Floors::FindNextStop`), the configured
floor, door and boarding times, and the exact set of passengers already inside each cabin or assigned to it and still
waiting. For every cabin it computes when each passenger will be picked up and dropped off.

The cost of a candidate is the extra weighted passenger time caused by the call:

| Factor | Meaning |
| --- | --- |
| Own wait and ride | Time until the new passenger is picked up (weighted x2: waiting on a floor hurts more) and then delivered. |
| Delay inflicted to others | Passengers already assigned to that cabin that get picked up or delivered later because of the new stops. Joining a stop that is already scheduled is nearly free. |
| Waiting age | The delay inflicted to a passenger who has already waited a long time counts up to 4 times more, so nobody is postponed over and over (starvation). |
| Capacity | Exact: the simulated route must never carry more than `maxPeople` people at the same time. Cabins that would exceed it are excluded; if all of them are, the call waits in the pending queue. |
| Wear leveling | Travelled floors, runs and door cycles add a tiny penalty, normalized among the candidates and never worth more than one floor of travel. It only breaks ties. |

If no cabin can take the call because of its capacity, the call goes to a pending queue that is retried
as soon as passengers leave a cabin (and periodically). A passenger who finds a full cabin anyway
(boarding refused) is automatically given back to the dispatcher instead of being left on the floor.

## Final statistics

When the simulation stops, the dashboard is replaced by a full final report. This
report is not limited by the normal rolling log window, so all configured elevators
are shown.
Long table values are wrapped onto continuation rows so recommendation reasons and
other explanatory fields keep the report aligned instead of breaking the table
layout.

If file logging is enabled, the same final report is appended to the configured log
file. This is useful when the screen report is long or when you want to compare
multiple runs after changing `config.json`.

The first table summarizes passenger flow:

| Metric | Meaning |
| --- | --- |
| `Queued calls` | Calls created and inserted into the waiting queue. |
| `Assigned calls` | Calls accepted by an elevator. |
| `Boarded passengers` | Passengers who entered an elevator. |
| `Completed passengers` | Passengers who reached their destination floor. |
| `Simulation time` | Current simulated day and time of day when the report is printed. The same value is shown live in the symbolic interface. |
| `Average wait` | Average time from call creation to passenger boarding, shown in seconds. |
| `Max wait` | Longest observed waiting time, shown in seconds. |
| `Wait p50` / `Wait p95` / `Wait p99` | Median, 95th and 99th percentile of the waiting time: how long *most* passengers wait and how bad the tail is. |
| `Average journey` / `Max journey` | Time from the call to the arrival at the destination (wait + ride). |

All times are simulated times (see [Simulated day](#simulated-day)).

The anomaly table highlights situations worth investigating:

| Indicator | Meaning |
| --- | --- |
| `Forced assignments` | Calls assigned to a cabin that had to reverse its sweep before picking the passenger up. Normal in a busy building, so it is context (`INFO`), not an anomaly. |
| `No available elevator` | Dispatch decisions where every cabin had to reverse its sweep: the saturation signal (`WARN` above 10% of the decisions). |
| `Requeued calls` | Passengers who found a full cabin and were dispatched again. It should be `0`: the dispatcher prevents it by simulating the load. |
| `Oldest waiting call` | Waiting time of the call still on a floor for the longest time when the report is printed. |
| `Capacity limit waits` | Calls that had to wait in the pending queue because every cabin had already reached its configured committed load (counted once per call, not once per retry). |
| `Pending boarding` | Calls still waiting when the simulation was stopped. |
| `Pending completion` | Passengers still inside elevators when the simulation was stopped. |
| `Avg assignment time` | Average time spent by the dispatch algorithm to assign a call. |
| `Max assignment time` | Slowest dispatch decision observed. |
| `Max wait / avg wait` | Tail-latency indicator: high values mean some users waited much longer than average. |

Each indicator has a simple level:

| Level | Meaning |
| --- | --- |
| `OK` | Normal condition. |
| `INFO` | Not necessarily a problem, but useful context. |
| `WARN` | Potential anomaly or inefficiency to inspect. |

The final elevator table shows one row per configured elevator:

| Metric | Meaning |
| --- | --- |
| `Assigned calls` | Number of calls assigned to that elevator. Useful to detect load imbalance. |
| `Travelled floors` | Total floors travelled by the cabin. |
| `Runs` | Number of movement runs started. |
| `Door cycles` | Number of door close cycles, a proxy for door wear. |
| `Queued stops` | Stops still queued when the report is printed. |
| `People on board` | Passengers still inside the cabin when the report is printed. |

The report also includes a fleet sizing recommendation:

| Parameter | Meaning |
| --- | --- |
| `Configured elevators` | Number of elevators from `building.numberOfElevators`. |
| `Recommended elevators` | Suggested number of elevators for the observed run. |
| `Sizing reason` | Short explanation of the recommendation. |
| `Target avg wait` | Heuristic service target derived from movement and stop timing. |
| `Observed avg wait` | Average wait measured during the run. |
| `Forced assignment rate` | Share of dispatch decisions that required a reversal (information only, not used for the recommendation). |
| `No-available rate` | Share of dispatch decisions where every elevator had to reverse (used for the recommendation). |
| `Unassigned elevators` | Elevators that received no calls during the run. |

The recommendation is conservative: it suggests adding elevators when waits,
backlog or fallback rates indicate pressure; it suggests reducing only when service
is comfortable and at least one elevator did no useful work.

The report also includes a cabin sizing recommendation:

| Parameter | Meaning |
| --- | --- |
| `Configured max people` | Current cabin capacity from `elevator.maxPeople`. |
| `Recommended max people` | Suggested number of people each cabin should carry for the observed load. |
| `Sizing reason` | Short explanation of the recommendation. |
| `Peak people on board` | Highest observed number of passengers physically inside a cabin. |
| `Peak committed load` | Highest observed cabin load including both people on board and people assigned but not boarded yet. |
| `Capacity limit waits` | Number of times dispatch had to wait because all cabins were at the configured committed load. |

Possible future improvements:
- A dispatcher that keeps cabins in reserve at the lobby during a rush, not only the idle ones
- More than two buildings side by side, or a comparison in series run as parallel buildings
