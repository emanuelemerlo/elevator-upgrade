# Guida per sviluppatori

*[Read in English](Developer-guide)*

Per chi vuole leggere il codice, modificarlo o aggiungere qualcosa. Codice e commenti sono in inglese.

## Mappa dei sorgenti

| Area | File | Ruolo |
| --- | --- | --- |
| Avvio | `Main.cpp` | Riga di comando, modalità console (pannello simbolico) o modalità web. |
| Corsa | `Simulation` | Una corsa di un edificio: avvio, arresto, ripartenza con nuove impostazioni, chiamate manuali, ondate, guasti, archivio, confronti in serie. |
| Orologio | `SimulationClock` | La linea del tempo simulato: velocità, pausa, passo, interruzione; ogni attesa del modello passa da qui. |
| Edificio | `BuildingContext` | Impostazioni, passeggeri in attesa, statistiche e segnale di arresto di un edificio; l'"edificio corrente" di ogni thread. |
| Configurazione | `Configuration` | Impostazioni, lettura di `config.json`, controlli. |
| Cabine | `Elevator` | Un thread per cabina: porte, movimento, salita e discesa, piani espressi, parcheggio, fuori servizio. |
| Piani | `Floors` | Fermate di una cabina e regola della prossima fermata (LOOK direzionale), condivisa con il dispatcher. |
| Passeggeri | `Call`, `People` | Un passeggero (chiamata) e un elenco di passeggeri sicuro tra thread (in attesa ai piani o dentro una cabina). |
| Dispatcher | `Management`, `AssignmentOptimizer` | Gruppi di cabine, scelta della cabina, coda d'attesa, scale, riconoscimento dei picchi; simulazione dei percorsi (funzioni pure). |
| Zone | `Zoning` | Disposizioni, gruppi e pianificazione dei viaggi attraverso gli atri. |
| Traffico | `TrafficProfile`, `Scenario`, `PeopleCallsGenerator` | Profilo giornaliero, i passeggeri di una corsa decisi in anticipo, il thread che li fa comparire al momento giusto. |
| Risultati | `Statistics`, `Energy.h`, `RunArchive` | Contatori e distribuzione delle attese, modello di energia, archivio delle corse concluse. |
| Eventi | `EventBus`, `Json.h` | Flusso degli eventi della simulazione, piccolo scrittore JSON. |
| Web | `WebServer`, `web/` | Server HTTP integrato e API JSON; l'interfaccia nel browser (HTML, CSS, JavaScript, nessun framework). |
| Console e log | `SymbolicInterface`, `ConsoleView`, `Log*`, `ILog` | Pannello in console e log asincrono. |
| Infrastruttura | `WorkerThread.h`, `Watchdog.h` | Thread lavoratore riutilizzabile, watchdog a tempo. |
| Test | `tests/` | `AssignmentOptimizerTests` (simulazione dei percorsi), `SimulationTests` (orologio, corse intere, guasti, scenari, strategie, edifici affiancati, zone, picchi). |

## I thread

| Thread | Creato da | Fa |
| --- | --- | --- |
| Principale | — | Console: aspetta Invio o la fine della corsa. Web: aspetta Invio. |
| Uno per cabina | `Elevator` | Aspetta una chiamata, si muove piano per piano, apre le porte, fa salire e scendere, parcheggia. |
| Lavoratore del dispatcher | `Management` | Ritenta le chiamate in attesa, manda alle scale chi ha perso la pazienza, riconosce i picchi. |
| Generatore di chiamate | `PeopleCallsGenerator` (un `WorkerThread`) | Dorme fino all'istante del prossimo passeggero dello scenario, poi lo inserisce e lo fa assegnare. |
| Scrittore del log | `LogBase` (un `WorkerThread`) | Consuma la coda del log e scrive a schermo o su file. |
| Pannello in console | `SymbolicInterface` | Ridisegna la vista simbolica ogni mezzo secondo (modalità console). |
| Server web | cpp-httplib | Un gruppo di thread che risponde alle richieste HTTP. |
| Confronto | `Simulation::StartBatch` | Fa girare le varianti una dopo l'altra. |

I dati condivisi sono protetti da piccoli mutex (ogni elenco `People`, le fermate di ogni cabina, le statistiche, il
flusso degli eventi). Le attese usano variabili di condizione con predicato. I comandi che arrivano dal web tengono
`Simulation::Lock()`, così una ripartenza non può distruggere le cabine mentre una richiesta le sta leggendo.

## La vita di un passeggero

1. `Scenario::Build` ha deciso, prima della corsa, quando compare, da dove, per dove e quanto è paziente.
2. Il thread del generatore dorme fino a quell'istante simulato (`SimulationClock::SleepUntil`), lo mette nell'elenco
   di attesa dell'edificio (`People::Insert`, evento `call`) e chiede al dispatcher (`Management::AssignCall`).
3. Il dispatcher pianifica la tratta (`Zoning::Plan`: diretta, oppure fino a un atrio per cambiare cabina), valuta ogni
   cabina del gruppo giusto (`AssignmentOptimizer::EvaluateAssignment`, o la distanza per le strategie più semplici),
   ne sceglie una, prenota il piano di salita (`Elevator::AnswerToCall`) e pubblica la decisione (evento `assign`). Se
   nessuna cabina ha posto, la chiamata va nella coda d'attesa.
4. Il thread della cabina si sveglia, sceglie la prossima fermata (`Floors::FindNextStop`), si muove, apre le porte;
   in `People::EnterAndExit` il passeggero passa dall'elenco di attesa alla cabina (evento `board`).
5. A destinazione scende (evento `arrive`, 👍 se servito in fretta); alla fine di una tratta scende all'atrio e torna
   al dispatcher (evento `transfer`). Se aspetta più della sua pazienza, il lavoratore del dispatcher lo toglie dalla
   coda (evento `stairs`).

## L'orologio simulato

`SimulationClock` proietta il tempo simulato su quello reale con una velocità che può cambiare in qualsiasi momento.
Ogni attesa del modello è `SleepFor(durata simulata)`: una pausa congela tutti i thread, un cambio di velocità sveglia
i thread addormentati perché ricalcolino la loro attesa reale, un'interruzione fa restituire `false` a ogni attesa
all'istante. Le attese consecutive dello stesso thread sono concatenate sulla linea del tempo simulato: se un thread si
sveglia un po' in ritardo, l'attesa successiva parte dall'istante in cui avrebbe dovuto svegliarsi, così a 2000x i
ritardi del sistema operativo non si accumulano.

## Due edifici: il contesto dell'edificio

Il modello originale legge il suo edificio da funzioni globali (`Configuration::Get()`, `Floors::GetPeople()`,
`Statistics::...`). Invece di riscriverlo, il fork fa restituire a quelle funzioni i dati **dell'edificio del thread
che le chiama** (`Context::Current()`, un puntatore per thread). Ogni thread di un edificio entra nel suo contesto
(`Context::Scope`) quando parte; un thread che non è mai entrato in un contesto usa l'edificio predefinito, che è
l'unico della versione in console. Così due `Simulation` possono girare sullo stesso orologio senza vedersi; ognuna ha
il suo segnale di arresto.

## API web

| Richiesta | Significato |
| --- | --- |
| `GET /api/state?since=N` | Orologio, cabine, passeggeri in attesa, statistiche, corsa, e gli eventi dopo il numero `N` (membro `twin` per il secondo edificio). |
| `GET /api/config` | Impostazioni e profilo del traffico giornaliero. |
| `GET /api/report[?building=twin]` | Report finale come righe di testo. |
| `GET /api/runs`, `POST /api/runs/clear` | Archivio delle corse concluse. |
| `POST /api/control` | `action=pause`, `resume`, `step` (`ms=`), `speed` (`value=`). |
| `POST /api/call` | `from=F&to=T`. |
| `POST /api/wave` | `kind=morning`, `lunch`, `evening` o `random`, più `count=N`. |
| `POST /api/elevator` | `id=A&outOfOrder=true` (o `false`), più `building=twin` per il secondo edificio. |
| `POST /api/restart` | Nuova corsa; parametri con i nomi di `config.json`, più `layout`. |
| `POST /api/batch`, `POST /api/batch/cancel` | Confronto: parametri dello scenario più `strategies`, `parkings`, `layouts` (separati da una barra verticale). |
| `POST /api/twin`, `POST /api/twin/stop` | Due edifici: parametri dello scenario più `left…` e `right…` (`DispatchStrategy`, `IdleParking`, `Layout`, `MaxPeople`). |

Esempio, un confronto da uno script:

```sh
curl -X POST localhost:8080/api/batch \
  -d "randomSeed=42&strategies=Optimized,Nearest&parkings=Stay,Peak&layouts=4|6"
curl localhost:8080/api/runs
```

Il browser chiede `/api/state` circa dieci volte al secondo. Tra una risposta e l'altra anima le cabine da solo: il
server manda quando sono iniziati ogni spostamento tra due piani e ogni movimento delle porte e quanto durano, e la
pagina fa avanzare l'orologio alla velocità corrente.

## Estendere il programma

**Una nuova strategia di assegnazione**
1. `Configuration.h`: aggiungila a `Dispatch::Strategy`; `Configuration.cpp`: leggila (`ParseStrategy`) e dalle un
   nome (`StrategyName`).
2. `Management::AssignCallInternal`: assegna ai candidati un `score` nello `switch (strategy)` (vince il più basso).
   Ogni candidato ha la valutazione del suo percorso (tempo di arrivo, se è di passaggio, se c'è posto) e lo stato
   della sua cabina.
3. `web/index.html`: aggiungila ai menu delle strategie e alle caselle del confronto.
4. Un test in `tests/SimulationTests.cpp` (vedi `Strategies()`).

**Una nuova regola per le cabine ferme**: `Dispatch::Parking`, poi lo `switch` nel costruttore di `Management` (piani
base) oppure, per una regola che cambia nel tempo, un aggiornamento periodico come `UpdatePeaks()`. Una cabina con un
piano base ci va da sola quando è ferma (`Elevator::ParkIfIdle`).

**Una nuova misura**: contala in `Statistics`, aggiungila al report (`Management::BuildReport`), allo stato
(`StatisticsJson` in `WebServer.cpp`), all'archivio (`Simulation::Archive`) e alle colonne dei risultati (`COLUMNS` in
`app.js`; `lower: true` o `higher: true` decide quale valore è il migliore).

**Un nuovo evento**: `EventBus::Publish("nome", Json::Writer::Object().Add(...))` dove accade; descrivilo in
`describe()` in `app.js` per mostrarlo nel registro.

## Test e controlli

```sh
make test
```

Per cercare accessi concorrenti non protetti, compila i test della simulazione con ThreadSanitizer:

```sh
g++ -std=c++17 -O1 -g -fsanitize=thread -pthread -Isrc \
  $(ls src/*.cpp | grep -v Main.cpp | grep -v WebServer.cpp) tests/SimulationTests.cpp -o sim_tsan
./sim_tsan
```

Le corse intere usano velocità alte; un test che dipende dai tempi aspetta con margini larghi. `traceLevel: "Debug"`
scrive su cosa si basa ogni decisione del dispatcher (`Dispatch state of ...`), per confrontare la previsione con
quello che la cabina fa davvero.
