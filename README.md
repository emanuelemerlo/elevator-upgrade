# elevator playground

A fork of [elevator](https://github.com/emanuelemerlo/elevator) that keeps its C++ simulation (threads, dispatcher,
statistics, console dashboard) and adds a browser interface to play with it: the building runs through a simulated
day, you can call lifts yourself, send waves of people, put cabins out of order and start new runs with other settings.

```sh
make
./Elevator.exe --web        # then open http://127.0.0.1:8080/
./Elevator.exe              # the original console simulation
```

See [Web interface](#web-interface) and [Simulated day](#simulated-day) below; the rest of this file is the
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

On Windows, the VS Code build task produces `Elevator.exe`. The task compiles all
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

### Tests

```sh
make test
```

builds and runs every program in `tests/`:

- `AssignmentOptimizerTests.cpp` checks the route simulation used by the dispatcher (arrival times, sweep direction,
  capacity, shared stops, anti-starvation weight) and the stop logic of the floors;
- `SimulationTests.cpp` checks the simulated clock (pause, step, speed changes, abort, no drift at high speed) and whole
  runs at high speed: a run ends by itself with every passenger delivered, a cabin put out of order while travelling
  stops at the next floor and its passenger completes the trip with another cabin, a restart applies new settings.

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
| `POST /api/restart` | New run; the parameters have the names of `config.json` (`numberOfElevators`, `numberOfFloors`, `dayDurationSeconds`, ...) |

In web mode the log goes to its file only, from `Warning` up, because hours of simulated traffic at high speed would
produce a very large file.

## Simulated day

The whole building runs on a simulated clock (`SimulationClock`): a day lasts 24 simulated hours and every duration of
the model (floor travel, doors, boarding, delays between calls) is expressed in that same time, so the proportions of
a real day are kept at any speed. `dayDurationSeconds` only says how long a day lasts on the wall clock (`1440` = 24
minutes, i.e. 60 times faster than real time); the speed can be changed while the simulation runs, and the clock can
be paused or advanced by a step. Every thread waits through the clock, so pausing freezes the whole building.

At high speed the operating system timers wake the threads a little late; consecutive waits of the same thread are
chained on the simulated time line, so these delays do not accumulate (a test checks the drift at 2000x).

The run starts at `dayStartHour` of day 1 and the generator stops at midnight of the last day; the run ends when the
last passenger has arrived. The intensity of the calls follows the daily profile of the original project (morning,
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
- Idle cabins parking (e.g. spread the empty cabins, send them to the lobby before the morning peak)
- Game layer on top of the web interface: goals, scores, budgets, comparison of dispatching strategies
