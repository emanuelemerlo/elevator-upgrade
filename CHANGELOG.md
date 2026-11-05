# Changelog

The release workflow publishes the section of the released version as the release notes, so every section is
written for the people who download the program. Versions follow [Semantic Versioning](https://semver.org/).

Il workflow di rilascio pubblica la sezione della versione rilasciata come note della release: ogni sezione è scritta
per chi scarica il programma.

## [Unreleased]

### What is new

- **Who goes where**: every waiting person shows the assigned cabin and the destination (`C→7`), closed cabins show
  the floors of their riders, pointing at a cabin shows its plan and pointing at a person shows the assigned cabin
  and the predicted pickup; your own calls get a line to the chosen cabin.

## [1.0.0] - 2026-11-05

First release of **elevator-upgrade**, the evolution of [elevator](https://github.com/emanuelemerlo/elevator): the
same C++ simulation, now with a browser interface to watch it, intervene and compare strategies on the same
passengers.

### Getting started

Unpack the archive of your platform and start `start-web` (Windows: `start-web.cmd`; Linux and macOS:
`./start-web.sh`): the simulation starts and the browser opens `http://127.0.0.1:8080/`. `Elevator --web` does the
same without opening the browser, `Elevator` alone starts the original console version. Settings are in
`config.json`. The [wiki](https://github.com/emanuelemerlo/elevator-upgrade/wiki) has the user manual, in Italian
and English.

- **Windows**: the program is not signed, so SmartScreen may warn the first time: *More info → Run anyway*.
- **macOS**: the program is not notarized; the first time, right-click it and choose *Open*, or run
  `xattr -d com.apple.quarantine Elevator` in the folder. The binary runs on Apple silicon and Intel Macs.
- **Linux**: built for x86-64, runs on distributions from 2022 on (glibc 2.35 or later).

### What is new

- **Simulated day**: a day lasts 24 simulated hours with office or residential traffic; the speed goes from real time
  to a day in 43 seconds, with pause and one-minute steps.
- **Browser interface**: animated building whose sky follows the hour, queues on the floors, your own calls, traffic
  waves, cabins out of order, live statistics, the reasoning of the dispatcher.
- **Comparisons**: reproducible scenarios, three dispatch strategies, four rules for idle cabins (including the
  lobby at rush hours), comparisons in series at full speed, archive of the results with charts and CSV export, two
  buildings side by side on the same passengers.
- **Measures**: waits, journeys, energy (motor, doors, standby), people who take the stairs 🤬 and people served
  quickly 👍.
- **Tall buildings**: zones, express runs, changes of cabin, sky lobbies.
- **Quality**: tests of the dispatcher and of whole runs, also under ThreadSanitizer; builds on Windows, Linux and
  macOS.

### Novità (in italiano)

Prima versione di **elevator-upgrade**, l'evoluzione di elevator: la stessa simulazione in C++, ora con
un'interfaccia nel browser per osservarla, intervenire e confrontare strategie sugli stessi passeggeri. Avvia
`start-web` (Windows `start-web.cmd`, Linux e macOS `./start-web.sh`): parte la simulazione e si apre il browser.
Giornata simulata con traffico d'ufficio o residenziale, edificio animato, chiamate e guasti a mano, confronti tra
strategie su scenari riproducibili, due edifici affiancati, energia, scale 🤬 e 👍, zone e atri intermedi dei
grattacieli. Il manuale, in italiano e in inglese, è nella wiki.
