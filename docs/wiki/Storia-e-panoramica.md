# Storia e panoramica

*[Read in English](History-and-overview)*

## Da dove viene

Negli anni '80 la rivista italiana *Commodore Computer Club* lanciò una gara: scrivere un programma che simulasse un
ascensore sul Commodore 64. L'autore allora era un bambino e non poteva partecipare, ma l'idea di provarci, quasi come
un gioco, gli è rimasta. Decenni dopo ci ha riprovato, questa volta in C++ con la libreria standard.

Il modello scelto non è quello classico con i pulsanti dentro la cabina. Il passeggero sceglie il **piano di
destinazione già al momento della chiamata**, sul pianerottolo; una "gestione" centrale riceve la chiamata e la affida
alla cabina più adatta. È il *destination dispatch*, il sistema installato in molti uffici moderni (e nell'edificio in
cui lavora l'autore), e lascia molto più spazio all'ottimizzazione del sistema tradizionale, perché il dispatcher sa
dove va ognuno prima di scegliere la cabina.

Fin dall'inizio il progetto è stato pensato come **una palestra**: un programma piccolo ma vero per esercitarsi con
thread, sincronizzazione, collezioni e modellazione.

## Le tappe

| Quando | Cosa |
| --- | --- |
| Ottobre-novembre 2019 | Prima versione in C++: un thread per cabina, generatore di chiamate, gestione, log su un thread separato, compilazione Windows e Linux. Diversi rifacimenti dei thread e dello spegnimento. |
| Maggio-giugno 2026 | Pannello simbolico in console, log su file, giorni simulati con un profilo di traffico giornaliero, report finale con statistiche e suggerimenti di dimensionamento. |
| Ottobre 2026 | Nuovo dispatcher: il percorso di ogni cabina viene simulato con e senza la nuova chiamata, e la chiamata va dove costa meno tempo ai passeggeri. Miglioramenti misurati, primi test. |
| Novembre 2026 | **elevator-upgrade**: il fork descritto in questa wiki. Orologio simulato, server web integrato e interfaccia nel browser, scenari riproducibili, confronti tra strategie, modello di consumo energetico, due edifici affiancati, zone e atri intermedi per i grattacieli. |

Il fork mantiene la natura dell'originale: è sempre una simulazione C++ con lo stesso nucleo (cabine, dispatcher,
statistiche) e la versione in console funziona come prima. L'interfaccia web è un modo in più per guardarla e per
giocarci, non una riscrittura.

## Non un gioco, una simulazione con cui giocare

L'interfaccia vuole essere piacevole e un po' giocosa: il cielo segue l'ora, la gente fa la coda ai piani e cambia
colore mentre aspetta, chi perde la pazienza se ne va per le scale con un 🤬, chi è servito in fretta esulta con un 👍.
Lo scopo però resta quello di un simulatore: **confrontare strategie di assegnazione e configurazioni dell'edificio su
esattamente gli stessi passeggeri**, misurando attese, viaggi, energia e persone che hanno rinunciato.

## Perché è interessante

Per chi vuole imparare o vedere un esempio completo, il progetto tocca molti temi classici della programmazione
concorrente e di sistema, in un ambito che tutti capiscono.

**Thread e sincronizzazione**
- **Un thread per cabina**, più il generatore di chiamate, il lavoratore del dispatcher, chi scrive il log e il server
  web: un vero programma multi-thread in cui ogni thread fa un solo lavoro semplice.
- **Produttore/consumatore**, in più punti. Il log: ogni componente *produce* messaggi in una coda condivisa, un solo
  thread li *consuma* e li scrive a schermo o su file (`LogBase`). Il dispatcher: le chiamate che non trovano posto in
  nessuna cabina vanno in una coda d'attesa consumata da un thread che le ritenta (`Management`). Il flusso degli
  eventi: la simulazione produce eventi in un buffer limitato, l'interfaccia web li consuma al suo ritmo tramite un
  numero progressivo (`EventBus`).
- **Variabili di condizione con predicato**, per evitare i classici risvegli persi e quelli spuri; i commenti nel
  codice spiegano ogni caso.
- **Un modello riutilizzabile di thread lavoratore** (`WorkerThread<T>`, il pattern *template method*: la classe
  gestisce avvio, attesa e arresto, la classe derivata scrive solo il suo ciclo) e un **watchdog** che segnala uno
  spegnimento troppo lungo.
- **Contesto per thread**: il modello originale tiene l'edificio in dati globali; il fork fa appartenere quei dati
  all'edificio del thread che li usa, così due edifici possono girare affiancati senza riscrivere il modello.
- I test girano anche sotto **ThreadSanitizer**, che ha trovato e aiutato a correggere accessi concorrenti reali.

**Simulazione**
- Un **orologio simulato**: una giornata dura 24 ore simulate, ogni durata è in tempo simulato, e l'orologio va dal
  tempo reale fino a una giornata in 43 secondi, si mette in pausa o avanza di un passo. Ad alta velocità le attese di
  un thread sono concatenate sulla linea del tempo simulato, così i ritardi dei timer del sistema operativo non si
  accumulano.
- Un **dispatcher basato sulla simulazione dei percorsi**, con capienza, invecchiamento contro l'attesa infinita,
  bilanciamento dell'usura, e strategie più semplici (cabina più vicina, raccolta direzionale) per confronto.
- **Scenari riproducibili**: i passeggeri di una corsa sono generati in anticipo da un seme, così due corse che
  differiscono solo per la strategia affrontano le stesse persone negli stessi istanti.
- Modelli di **traffico** (uffici, abitazioni), **energia** (contrappeso, motore, consumo a vuoto), **pazienza** e
  **zone** dei grattacieli con tratte espresse, cambi di cabina e atri intermedi.

**Interfaccia web senza un backend separato**
- Un **server HTTP integrato** nello stesso processo C++ (cpp-httplib, un solo file di intestazione) espone una
  piccola API JSON; il browser chiede lo stato circa dieci volte al secondo e **anima le cabine da solo** tra una
  risposta e l'altra, partendo dall'istante di inizio e dalla durata di ogni movimento.
- La stessa API si usa anche da script: i confronti tra strategie si possono automatizzare senza interfaccia.

## Cosa non è

- Non dimensiona un impianto reale: tempi, traffico ed energia sono ordini di grandezza plausibili, sufficienti per
  confrontare alternative, non per progettare un ascensore.
- Alle velocità più alte i risultati sono affidabili in media, meno negli estremi (attesa più lunga, persone sulle
  scale): vedi le note nel [Manuale utente](Manuale-utente#leggere-i-risultati).
