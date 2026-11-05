# Manuale utente

*[Read in English](User-manual)*

Questa pagina descrive l'interfaccia web (`Elevator.exe --web`, vedi [Installazione e avvio](Installazione-e-avvio)).
Le scritte dell'interfaccia sono in inglese e sono citate come appaiono.

## Lo schermo

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│ 07:42  Day 1 of 1   ⏸  +1 min   Speed ━━━●━━  ×60, a day in 24 min   ▁▃█▃▅▂▃▆▂ │  barra in alto
├──────────────────────────────────────────────────────────┬──────────────────────────┤
│                                                          │ Right now                │
│        l'edificio in sezione                             │ Dispatcher               │
│        (uno o due edifici)                               │ Stir things up           │
│                                                          │ Runs and comparisons     │
│                                                          │ What happened            │
├──────────────────────────────────────────────────────────┴──────────────────────────┤
│ suggerimento: cosa fa un clic                                                       │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

### Barra in alto

- **Orologio**: l'ora simulata, il giorno della corsa, *paused* (in pausa) o *Run complete* (corsa finita).
- **⏸ / ▶** (barra spaziatrice): pausa e ripresa. Si ferma tutto l'edificio, anche le cabine a metà di un piano.
- **+1 min** (S): avanza di un minuto simulato e si rimette in pausa, per seguire da vicino una situazione.
- **Speed**: cursore logaritmico dal tempo reale a 2000x (una giornata in 43 secondi); scorciatoie *Real time*,
  *×60*, *×360*, *×1440*; i tasti + e − la raddoppiano o la dimezzano. Cambiare velocità non cambia mai i risultati:
  ogni durata è in tempo simulato.
- **Traffic today**: il profilo del traffico della giornata, con l'ora attuale.

### L'edificio

- Il **cielo** segue l'ora: alba, giorno, tramonto, notte con le stelle; la sera i pianerottoli si illuminano.
- A sinistra di ogni piano il suo numero e la **porta verde delle scale**.
- Le **persone in attesa** fanno la coda sul pianerottolo verso gli ascensori e cambiano colore con l'attesa: verde,
  arancio dopo 20 s, rosso dopo 45 s. Un cerchio scuro indica i tuoi passeggeri, uno rosso chi è dovuto scendere da
  una cabina guasta, uno blu chi aspetta la cabina successiva dopo un cambio a un atrio. Le frecce accanto alla coda
  sono i pulsanti di chiamata, accesi quando qualcuno aspetta per salire o scendere.
- Le **cabine** si muovono nei loro vani, aprono e chiudono le porte, mostrano chi c'è dentro (il numero quando le
  porte sono chiuse) e in basso una barra del carico (rossa quando sono piene). Sopra ogni vano il nome della cabina
  e la sua direzione. I puntini arancioni accanto a un vano sono i piani dove quella cabina deve fermarsi.
- Le cabine **fuori servizio** sono a strisce rosse e il loro vano è più scuro.
- **Zone**: le cabine di un gruppo hanno il suo colore; i piani che una cabina attraversa senza fermarsi sono più scuri
  nel suo vano.
- 🤬 **Scale**: chi perde la pazienza esce dalla coda e va verso la porta delle scale.
- 👍 **Servito in fretta**: chi arriva dopo poca attesa scende dalla cabina ed esulta.

### Pannello

- **Right now**: la corsa (strategia, cabine ferme, scenario e seme, *Hands-on run* se sei intervenuto) e i numeri:
  in attesa, in viaggio, arrivati (con la quota servita in fretta), attesa media, 95° percentile e più lunga, persone
  sulle scale, energia (passa sopra col mouse per vedere viaggio e consumo a vuoto; sotto c'è l'energia per viaggio),
  cambi di cabina. Più sotto, le persone in attesa nelle ultime 3 ore simulate. Con due edifici i numeri diventano una
  tabella sinistra/destra, con il lato migliore in verde.
- **Dispatcher**: l'ultima decisione di assegnazione, con una barra per ogni cabina del gruppo: il tempo che la
  chiamata aggiungerebbe ai passeggeri (Optimized), o la distanza in piani (Nearest, Collective). Ha vinto la barra
  arancione. Le decisioni sulle tue chiamate restano visibili più a lungo.
- **What happened**: guasti, ondate, picchi all'ingresso, le prime persone sulle scale, i tuoi passeggeri, la fine
  della corsa. Spunta *every passenger* per vedere ogni chiamata, assegnazione, salita e arrivo.

## Cosa puoi fare

| Per | Fai |
| --- | --- |
| Chiamare un ascensore | Clicca un piano (diventa arancione), poi il piano dove andare. Esc annulla. Con due edifici il passeggero compare in entrambi. |
| Mettere una cabina fuori servizio | Cliccala. Si ferma al piano successivo, tutti scendono e aspettano un'altra cabina, le chiamate che la aspettavano vengono riassegnate. Cliccala di nuovo per ripararla. |
| Mandare una folla | *Stir things up*: *Morning rush* (dall'ingresso verso l'alto in un ufficio), *Lunch break* (da e verso l'ingresso), *Evening rush* (verso l'ingresso), *Random crowd* (casuale); *People per wave* dice quante persone. |
| Ripartire con altre impostazioni | *New run…* (sotto). |
| Vedere il report completo | *Report*: lo stesso report che la versione in console stampa alla fine. Si apre da solo quando tutti sono arrivati. |

Chiamate, ondate e guasti rendono la corsa *hands-on* ("manipolata"): viene salvata comunque, ma tenuta separata dalle
corse confrontabili.

## Nuova corsa

*New run…* fa ripartire l'edificio dalla prima ora del giorno 1:

- **Cabins and zones**: un numero (`8`) o una disposizione a zone (`1-20:6, 21-39:6`, `... @ 30:4` per un atrio
  intermedio), vedi [Configurazione](Configurazione#zone-dei-grattacieli).
- **Floors** (piani), **People per cabin** (persone per cabina), **Trips per day** (viaggi al giorno), **Building**
  (ufficio, abitazioni, viaggi casuali), **Days** (giorni), **Start at hour** (ora di partenza), **A day lasts
  (minutes)** (durata reale di una giornata), **Seed** (seme, 0 = nuovo scenario).
- **Dispatch strategy** (strategia), **Idle cabins** (cabine ferme), **Patience before the stairs** (pazienza),
  **Served quickly: waited at most** (soglia del 👍).
- *Cabin timing and energy*: secondi per piano, apertura porte, tempo di salita e discesa, consumo a vuoto,
  rendimento del motore, tempo dei piani espressi.

## Confrontare le strategie

*Compare strategies…* fa girare più varianti **dello stesso scenario** una dopo l'altra alla massima velocità, circa
mezzo minuto per giornata simulata ciascuna:

1. Scegli lo scenario: piani, persone per cabina, viaggi al giorno, tipo di edificio, giorni, ora di partenza, seme.
   Di norma è quello sullo schermo, così puoi confrontarlo con quello che hai appena visto.
2. Spunta le **strategie** e le regole per le **cabine ferme**, e scrivi le **disposizioni di cabine e zone** da
   provare, una per riga (`8`, `12`, `1-20:6, 21-39:6`...). Ogni combinazione è una variante, al massimo 24; la
   finestra dice quante sono e quanto dureranno.
3. *Run comparison*. Un banner mostra la variante in corso; *Stop comparison* ferma il confronto dopo quella attuale.
   Durante un confronto i comandi che cambierebbero le corse sono disattivati.
4. Alla fine si apre *Results* sullo scenario.

## Risultati

*Results* mostra le corse concluse di uno scenario (si sceglie in alto): si confrontano solo corse con esattamente gli
stessi passeggeri.

- **Grafici** delle misure principali, dal migliore, in verde.
- **Tabella** con una riga per corsa: strategia, cabine ferme, cabine e zone, viaggi, attese, viaggio, serviti in
  fretta, scale, cambi, energia, energia per viaggio, piani percorsi, cicli delle porte. Il valore migliore di ogni
  colonna è verde.
- *include hands-on runs* mostra anche le corse in cui sei intervenuto (in corsivo).
- **Watch** rifà quella variante dal vivo, a una velocità che si riesce a seguire.
- **Export CSV** scarica tutto l'archivio, con ogni campo, per un foglio di calcolo.
- **Clear all results** svuota l'archivio (chiede conferma).

## Due edifici affiancati

*Two buildings…* fa girare due varianti dal vivo, sugli stessi passeggeri e con lo stesso orologio: scegli lo
scenario, poi per ogni lato la strategia, la regola per le cabine ferme, cabine e zone e le persone per cabina. I due
edifici sono disegnati affiancati con la loro descrizione; il pannello li confronta mentre procedono, il grafico delle
code mostra l'edificio di sinistra a linea piena e quello di destra tratteggiato, il registro e il dispatcher dicono da
quale lato arriva un evento. Le tue chiamate e le ondate vanno in entrambi, un guasto solo nella cabina che clicchi.
Quando entrambi hanno finito, i due risultati vengono salvati in coppia e si apre *Results*. *Back to one building*
torna a un solo edificio.

## Ricette

**Qual è la strategia migliore per il mio ufficio?** *New run…* con i tuoi piani, cabine e viaggi al giorno; poi
*Compare strategies…* con le tre strategie e le regole *Stay* e *Peak* per le cabine ferme. Guarda attesa media e al
95%, scale ed energia per viaggio: raramente c'è un vincitore su tutto.

**Quante cabine mi servono?** *Compare strategies…* con una sola strategia e più righe in *cabins and zones*, per
esempio `4`, `5`, `6`, `8`. Oltre un certo numero le attese smettono di migliorare mentre l'energia per viaggio
continua a salire.

**In una torre conviene dividere in zone?** *Two buildings…* con gli stessi piani: da un lato `24`, dall'altro
`1-20:8, 21-40:8, 41-59:8`. Guarda il picco del mattino, poi confronta viaggi, cambi di cabina ed energia.

**Cosa succede se una cabina si guasta nel momento peggiore?** Parti alle 07:45, manda un *Morning rush*, clicca una
cabina piena di gente. La corsa diventa *hands-on*: confrontala con lo stesso scenario senza guasto.

## Leggere i risultati

- Ogni numero è in tempo simulato. L'**attesa** va dalla chiamata alla prima salita; il **viaggio** dalla chiamata
  all'arrivo, cambi di cabina compresi.
- *95% wait less* significa che 95 passeggeri su 100 hanno aspettato meno di quel tempo: dice sugli sfortunati più di
  quanto dica la media.
- Alle velocità più alte i timer del sistema operativo a volte ritardano una cabina: le medie restano entro pochi
  punti percentuali, gli estremi (attesa più lunga, persone sulle scale) possono uscire peggiori in una singola corsa.
  Per gli estremi ripeti la corsa o usa una velocità più bassa (360x basta).
- Il modello di energia e i tempi sono ordini di grandezza plausibili, fatti per confrontare alternative, non per
  dimensionare un impianto reale.

## Tastiera

| Tasto | Azione |
| --- | --- |
| Barra spaziatrice | Pausa / ripresa |
| S | Avanza di un minuto simulato |
| + / − | Raddoppia / dimezza la velocità |
| Esc | Annulla la chiamata che stai inserendo |
