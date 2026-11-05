# Getting started

*[Leggi in italiano](Installazione-e-avvio)*

## What you need

- A C++17 compiler: Visual Studio 2022 or 2026 on Windows, GCC or Clang on Linux (MinGW on Windows also works through
  the `makefile`).
- A recent browser for the web interface (Chrome, Edge, Firefox, Safari).
- Nothing else: the only external library, [cpp-httplib](https://github.com/yhirose/cpp-httplib), is a single header
  already included in `third_party/`.

## Get the code

```sh
git clone https://github.com/emanuelemerlo/elevator-upgrade
cd elevator-upgrade
```

## Build

### Windows, Visual Studio

Open `Elevator.sln`. The project uses the toolset of the Visual Studio that opens it (v143 with Visual Studio 2022,
v145 with Visual Studio 2026), so it does not need to be retargeted: if Visual Studio proposes it, you can decline.
Choose *Debug* or *Release*, *x64*, and build. The project treats warnings as errors (level 4).

To start it with the web interface from Visual Studio, set the command arguments to `--web` (*Project → Properties →
Debugging → Command Arguments*) and make sure the working directory is the repository folder, where `config.json` and
the `web` folder are.

### Windows, VS Code

The *Build Elevator* task compiles every source with g++ (MinGW) into `Elevator.exe`; *Run Elevator (web)* builds and
starts it with `--web`.

### Linux (and MinGW)

```sh
make          # builds Elevator.exe
make test     # builds and runs the tests
make clean
```

## Run

Start the program **from the repository folder**: it reads `config.json` from the current folder and serves the web
interface from `./web`.

### In the browser

```sh
./Elevator.exe --web
```

The console prints the address to open, by default `http://127.0.0.1:8080/`. Press Enter in the console to quit.

| Option | Meaning |
| --- | --- |
| `--web` | Starts the simulation behind the web interface. |
| `--port N` | Port of the web interface (default `8080`). |
| `--host ADDRESS` | Address to listen on (default `127.0.0.1`, this computer only; `0.0.0.0` lets other computers of the network connect). |
| `--web-root DIR` | Folder with the files of the web interface (default `./web`). |
| `--config FILE` | Configuration file (default `config.json`). |

In web mode the log goes to its file only, from the *Warning* level up.

### In the console

```sh
./Elevator.exe
```

The original symbolic dashboard: one row per floor, one column per cabin (`[A^03]` = cabin A going up with 3 people),
the people waiting on each floor and the rolling log. By default the run stops by itself when the day is over and every
passenger has arrived (`autoStop`); press Enter to stop earlier. At the end the screen shows the final report.

With `"defaultLogType": "File"` the dashboard is not drawn and the report is written to the log file: useful for
unattended runs.

## First steps in the browser

1. Open the address, the building starts at 06:00 of day 1 at 60x (a day in 24 minutes).
2. Click a floor, then another one: you have called a lift; your passenger has a dark ring.
3. Press *Morning rush* to send a crowd from the lobby, watch the queues and the dispatcher panel.
4. Click a cabin to put it out of order, click it again to repair it.
5. Open *Compare strategies…*, keep the defaults and run it: about half a minute later the table of results opens.

The whole interface is described in the [User manual](User-manual).

## Results files

- `elevator.log`: the log (file mode or web mode).
- `runs.jsonl`: the results of every completed run, one JSON object per line; the web interface reads it again at the
  next start and can export it as CSV. Delete it to start from an empty archive.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| `Invalid configuration: ...` at start | The message names the setting and the accepted values; fix `config.json`. |
| `Cannot listen on 127.0.0.1:8080` | Another program uses the port: start with `--port 8081`. |
| The browser says *Not Found* and the console says the web interface folder was not found | The program was started from another folder: start it from the repository folder or use `--web-root`. |
| *The simulation is not answering* in the page | The program has stopped; the page reconnects by itself when it starts again. |
| `simulationDayDurationMs and timeScale have been replaced...` | A configuration of the original project: use `dayDurationSeconds` instead (see [Configuration](Configuration)). |
