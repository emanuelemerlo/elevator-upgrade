# History and overview

*[Leggi in italiano](Storia-e-panoramica)*

## Where it comes from

In the 1980s the Italian magazine *Commodore Computer Club* launched a contest: write a program that simulates an
elevator on the Commodore 64. The author was a child at the time and could not take part, but the idea of trying it,
almost as a game, stayed with him. Decades later he tried again, this time in C++ with the standard library.

The model chosen is not the classic one with buttons inside the cabin. The passenger chooses the **destination floor
at the call**, on the landing; a central "management" receives the call and gives it to the most suitable cabin. This
is *destination dispatch*, the system installed in many modern office buildings (and in the one where the author
works), and it leaves much more room for optimization than the traditional system, because the dispatcher knows where
everybody is going before choosing the cabin.

From the start the project was meant as **a gym**: a small but real program to practise threads, synchronization,
collections and modelling.

## Timeline

| When | What |
| --- | --- |
| October-November 2019 | First version in C++: one thread per cabin, calls generator, management, log on a separate thread, Windows and Linux builds. Several refactorings of the threads and of the shutdown. |
| May-June 2026 | Symbolic console dashboard, logging to file, simulated days with a daily traffic profile, final report with statistics and sizing recommendations. |
| October 2026 | New dispatcher: the route of every cabin is simulated with and without the new call, the call goes where it costs the least passenger time. Measured improvements, first tests. |
| November 2026 | **elevator-upgrade**: the fork described in this wiki. Simulated clock, embedded web server and browser interface, reproducible scenarios, strategy comparisons, energy model, two buildings side by side, zones and sky lobbies for tall buildings. |

The fork keeps the nature of the original: it is still a C++ simulation with the same core (cabins, dispatcher,
statistics) and the console version still works as before. The web interface is one more way to look at it and to
play with it, not a rewrite.

## Not a game, a simulation you can play with

The interface is meant to be pleasant and a little playful: the sky follows the hour, people queue on the floors and
change colour while they wait, those who lose patience walk to the stairs with a 🤬, those served quickly cheer with a
👍. But the purpose stays the one of a simulator: **comparing dispatch strategies and building configurations on
exactly the same passengers**, with waits, journeys, energy and the people who gave up.

## Why it is interesting

For those who want to learn or to look at a complete example, the project touches many classic topics of
concurrent and system programming, in a domain everybody understands.

**Threads and synchronization**
- **One thread per cabin**, plus the calls generator, the dispatcher worker, the log writer and the web server: a
  real multi-threaded program where every thread does one simple job.
- **Producer/consumer**, in several places. The log: every component *produces* messages into a shared queue, a
  single thread *consumes* them and writes to the screen or to the file (`LogBase`). The dispatcher: calls that do
  not fit any cabin go to a pending queue consumed by a worker that retries them (`Management`). The event stream:
  the simulation produces events into a bounded buffer, the web interface consumes them at its own pace by sequence
  number (`EventBus`).
- **Condition variables with predicates**, to avoid the classic lost wake-ups and spurious wake-ups; the code
  comments explain each case.
- **A reusable worker thread template** (`WorkerThread<T>`, the *template method* pattern: the class manages start,
  wait and stop, the derived class only writes its cycle) and a **watchdog** that reports a shutdown taking too long.
- **Thread-local context**: the original model keeps the building in global data; the fork makes that data belong to
  the building of the calling thread, so that two buildings can run side by side without rewriting the model.
- The tests also run under **ThreadSanitizer**, which found and helped fix real data races.

**Simulation**
- A **simulated clock**: a day lasts 24 simulated hours, every duration is in simulated time, and the clock can run
  from real time up to a whole day in 43 seconds, be paused or advanced by a step. At high speed the waits of a thread
  are chained on the simulated time line so that the operating system timers do not accumulate drift.
- A **dispatcher based on route simulation**, with capacity, aging against starvation, wear levelling, and simpler
  strategies (nearest cabin, directional collective control) to compare with.
- **Reproducible scenarios**: the passengers of a run are generated in advance from a seed, so two runs that differ
  only in the strategy face the same people at the same moments.
- Models of **traffic** (offices, homes), **energy** (counterweight, motor, standby), **patience** and **zoning** of
  tall buildings with express runs, changes of cabin and sky lobbies.

**Web interface without a separate backend**
- An **embedded HTTP server** in the same C++ process (cpp-httplib, a single header) exposes a small JSON API; the
  browser polls the state about ten times per second and **animates the cabins locally** between two answers, from
  the start time and duration of every movement.
- The same API can be used from scripts: comparisons of strategies can be automated without the interface.

## What it is not

- It does not size a real installation: timings, traffic and energy are plausible orders of magnitude, enough to
  compare alternatives, not to design an elevator.
- Results at the highest speeds are reliable on average, less on the extremes (longest wait, people on the stairs):
  see the notes in the [User manual](User-manual#reading-the-results).
