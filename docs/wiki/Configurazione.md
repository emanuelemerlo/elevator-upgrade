# Configurazione

*[Read in English](Configuration)*

La simulazione legge `config.json` all'avvio (o il file indicato con `--config`). È un oggetto JSON piatto: i gruppi
qui sotto (*edificio*, *traffico*...) servono solo a ordinare questa pagina. Le chiavi mancanti tengono il valore
predefinito; un valore non valido ferma il programma all'avvio con un messaggio che indica la chiave e i valori
accettati. Nell'interfaccia web quasi tutte le impostazioni si possono cambiare anche per una nuova corsa (*New
run…*), senza toccare il file.

Tutte le durate sono in tempo **simulato**, salvo dove il nome dice altrimenti: una giornata dura 24 ore simulate a
qualunque velocità.

## Edificio e cabine

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `numberOfFloors` | `10` | Piani, da `2` a `60`. Il piano `0` è l'ingresso, quindi `10` significa piani 0-9. |
| `numberOfElevators` | `4` | Cabine di un edificio senza zone, da `1` a `26` (chiamate A-Z). Con `zones` le cabine sono quelle delle zone. |
| `maxPeople` | `12` | Persone per cabina. Il dispatcher non assegna mai una chiamata che sovraccaricherebbe una cabina. |
| `timeToReachTheNextFloorMs` | `2000` | Tempo per spostarsi di un piano. |
| `doorTransitionTimeMs` | `1500` | Tempo per aprire, e per chiudere, le porte. |
| `enterAndExitTimeMs` | `3000` | Tempo in cui le porte restano aperte per far salire e scendere. |

## Zone dei grattacieli

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `zones` | `""` | Vuoto: un solo gruppo serve tutti i piani. Altrimenti le zone e le loro cabine, per esempio `"1-20:6, 21-39:6"`. |
| `skyLobbies` | `""` | Atri intermedi e cabine della loro navetta dal piano terra, per esempio `"30:4"`. |
| `expressFloorFactor` | `0.35` | Tempo per attraversare un piano che la cabina non serve, come frazione di `timeToReachTheNextFloorMs` (0-1). |

Nell'interfaccia web cabine e zone sono un solo campo, *Cabins and zones*:

| Disposizione | Significato |
| --- | --- |
| `8` | Un gruppo di 8 cabine che serve tutti i piani. |
| `1-20:6, 21-39:6` | Due zone servite dall'ingresso: 6 cabine per i piani 1-20, 6 per i piani 21-39. |
| `1-29:8, 31-45:6, 46-59:6 @ 30:4` | Un atrio intermedio al piano 30 raggiunto dal piano terra con 4 cabine navetta; le zone sopra partono da lì. |

Le zone devono coprire ogni piano sopra l'ingresso esattamente una volta (un atrio intermedio non sta in nessuna zona);
al massimo 18 zone e 64 cabine in tutto. Le cabine prendono il nome del loro gruppo: `A1`, `A2`... `B1`... `S1` per le
navette.

## Traffico

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `type` | `"Random"` | `Random`: lo scenario della giornata (sotto). `Fixed`: ripete un breve elenco fisso di chiamate, per il debug. |
| `trafficPattern` | `"Office"` | `Office`: arrivi dall'ingresso al mattino, pranzo in entrambe le direzioni, uscite la sera. `Residential`: il contrario. `Uniform`: viaggi casuali tutto il giorno (il modello originale). |
| `averageCallsPerDay` | `2000` | Viaggi in una giornata simulata. L'intensità segue un profilo giornaliero con picchi al mattino, a pranzo e la sera. |
| `numberOfSimulationDays` | `1` | Giorni della corsa. |
| `dayStartHour` | `6.0` | Ora del giorno 1 in cui parte la corsa (`7.5` = 07:30). |
| `numberOfCalls` | `4294967295` | Ferma lo scenario dopo questo numero di chiamate (il massimo significa "nessun limite"). |
| `maxConcurrentCalls` | `200` | Limite della popolazione: con questo numero di chiamate in corso, le nuove aspettano che qualcuno arrivi. |
| `minDelayBetweenCallsMs` | `500` | Intervallo minimo tra due chiamate. |
| `maxDelayBetweenCallsMs` | `900000` | Intervallo massimo tra due chiamate (15 minuti: di notte l'edificio è tranquillo ma non deserto). |
| `randomSeed` | `0` | Seme dello scenario. `0` ne sceglie uno nuovo a ogni corsa; il seme usato viene mostrato e archiviato, così la corsa si può ripetere. |

## Passeggeri

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `stairsPatienceSeconds` | `120` | Pazienza media: ogni passeggero riceve tra la metà e una volta e mezza questo valore; chi aspetta di più prende le scale. `0` = nessuno rinuncia. I passeggeri creati da te non rinunciano mai. |
| `happyWaitSeconds` | `20` | Chi in tutto ha aspettato al massimo questo tempo arriva "servito in fretta" (👍). È solo una valutazione, non cambia lo scenario. |

## Assegnazione

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `dispatchStrategy` | `"Optimized"` | `Optimized`: simulazione dei percorsi, minimo tempo aggiunto ai passeggeri. `Nearest`: la cabina più vicina. `Collective`: la più vicina tra quelle ferme e quelle già dirette verso la chiamata. |
| `idleParking` | `"Stay"` | Dove aspetta una cabina ferma. `Stay`: dove si è fermata. `Lobby`: all'ingresso del suo gruppo. `Spread`: a un piano base, distribuiti in modo uniforme. `Peak`: all'ingresso durante un picco riconosciuto dal traffico, dove si è fermata negli altri momenti. |

## Energia

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `standbyPowerW` | `200` | Consumo a vuoto di ogni cabina, in watt, per tutta la corsa. |
| `motorEfficiency` | `0.75` | Rendimento di motore e riduttore (0-1). |

Il modello: il contrappeso bilancia la cabina vuota più metà del carico nominale (75 kg a persona); spostarsi di un
piano (3 m) costa il lavoro per sollevare il lato più pesante quando sale, più l'attrito; 1 Wh per ogni avvio del
motore, 0,15 Wh per ogni ciclo delle porte; in discesa non si recupera energia.

## Tempo e corsa

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `dayDurationSeconds` | `1440` | Secondi reali per 24 ore simulate: la velocità iniziale (`1440` = una giornata in 24 minuti = 60x; `86400` = tempo reale; minimo `43.2` = 2000x). Si può cambiare mentre la corsa procede. |
| `autoStop` | `true` | Console: ferma e stampa il report quando la corsa è finita. |

## Log e file

| Chiave | Valore fornito | Significato |
| --- | --- | --- |
| `traceLevel` | `"Info"` | `Debug`, `Verbose`, `Info`, `Warning`, `Error`. `Debug` mostra anche su cosa si basa ogni decisione del dispatcher. |
| `defaultLogType` | `"Screen"` | Console: `Screen`, `File` (niente pannello, report nel file), `ScreenAndFile` (anche `Both`). La modalità web scrive sempre solo nel file. |
| `filePath` | `"elevator.log"` | File di log. |
| `runsFilePath` | `"runs.jsonl"` | Archivio delle corse concluse. |

## Chiavi del progetto originale

`simulationDayDurationMs` e `timeScale` sono state sostituite da `dayDurationSeconds`: una configurazione che le usa
ancora viene rifiutata con una spiegazione, invece di essere letta in modo sbagliato senza avvisare.

## Esempio: un ufficio di 40 piani con due zone, cabine ferme all'ingresso nelle ore di punta

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
