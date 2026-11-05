# Installazione e avvio

*[Read in English](Getting-started)*

## Cosa serve

- Un compilatore C++17: Visual Studio 2022 o 2026 su Windows, GCC o Clang su Linux (su Windows funziona anche MinGW
  tramite il `makefile`).
- Un browser recente per l'interfaccia web (Chrome, Edge, Firefox, Safari).
- Nient'altro: l'unica libreria esterna, [cpp-httplib](https://github.com/yhirose/cpp-httplib), è un solo file di
  intestazione già incluso in `third_party/`.

## Scaricare una versione pronta

La [pagina delle release](https://github.com/emanuelemerlo/elevator-upgrade/releases) ha pacchetti pronti per Windows
(x64), Linux (x64, glibc 2.35 o successiva) e macOS (Apple silicon e Intel). Estrai l'archivio e avvia
`start-web.cmd` (Windows) o `./start-web.sh` (Linux, macOS): parte la simulazione e si apre il browser. Il programma
non è firmato: su Windows SmartScreen può chiedere conferma (*Ulteriori informazioni → Esegui comunque*), su macOS la
prima volta fai clic destro e scegli *Apri*. Per compilarlo da te, prosegui.

## Scaricare il codice

```sh
git clone https://github.com/emanuelemerlo/elevator-upgrade
cd elevator-upgrade
```

## Compilare

### Windows, Visual Studio

Apri `Elevator.sln`. Il progetto usa il toolset del Visual Studio che lo apre (v143 con Visual Studio 2022, v145 con
Visual Studio 2026), quindi non va convertito: se Visual Studio lo propone, puoi rifiutare. Scegli *Debug* o
*Release*, *x64*, e compila. Il progetto tratta gli avvisi come errori (livello 4).

Per avviarlo con l'interfaccia web da Visual Studio, imposta gli argomenti a `--web` (*Progetto → Proprietà → Proprietà di
configurazione → Debug → Argomenti comando*) e controlla che la cartella di lavoro sia quella del repository, dove si trovano `config.json`
e la cartella `web`.

### Windows, VS Code

Il task *Build Elevator* compila tutti i sorgenti con g++ (MinGW) in `Elevator.exe`; *Run Elevator (web)* compila e lo
avvia con `--web`.

### Linux (e MinGW)

```sh
make          # compila Elevator.exe
make test     # compila ed esegue i test
make clean
```

## Avviare

Avvia il programma **dalla cartella del repository**: legge `config.json` dalla cartella corrente e serve
l'interfaccia web da `./web`.

### Nel browser

```sh
./Elevator.exe --web
```

La console scrive l'indirizzo da aprire, di solito `http://127.0.0.1:8080/`. Premi Invio nella console per uscire.

| Opzione | Significato |
| --- | --- |
| `--web` | Avvia la simulazione con l'interfaccia web. |
| `--port N` | Porta dell'interfaccia web (predefinita `8080`). |
| `--host INDIRIZZO` | Indirizzo su cui ascoltare (predefinito `127.0.0.1`, solo questo computer; `0.0.0.0` permette il collegamento da altri computer della rete). |
| `--web-root CARTELLA` | Cartella con i file dell'interfaccia web (predefinita `./web`). |
| `--config FILE` | File di configurazione (predefinito `config.json`). |
| `--version` | Stampa la versione ed esce. |

In modalità web il log va solo nel suo file, dal livello *Warning* in su.

### In console

```sh
./Elevator.exe
```

Il pannello simbolico originale: una riga per piano, una colonna per cabina (`[A^03]` = cabina A in salita con 3
persone), le persone in attesa a ogni piano e il log che scorre. Di norma la corsa si ferma da sola quando la giornata
è finita e tutti sono arrivati (`autoStop`); premi Invio per fermarla prima. Alla fine lo schermo mostra il report.

Con `"defaultLogType": "File"` il pannello non viene disegnato e il report finisce nel file di log: utile per le
corse senza nessuno davanti.

## Primi passi nel browser

1. Apri l'indirizzo: l'edificio parte alle 06:00 del giorno 1 a velocità 60x (una giornata in 24 minuti).
2. Clicca un piano, poi un altro: hai chiamato un ascensore; il tuo passeggero ha un cerchio scuro.
3. Premi *Morning rush* per mandare una folla dall'ingresso, guarda le code e il pannello del dispatcher.
4. Clicca una cabina per metterla fuori servizio, cliccala di nuovo per ripararla.
5. Apri *Compare strategies…*, lascia le impostazioni proposte e avvialo: circa mezzo minuto dopo si apre la tabella
   dei risultati.

Tutta l'interfaccia è descritta nel [Manuale utente](Manuale-utente).

## File dei risultati

- `elevator.log`: il log (modalità file o modalità web).
- `runs.jsonl`: i risultati di ogni corsa conclusa, un oggetto JSON per riga; l'interfaccia web lo rilegge
  all'avvio successivo e lo esporta in CSV. Cancellalo per ripartire da un archivio vuoto.

## Problemi frequenti

| Problema | Cosa fare |
| --- | --- |
| `Invalid configuration: ...` all'avvio | Il messaggio indica l'impostazione e i valori accettati; correggi `config.json`. |
| `Cannot listen on 127.0.0.1:8080` | Un altro programma usa la porta: avvia con `--port 8081`. |
| Il browser dice *Not Found* e la console dice che la cartella dell'interfaccia non è stata trovata | Il programma è stato avviato da un'altra cartella: avvialo dalla cartella del repository o usa `--web-root`. |
| Nella pagina compare *The simulation is not answering* | Il programma si è fermato; la pagina si ricollega da sola quando riparte. |
| `simulationDayDurationMs and timeScale have been replaced...` | Una configurazione del progetto originale: usa `dayDurationSeconds` (vedi [Configurazione](Configurazione)). |
