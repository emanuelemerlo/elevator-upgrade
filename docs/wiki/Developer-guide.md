# Developer guide

*[Leggi in italiano](Guida-per-sviluppatori)*

For those who want to read the code, change it or add something. Code and comments are in English.

## Source map

| Area | Files | Role |
| --- | --- | --- |
| Entry point | `Main.cpp` | Command line, console mode (symbolic dashboard) or web mode. |
| Run | `Simulation` | One run of a building: start, stop, restart with new settings, manual calls, waves, faults, archive, comparisons in series. |
| Clock | `SimulationClock` | The simulated time line: speed, pause, step, abort; every wait of the model goes through it. |
| Building | `BuildingContext` | Settings, waiting passengers, statistics and stop flag of one building; the thread-local "current building". |
| Configuration | `Configuration` | Settings, parsing of `config.json`, validation. |
| Cabins | `Elevator` | One thread per cabin: doors, movement, boarding, express floors, parking, out of order. |
| Floors | `Floors` | Stops of a cabin and the next-stop rule (directional LOOK), shared with the dispatcher. |
| Passengers | `Call`, `People` | A passenger (call) and a thread-safe list of passengers (waiting on the floors or inside a cabin). |
| Dispatcher | `Management`, `AssignmentOptimizer` | Groups of cabins, choice of the cabin, pending queue, stairs, rush detection; route simulation (pure functions). |
| Zones | `Zoning` | Layouts, groups and route planning through the lobbies. |
| Traffic | `TrafficProfile`, `Scenario`, `PeopleCallsGenerator` | Daily profile, the passengers of a run decided in advance, the thread that releases them on time. |
| Results | `Statistics`, `Energy.h`, `RunArchive` | Counters and wait distribution, energy model, archive of the completed runs. |
| Events | `EventBus`, `Json.h` | Stream of the simulation events, minimal JSON writer. |
| Web | `WebServer`, `web/` | Embedded HTTP server and JSON API; the browser interface (HTML, CSS, JavaScript, no framework). |
| Console and log | `SymbolicInterface`, `ConsoleView`, `Log*`, `ILog` | Console dashboard and the asynchronous log. |
| Infrastructure | `WorkerThread.h`, `Watchdog.h` | Reusable worker thread, timeout watchdog. |
| Tests | `tests/` | `AssignmentOptimizerTests` (route simulation), `SimulationTests` (clock, whole runs, faults, scenarios, strategies, twins, zones, rushes). |

## Threads

| Thread | Created by | Does |
| --- | --- | --- |
| Main | — | Console: waits for Enter or for the end of the run. Web: waits for Enter. |
| One per cabin | `Elevator` | Waits for a call, moves floor by floor, opens the doors, lets people in and out, parks. |
| Dispatcher worker | `Management` | Retries the pending calls, sends the impatient passengers to the stairs, detects the rushes. |
| Calls generator | `PeopleCallsGenerator` (a `WorkerThread`) | Sleeps until the time of the next passenger of the scenario, then inserts and assigns him. |
| Log writer | `LogBase` (a `WorkerThread`) | Consumes the log queue and writes to the screen or the file. |
| Console dashboard | `SymbolicInterface` | Draws the symbolic view every half second (console mode). |
| Web server | cpp-httplib | A pool of threads answering the HTTP requests. |
| Comparison | `Simulation::StartBatch` | Runs the variants one after the other. |

Shared data is protected by small mutexes (every `People` list, the stops of every cabin, the statistics, the event
stream). Waits use condition variables with a predicate. The commands coming from the web hold `Simulation::Lock()`,
so that a restart cannot destroy the cabins while a request reads them.

## The life of a passenger

1. `Scenario::Build` has decided, before the run, when he appears, from where, to where and how patient he is.
2. The generator thread sleeps until that simulated time (`SimulationClock::SleepUntil`), puts him in the waiting
   list of the building (`People::Insert`, event `call`) and asks the dispatcher (`Management::AssignCall`).
3. The dispatcher plans the leg (`Zoning::Plan`: direct, or to a lobby to change cabin), evaluates every cabin of the
   right group (`AssignmentOptimizer::EvaluateAssignment`, or the distance for the simpler strategies), chooses one,
   flags the pickup floor (`Elevator::AnswerToCall`) and publishes the decision (event `assign`). If no cabin has room,
   the call goes to the pending queue.
4. The cabin thread wakes up, chooses its next stop (`Floors::FindNextStop`), moves, opens the doors; in
   `People::EnterAndExit` the passenger moves from the waiting list into the cabin (event `board`).
5. At his destination he gets out (event `arrive`, 👍 if served quickly); at the end of a leg he gets out at the lobby
   and goes back to the dispatcher (event `transfer`). If he waits longer than his patience, the dispatcher worker
   removes him (event `stairs`).

## The simulated clock

`SimulationClock` maps the simulated time onto the wall clock with a speed that can change at any moment. Every wait
of the model is `SleepFor(simulated duration)`: a pause freezes every thread, a speed change wakes the sleeping threads
so that they recompute their real wait, an abort makes every wait return `false` at once. Consecutive sleeps of the
same thread are chained on the simulated time line: if a thread wakes up a little late, its next sleep starts from the
time it should have woken up, so at 2000x the delays of the operating system do not accumulate.

## Two buildings: the building context

The original model reads its building from global functions (`Configuration::Get()`, `Floors::GetPeople()`,
`Statistics::...`). Instead of rewriting it, the fork made those functions return the data of the **building of the
calling thread** (`Context::Current()`, a thread-local pointer). Every thread of a building enters its context
(`Context::Scope`) when it starts; a thread that never entered one uses the default building, which is the only one of
the console version. Two `Simulation`s can therefore run on the same clock without seeing each other; each has its own
stop flag.

## Web API

| Request | Meaning |
| --- | --- |
| `GET /api/state?since=N` | Clock, cabins, waiting passengers, statistics, run, and the events after sequence `N` (`twin` member for the second building). |
| `GET /api/config` | Settings and daily traffic profile. |
| `GET /api/report[?building=twin]` | Final report as text lines. |
| `GET /api/runs`, `POST /api/runs/clear` | Archive of the completed runs. |
| `POST /api/control` | `action=pause`, `resume`, `step` (`ms=`), `speed` (`value=`). |
| `POST /api/call` | `from=F&to=T`. |
| `POST /api/wave` | `kind=morning`, `lunch`, `evening` or `random`, plus `count=N`. |
| `POST /api/elevator` | `id=A&outOfOrder=true` (or `false`), plus `building=twin` for the second building. |
| `POST /api/restart` | New run; parameters with the names of `config.json`, plus `layout`. |
| `POST /api/batch`, `POST /api/batch/cancel` | Comparison: scenario parameters plus `strategies`, `parkings`, `layouts` (separated by a vertical bar). |
| `POST /api/twin`, `POST /api/twin/stop` | Two buildings: scenario parameters plus `left…` and `right…` (`DispatchStrategy`, `IdleParking`, `Layout`, `MaxPeople`). |

Example, a comparison from a script:

```sh
curl -X POST localhost:8080/api/batch \
  -d "randomSeed=42&strategies=Optimized,Nearest&parkings=Stay,Peak&layouts=4|6"
curl localhost:8080/api/runs
```

The browser asks `/api/state` about ten times per second. Between two answers it animates the cabins by itself: the
server sends when every floor transition and door movement started and how long it lasts, and the page advances the
clock at the current speed.

## Extending it

**A new dispatch strategy**
1. `Configuration.h`: add it to `Dispatch::Strategy`; `Configuration.cpp`: parse it (`ParseStrategy`) and name it
   (`StrategyName`).
2. `Management::AssignCallInternal`: give the candidates a `score` in the `switch (strategy)` (lower wins). Every
   candidate has its route evaluation (pickup time, on the way or not, feasibility) and its cabin state.
3. `web/index.html`: add it to the strategy menus and to the comparison checkboxes.
4. A test in `tests/SimulationTests.cpp` (see `Strategies()`).

**A new idle parking rule**: `Dispatch::Parking`, then the `switch` in the `Management` constructor (home floors) or,
for a dynamic rule, a periodic update like `UpdatePeaks()`. A cabin with a home floor goes there by itself when idle
(`Elevator::ParkIfIdle`).

**A new metric**: count it in `Statistics`, add it to the report (`Management::BuildReport`), to the state
(`StatisticsJson` in `WebServer.cpp`), to the archive (`Simulation::Archive`) and to the columns of the results
(`COLUMNS` in `app.js`; `lower: true` or `higher: true` decides which value is the best).

**A new event**: `EventBus::Publish("name", Json::Writer::Object().Add(...))` where it happens; describe it in
`describe()` in `app.js` to show it in the feed.

## Tests and checks

```sh
make test
```

Build the simulation tests with ThreadSanitizer to look for data races:

```sh
g++ -std=c++17 -O1 -g -fsanitize=thread -pthread -Isrc \
  $(ls src/*.cpp | grep -v Main.cpp | grep -v WebServer.cpp) tests/SimulationTests.cpp -o sim_tsan
./sim_tsan
```

The whole runs use high speeds; a test that depends on timing waits with generous timeouts. `traceLevel: "Debug"`
writes what every dispatch decision was based on (`Dispatch state of ...`), to compare the prediction with what the
cabin really does.
