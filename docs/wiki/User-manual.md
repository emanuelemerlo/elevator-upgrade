# User manual

*[Leggi in italiano](Manuale-utente)*

This page describes the web interface (`Elevator.exe --web`, see [Getting started](Getting-started)). The labels of
the interface are in English and are quoted as they appear.

## The screen

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│ 07:42  Day 1 of 1   ⏸  +1 min   Speed ━━━●━━  ×60, a day in 24 min   ▁▃█▃▅▂▃▆▂ │  top bar
├──────────────────────────────────────────────────────────┬──────────────────────────┤
│                                                          │ Right now                │
│        the building in cross-section                     │ Dispatcher               │
│        (one or two buildings)                            │ Stir things up           │
│                                                          │ Runs and comparisons     │
│                                                          │ What happened            │
├──────────────────────────────────────────────────────────┴──────────────────────────┤
│ hint: what a click does                                                             │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

### Top bar

- **Clock**: the simulated hour, the day of the run, *paused* or *Run complete*.
- **⏸ / ▶** (Space): pause and resume. The whole building freezes, also the cabins in the middle of a floor.
- **+1 min** (S): advance one simulated minute and pause again, to follow a situation closely.
- **Speed**: logarithmic slider from real time to 2000x (a day in 43 seconds); presets *Real time*, *×60*, *×360*,
  *×1440*; keys + and − double or halve it. Changing the speed never changes the results: every duration is in
  simulated time.
- **Traffic today**: the daily traffic profile of the building, with the current hour.

### The building

- The **sky** follows the hour: dawn, day, sunset, night with stars; the landings light up in the evening.
- On the left of every floor its number and the green **door of the stairs**.
- **People waiting** queue on the landing towards the lifts and change colour with the wait: green, amber after 20 s,
  red after 45 s. A dark ring marks your own passengers, a red ring the ones who had to leave a broken cabin, a blue
  ring the ones waiting for the next cabin after a change at a lobby. The arrows next to the queue are the call
  buttons, lit when somebody waits to go up or down.
- **Cabins** move in their shafts, open and close the doors, show the people inside (the number when the doors are
  closed) and a load bar at the bottom (red when full). Above every shaft the name of the cabin and its direction.
  Small amber dots next to a shaft are the floors where that cabin has to stop.
- **Out of order** cabins are striped in red and their shaft is darker.
- **Zones**: the cabins of a group have its colour; the floors a cabin passes express are darker in its shaft.
- 🤬 **Stairs**: who loses patience leaves the queue and walks to the door of the stairs.
- 👍 **Served quickly**: who arrives after little waiting steps out of the cabin and cheers.

### Who goes where

The original console log said, for every passenger, the destination and the cabin assigned. The building shows the
same thing:

- Above every waiting person a label like **`C→7`**: assigned to cabin C, going to floor 7, in the colour of the
  cabin (the colour band on the roof of every shaft). On low floors the labels appear when you point at the floor.
- A cabin with the doors closed shows the floors of its riders (**`→7 9 12`**) instead of their number.
- **Point at a cabin**: the people it has to pick up stay lit, the others fade; inside its shaft a tag on every floor
  where it stops (`↓2 ↑3`: two get out, three get in) and a card with who is on board, who it is picking up and the
  order of the next stops.
- **Point at a person**: a card with where they are and where they go, the cabin coming for them (outlined), when the
  dispatcher expects it (green if it arrives in time, amber if late) and how long they have waited; after a change at
  a lobby, it says so.
- **Your own calls**: when the dispatcher assigns one, a dashed line joins your passenger to the chosen cabin for a
  moment, next to the bars of the *Dispatcher* panel.

### Panel

- **Right now**: the run (strategy, idle cabins, scenario and seed, *Hands-on run* if you intervened) and the numbers:
  waiting, riding, delivered (with the share served quickly), average, 95th percentile and longest wait, people who
  took the stairs, energy (hover for travel and standby; below it the energy per trip), changes of cabin. Below, the
  number of people waiting over the last 3 simulated hours. With two buildings the numbers become a left/right table,
  the better side in green.
- **Dispatcher**: the last assignment decision, with a bar for every cabin of the group: the extra passenger time the
  call would cost (Optimized), or the distance in floors (Nearest, Collective). The orange bar won. Decisions on your
  own calls stay on screen longer.
- **What happened**: faults, waves, rushes at the lobby, the first people on the stairs, your passengers, the end of
  the run. Tick *every passenger* to see every call, assignment, boarding and arrival.

## Things you can do

| To | Do |
| --- | --- |
| Call a lift | Click a floor (it turns amber), then the floor to go to. Esc cancels. With two buildings the passenger appears in both. |
| Put a cabin out of order | Click it. It stops at the next floor, everybody gets out and waits for another cabin, the calls waiting for it are reassigned. Click it again to repair it. |
| Send a crowd | *Stir things up*: *Morning rush* (from the lobby up in an office), *Lunch break* (to and from the lobby), *Evening rush* (down to the lobby), *Random crowd*; *People per wave* sets how many. |
| Start again with other settings | *New run…* (below). |
| See the full report | *Report*: the same report the console version prints at the end. It opens by itself when everybody has arrived. |

Calls, waves and faults make the run *hands-on*: it is still saved, but kept apart from the comparable runs.

## New run

*New run…* starts the building again from the first hour of day 1:

- **Cabins and zones**: a number (`8`) or a layout with zones (`1-20:6, 21-39:6`, `... @ 30:4` for a sky lobby), see
  [Configuration](Configuration#zones-of-tall-buildings).
- **Floors**, **People per cabin**, **Trips per day**, **Building** (office, homes, random trips), **Days**, **Start at
  hour**, **A day lasts (minutes)**, **Seed** (0 = a new scenario).
- **Dispatch strategy**, **Idle cabins**, **Patience before the stairs**, **Served quickly: waited at most**.
- *Cabin timing and energy*: seconds per floor, door opening, boarding time, standby power, motor efficiency, express
  floor time.

## Comparing strategies

*Compare strategies…* runs several variants of **the same scenario** one after the other at full speed, about half
a minute per simulated day each:

1. Choose the scenario: floors, people per cabin, trips per day, building, days, start hour, seed. By default it is
   the one on screen, so that you can compare it with what you have just watched.
2. Tick the **strategies** and the **idle cabins** rules, and write the **cabins and zones** to try, one per line
   (`8`, `12`, `1-20:6, 21-39:6`...). Every combination is a variant, at most 24; the dialog says how many and how long.
3. *Run comparison*. A banner shows the variant in progress; *Stop comparison* ends it after the current one. During a
   comparison the commands that would change the runs are disabled.
4. At the end *Results* opens on the scenario.

## Results

*Results* shows the completed runs of a scenario (chosen at the top): only runs with exactly the same passengers are
compared.

- **Charts** of the main metrics, best first, in green.
- **Table** with one row per run: strategy, idle cabins, cabins and zones, trips, waits, journey, served quickly, stairs,
  changes, energy, energy per trip, floors travelled, door cycles. The best value of every column is green.
- *include hands-on runs* also shows the runs where you intervened (in italics; they never win a column). It ticks
  itself when a scenario has only hands-on runs, and when the two buildings you just watched are hands-on; the runs just
  finished are highlighted.
- **Watch** runs that variant again live, at a speed you can follow.
- **Export CSV** downloads the whole archive, every field, for a spreadsheet.
- **Clear all results** empties the archive (it asks first).

## Two buildings side by side

*Two buildings…* runs two variants live, on the same passengers and the same clock: choose the scenario, then for each
side the strategy, the idle cabins rule, the cabins and zones and the people per cabin. The two buildings are drawn
side by side with their description, the panel compares them as they go, the history draws the left building solid and
the right one dashed, the feed and the dispatcher say which side an event comes from. Your calls and waves go to both,
a fault only to the cabin you click. When both are done the two results are saved as a pair and *Results* opens.
*Back to one building* returns to a single building.

## Recipes

**Which strategy is best for my office?** *New run…* with your floors, cabins and trips per day; then *Compare
strategies…* with the three strategies and the idle cabins rules *Stay* and *Peak*. Look at the average and 95% wait,
the stairs and the energy per trip: there is rarely a winner on everything.

**How many cabins do I need?** *Compare strategies…* with one strategy and several lines in *cabins and zones*, e.g.
`4`, `5`, `6`, `8`. Beyond a certain number the waits stop improving while the energy per trip keeps growing.

**Does zoning pay off in a tower?** *Two buildings…* with the same floors: on one side `24`, on the other
`1-20:8, 21-40:8, 41-59:8`. Watch the morning rush, then compare journeys, changes of cabin and energy.

**What happens when a cabin breaks at the worst moment?** Start at 07:45, send a *Morning rush*, click a cabin full of
people. The run becomes hands-on, so compare it with the same scenario without the fault.

## Reading the results

- Every number is in simulated time. **Wait** goes from the call to the first boarding; **journey** from the call to
  the arrival, changes of cabin included.
- *95% wait less* means that 95 passengers out of 100 waited less than that: it tells more than the average about the
  unlucky ones.
- At the highest speeds the operating system timers sometimes delay a cabin: averages stay within a few percent, the
  extremes (longest wait, people on the stairs) can come out worse in a single run. For the extremes repeat the run or
  use a lower speed (360x is enough).
- The energy model and the timings are plausible orders of magnitude, made to compare alternatives, not to size a real
  installation.

## Keyboard

| Key | Action |
| --- | --- |
| Space | Pause / resume |
| S | Advance one simulated minute |
| + / − | Double / halve the speed |
| Esc | Cancel the call being placed |
