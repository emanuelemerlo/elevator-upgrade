/*
 * Elevator playground: browser view of the C++ simulation (Elevator.exe --web).
 *
 * The page polls /api/state about ten times per second. Between two answers the cabins are animated
 * locally: the server sends when each floor transition or door movement started (in simulated time)
 * and the page extrapolates the simulated clock with the current speed.
 */
'use strict';

const POLL_MS = 100;
const HISTORY_WINDOW_MS = 3 * 3600 * 1000;
const DAY_MS = 24 * 3600 * 1000;

const $ = (id) => document.getElementById(id);

const ui = {
  clockTime: $('clockTime'),
  clockDay: $('clockDay'),
  playPause: $('playPause'),
  stepButton: $('stepButton'),
  speedRange: $('speedRange'),
  speedValue: $('speedValue'),
  profileChart: $('profileChart'),
  profileCaption: $('profileCaption'),
  canvas: $('building'),
  hint: $('hint'),
  offline: $('offline'),
  statWaiting: $('statWaiting'),
  statRiding: $('statRiding'),
  statDelivered: $('statDelivered'),
  statAvgWait: $('statAvgWait'),
  statP95Wait: $('statP95Wait'),
  statMaxWait: $('statMaxWait'),
  statStairs: $('statStairs'),
  statEnergy: $('statEnergy'),
  statWhPerTrip: $('statWhPerTrip'),
  runInfo: $('runInfo'),
  decisionNote: $('decisionNote'),
  batchBanner: $('batchBanner'),
  batchText: $('batchText'),
  compareDialog: $('compareDialog'),
  compareForm: $('compareForm'),
  compareError: $('compareError'),
  compareCount: $('compareCount'),
  runsDialog: $('runsDialog'),
  scenarioFilter: $('scenarioFilter'),
  scenarioNote: $('scenarioNote'),
  showHandsOn: $('showHandsOn'),
  runsCharts: $('runsCharts'),
  runsTable: $('runsTable'),
  numbers: $('numbers'),
  twinTable: $('twinTable'),
  twinDialog: $('twinDialog'),
  twinForm: $('twinForm'),
  twinError: $('twinError'),
  twinStop: $('twinStop'),
  history: $('history'),
  decisionTitle: $('decisionTitle'),
  decision: $('decision'),
  waveCount: $('waveCount'),
  feed: $('feed'),
  feedAll: $('feedAll'),
  runDialog: $('runDialog'),
  runForm: $('runForm'),
  runError: $('runError'),
  reportDialog: $('reportDialog'),
  reportText: $('reportText'),
  reportNote: $('reportNote'),
  reportResume: $('reportResume'),
};

// ---------------------------------------------------------------------------------------------
// State received from the simulation

let state = null;          // last /api/state answer
let receivedAt = 0;        // performance.now() when it arrived
let lastSeq = 0;           // last event sequence number seen
let config = null;         // /api/config answer (settings and traffic curve)
let minSpeed = 1;
let maxSpeed = 2000;

const myCalls = new Set(); // passengers created by clicking on the building
const history = [];        // { t: simulated ms, waiting: count }

let selectedFloor = null;  // origin floor of a call being placed
let hover = null;          // { kind: 'floor' | 'cabin', floor, cabin }
let layouts = [];          // geometry of every building of the last drawn frame, for hit testing

let decisionPinnedUntil = 0;
let decisionShownAt = 0;
let decisionWaiting = null; // last decision skipped because the previous one was still on screen

/** Simulated time (ms since the start of the run) extrapolated to this animation frame. */
function simNow() {
  if (!state) return 0;
  const clock = state.clock;
  if (clock.paused) return clock.nowMs;
  return clock.nowMs + (performance.now() - receivedAt) * clock.speed;
}

// ---------------------------------------------------------------------------------------------
// Server calls

async function post(path, params) {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(params).toString(),
  });

  const body = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(body.error || `The simulation answered ${response.status}`);
  return body;
}

async function loadConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  config = await response.json();
  minSpeed = Math.max(1, config.minSpeed);
  maxSpeed = config.maxSpeed;
  drawProfile();
}

async function poll() {
  try {
    const response = await fetch(`/api/state?since=${lastSeq}`, { cache: 'no-store' });
    if (!response.ok) throw new Error(String(response.status));
    const next = await response.json();
    onState(next);
    ui.offline.hidden = true;
  } catch (error) {
    ui.offline.hidden = false;
  } finally {
    setTimeout(poll, POLL_MS);
  }
}

function onState(next) {
  const restarted = state && next.clock.nowMs < state.clock.nowMs - 1000;
  if (restarted) {
    history.length = 0;
    myCalls.clear();
    selectedFloor = null;
  }

  state = next;
  receivedAt = performance.now();

  for (const event of next.events) handleEvent(event);
  lastSeq = Math.max(lastSeq, next.lastSeq);

  recordHistory();
  updatePanel();
}

// ---------------------------------------------------------------------------------------------
// Formatting helpers

function clamp(value, low, high) { return Math.min(high, Math.max(low, value)); }

function seconds(ms) {
  if (ms === undefined || ms === null || Number.isNaN(ms)) return '–';
  if (ms < 59500) return `${(ms / 1000).toFixed(ms < 9950 ? 1 : 0)} s`;
  const total = Math.round(ms / 1000);
  return `${Math.floor(total / 60)}:${String(total % 60).padStart(2, '0')} min`;
}

/** Shorter form for the big numbers: seconds up to ten minutes, then minutes. */
function shortDuration(ms) {
  if (ms === undefined || ms === null || Number.isNaN(ms)) return '–';
  if (ms < 9950) return `${(ms / 1000).toFixed(1)} s`;
  if (ms < 600000) return `${Math.round(ms / 1000)} s`;
  return `${Math.round(ms / 60000)} min`;
}

function clockLabel(timeOfDayMs) {
  const minutesOfDay = Math.floor((((timeOfDayMs % DAY_MS) + DAY_MS) % DAY_MS) / 60000);
  const hours = String(Math.floor(minutesOfDay / 60)).padStart(2, '0');
  const minutes = String(minutesOfDay % 60).padStart(2, '0');
  return `${hours}:${minutes}`;
}

function speedLabel(speed) {
  const dayRealSeconds = DAY_MS / 1000 / speed;
  let day;
  if (dayRealSeconds < 120) day = `${Math.round(dayRealSeconds)} s`;
  else if (dayRealSeconds < 7200) day = `${Math.round(dayRealSeconds / 60)} min`;
  else day = `${(dayRealSeconds / 3600).toFixed(dayRealSeconds < 36000 ? 1 : 0)} h`;
  const factor = speed < 10 ? speed.toFixed(1).replace(/\.0$/, '') : Math.round(speed);
  return `×${factor}, a day in ${day}`;
}

// Slider position (0-1000) <-> speed, on a logarithmic scale.
function sliderToSpeed(value) {
  const low = Math.log10(minSpeed);
  const high = Math.log10(maxSpeed);
  return 10 ** (low + (value / 1000) * (high - low));
}

function speedToSlider(speed) {
  const low = Math.log10(minSpeed);
  const high = Math.log10(maxSpeed);
  return Math.round(((Math.log10(clamp(speed, minSpeed, maxSpeed)) - low) / (high - low)) * 1000);
}

function mixColor(a, b, t) {
  const pa = parseInt(a.slice(1), 16);
  const pb = parseInt(b.slice(1), 16);
  const channel = (shift) => Math.round(((pa >> shift) & 255) * (1 - t) + ((pb >> shift) & 255) * t);
  return `#${((channel(16) << 16) | (channel(8) << 8) | channel(0)).toString(16).padStart(6, '0')}`;
}

// ---------------------------------------------------------------------------------------------
// Sky: the page background follows the simulated hour

const SKY = [
  { h: 0, top: '#0b1430', bottom: '#1d2a50' },
  { h: 4.8, top: '#0e1838', bottom: '#2a325e' },
  { h: 6.2, top: '#4d65a3', bottom: '#f0ab76' },
  { h: 7.6, top: '#79b2e2', bottom: '#cfe6f2' },
  { h: 12, top: '#5fa8e6', bottom: '#d3ebf7' },
  { h: 17.4, top: '#76acdc', bottom: '#e2ecef' },
  { h: 19.2, top: '#3f4f8d', bottom: '#f09363' },
  { h: 20.6, top: '#18224c', bottom: '#433a6a' },
  { h: 22, top: '#0b1430', bottom: '#1d2a50' },
  { h: 24, top: '#0b1430', bottom: '#1d2a50' },
];

let lastSkyKey = '';

function skyAt(hour) {
  for (let index = 1; index < SKY.length; index += 1) {
    const before = SKY[index - 1];
    const after = SKY[index];
    if (hour <= after.h) {
      const t = (hour - before.h) / (after.h - before.h);
      return { top: mixColor(before.top, after.top, t), bottom: mixColor(before.bottom, after.bottom, t) };
    }
  }
  return SKY[0];
}

/** 0 at noon, 1 in the middle of the night: drives lights and stars. */
function nightAmount(hour) {
  if (hour >= 7.5 && hour <= 18.5) return 0;
  if (hour > 18.5 && hour < 20.5) return (hour - 18.5) / 2;
  if (hour > 5.5 && hour < 7.5) return 1 - (hour - 5.5) / 2;
  return 1;
}

function updateSky(hour) {
  const sky = skyAt(hour);
  const key = sky.top + sky.bottom;
  if (key === lastSkyKey) return;
  lastSkyKey = key;
  document.documentElement.style.setProperty('--sky-top', sky.top);
  document.documentElement.style.setProperty('--sky-bottom', sky.bottom);
}

// ---------------------------------------------------------------------------------------------
// Events: feed, dispatcher decision, end of the run

function feedTime(event) {
  return clockLabel((state ? state.clock.startTimeOfDayMs : 0) + event.t);
}

function describe(event) {
  const d = event.data;
  const mine = d.id !== undefined && myCalls.has(d.id);
  const all = ui.feedAll.checked;

  switch (event.type) {
    case 'start':
      return { text: `New run: ${d.elevators} cabins, ${d.floors} floors.` };
    case 'call':
      if (mine) return { text: `You called a lift at floor ${d.from} to go to ${d.to} (#${d.id}).`, cls: 'mine' };
      return all ? { text: `#${d.id} calls at floor ${d.from}, going to ${d.to}.` } : null;
    case 'assign':
      if (!mine && !all) return null;
      return {
        text: `#${d.id} gets cabin ${d.elevator}, expected in ${seconds(d.pickupMs)}${d.onTheWay ? '' : ' (it has to turn around)'}.`,
        cls: mine ? 'mine' : '',
      };
    case 'board':
      if (!mine && !all) return null;
      return { text: `#${d.id} boards cabin ${d.elevator} at floor ${d.floor} after ${seconds(d.waitMs)}.`, cls: mine ? 'mine' : '' };
    case 'arrive':
      if (!mine && !all) return null;
      return { text: `#${d.id} reaches floor ${d.floor}, ${seconds(d.journeyMs)} after calling.`, cls: mine ? 'good' : '' };
    case 'refused':
      return { text: `Cabin ${d.elevator} is full: #${d.id} waits for another one.` };
    case 'pending':
      if (d.reason === 'noService') return mine || all ? { text: `#${d.id} waits: no cabin is in service.`, cls: 'fault' } : null;
      return mine || all ? { text: `#${d.id} waits: every cabin is full.` } : null;
    case 'fault':
      return {
        text: `Cabin ${d.elevator} out of order at floor ${d.floor}: ${d.evacuated} ${d.evacuated === 1 ? 'person' : 'people'} got out, ${d.reassigned} ${d.reassigned === 1 ? 'call' : 'calls'} moved to other cabins.`,
        cls: 'fault',
      };
    case 'evacuate':
      return mine ? { text: `#${d.id} had to leave cabin ${d.elevator} at floor ${d.floor}.`, cls: 'fault' } : null;
    case 'repair':
      return { text: `Cabin ${d.elevator} is back in service.`, cls: 'good' };
    case 'wave':
      return { text: `${d.count} people show up (${d.kind === 'random' ? 'random crowd' : `${d.kind} wave`}).` };
    case 'control':
      if (d.action === 'pause') return { text: 'Paused.' };
      if (d.action === 'resume') return { text: 'Running again.' };
      if (d.action === 'step') return { text: 'One step forward.' };
      return null;
    case 'finished':
      return {
        text: `Everybody is home: ${d.completed} trips${d.stairs ? `, ${d.stairs} by the stairs` : ''}. Result saved${d.clean ? '' : ' as a hands-on run'}; the clock is paused.`,
        cls: 'good',
      };
    case 'stairs':
      return mine || all || stairsInFeed()
        ? { text: `#${d.id} gave up after ${seconds(d.waitMs)} at floor ${d.floor} and took the stairs.`, cls: mine ? 'mine' : '' }
        : null;
    case 'batch':
      return { text: `Comparison, variant ${d.index + 1} of ${d.total}: ${d.label}.` };
    case 'twinFinished':
      return { text: 'Both buildings are done: the two results are saved side by side.', cls: 'good' };
    case 'batchDone':
      return { text: d.cancelled ? `Comparison stopped after ${d.completed} of ${d.total} variants.` : `Comparison done: ${d.total} variants.`, cls: 'good' };
    default:
      return null;
  }
}

// The first few stairs takers of a run are always reported, then only with "every passenger".
let stairsReported = 0;
function stairsInFeed() {
  stairsReported += 1;
  return stairsReported <= 5;
}

// Events that are the same in both buildings of a twin run are reported once, from the left building.
const SHARED_EVENTS = ['call', 'wave', 'start', 'control', 'stop'];

function addToFeed(event) {
  if (event.b === 'twin' && SHARED_EVENTS.includes(event.type)) return;
  const entry = describe(event);
  if (!entry) return;
  if (state && state.twin && !SHARED_EVENTS.includes(event.type) && event.type !== 'twinFinished') {
    entry.text = `${event.b === 'twin' ? 'Right' : 'Left'}: ${entry.text}`;
  }

  const item = document.createElement('li');
  const time = document.createElement('time');
  time.textContent = feedTime(event);
  const text = document.createElement('span');
  text.textContent = entry.text;
  if (entry.cls) text.className = entry.cls;
  item.append(time, text);
  ui.feed.prepend(item);

  while (ui.feed.children.length > 150) ui.feed.lastElementChild.remove();
}

const STRATEGY_NOTES = {
  Optimized: 'Each bar is the extra passenger time the call would add to that cabin\'s route (waiting counts double). The shortest bar wins.',
  Nearest: 'Each bar is the distance of the cabin from the call, in floors, whatever the cabin is doing. The closest one wins.',
  Collective: 'Each bar is the distance in floors; a cabin that has to turn around first counts as if it were two building heights further. The shortest bar wins.',
};

function showDecision(event) {
  const d = event.data;
  const mine = myCalls.has(d.id);
  const now = performance.now();

  // At high speed decisions arrive in bursts: keep each one readable, and keep the user's own calls a bit longer.
  if (!mine && (now < decisionPinnedUntil || now - decisionShownAt < 1200)) {
    decisionWaiting = event;
    return;
  }
  decisionWaiting = null;
  decisionShownAt = now;
  if (mine) decisionPinnedUntil = now + 5000;

  const side = state && state.twin ? `${event.b === 'twin' ? 'Right' : 'Left'}: ` : '';
  ui.decisionTitle.textContent =
    `${side}${mine ? 'Your call' : `Call #${d.id}`}, floor ${d.from} to ${d.to}: cabin ${d.elevator}, pickup in ${seconds(d.pickupMs)}.`;

  const optimized = !d.strategy || d.strategy === 'Optimized';
  const scoreOf = (c) => (c.score !== undefined ? c.score : c.costMs);
  const costs = d.candidates.filter((c) => c.state === 'ok').map(scoreOf);
  const maxCost = Math.max(optimized ? 1 : 0.5, ...costs);
  ui.decisionNote.textContent = STRATEGY_NOTES[d.strategy] || STRATEGY_NOTES.Optimized;

  ui.decision.replaceChildren(...d.candidates.map((candidate) => {
    const item = document.createElement('li');
    if (candidate.elevator === d.elevator) item.classList.add('winner');
    if (candidate.state !== 'ok') item.classList.add('excluded');

    const cabin = document.createElement('span');
    cabin.className = 'cabin';
    cabin.textContent = candidate.elevator;

    const bar = document.createElement('span');
    bar.className = 'bar';
    const fill = document.createElement('span');
    fill.style.width = candidate.state === 'ok' ? `${Math.max(3, (scoreOf(candidate) / maxCost) * 100)}%` : '0';
    bar.append(fill);

    const value = document.createElement('span');
    value.className = 'value';
    let label = 'full';
    if (candidate.state === 'outOfOrder') label = 'out of order';
    else if (candidate.state === 'ok' && optimized) label = seconds(scoreOf(candidate));
    else if (candidate.state === 'ok') {
      const floors = Math.abs(Math.round(scoreOf(candidate) % (2 * (state ? state.building.floors : 1000))));
      label = `${floors} fl.${candidate.onTheWay || d.strategy === 'Nearest' ? '' : ' +turn'}`;
    }
    value.textContent = label;

    item.append(cabin, bar, value);
    return item;
  }));
}

let lastBatchScenario = null;

function anyDialogOpen() {
  return Boolean(document.querySelector('dialog[open]'));
}

function handleEvent(event) {
  if (event.type === 'assign') showDecision(event);
  if (event.type === 'stairs') addWalker(event.data, event.b || '');
  if (event.type === 'batchDone' && !event.data.cancelled) openRuns(lastBatchScenario);
  if (event.type === 'batch') lastBatchScenario = null;
  if (event.type === 'twinFinished') openRuns(state ? state.run.scenario : null);
  if (event.type === 'start' && !event.b) {
    stairsReported = 0;
    walkers.length = 0;
    lastWalkerStart.clear();
    if (event.data.batch) lastBatchScenario = event.data.scenario;
    decisionShownAt = 0;
    decisionPinnedUntil = 0;
    decisionWaiting = null;
    ui.decision.replaceChildren();
    ui.decisionTitle.textContent = 'Waiting for the first call.';
    ui.feed.replaceChildren();
  }
  addToFeed(event);
  // A run of a comparison ends quietly: the results open when the whole comparison is done.
  if (event.type === 'finished' && !event.data.batch && !event.data.pair && !anyDialogOpen()) openReport(true);
}

// ---------------------------------------------------------------------------------------------
// Panel: clock, controls, numbers, history

function recordHistory() {
  const t = state.clock.nowMs;
  const last = history[history.length - 1];
  if (last && t - last.t < 30000 && t >= last.t) return;
  history.push({ t, waiting: state.waiting.length, twin: state.twin ? state.twin.waiting.length : null });
  while (history.length && history[0].t < t - HISTORY_WINDOW_MS) history.shift();
}

function updatePanel() {
  const { clock, stats } = state;

  ui.clockTime.textContent = clock.dayOver && clock.finished ? '24:00' : clockLabel(clock.timeOfDayMs);
  let day = `Day ${clock.day} of ${clock.days}`;
  if (clock.finished) day = 'Run complete';
  else if (clock.dayOver) day = 'Day over, last passengers on their way';
  else if (clock.paused) day += ', paused';
  ui.clockDay.textContent = day;

  ui.playPause.classList.toggle('paused', clock.paused);
  ui.playPause.setAttribute('aria-label', clock.paused ? 'Resume' : 'Pause');
  ui.playPause.title = clock.paused ? 'Resume (Space)' : 'Pause (Space)';

  if (!speedDragging) {
    ui.speedRange.value = speedToSlider(clock.speed);
    ui.speedValue.textContent = speedLabel(clock.speed);
  }
  for (const chip of document.querySelectorAll('[data-speed]')) {
    chip.setAttribute('aria-pressed', String(Math.abs(Number(chip.dataset.speed) - clock.speed) < 0.5));
  }

  const riding = state.elevators.reduce((total, elevator) => total + elevator.passengers.length, 0);
  ui.statWaiting.textContent = state.waiting.length;
  ui.statRiding.textContent = riding;
  ui.statDelivered.textContent = stats.completed;
  ui.statAvgWait.textContent = stats.boarded ? shortDuration(stats.avgWaitMs) : '–';
  ui.statP95Wait.textContent = stats.boarded ? shortDuration(stats.p95WaitMs) : '–';
  ui.statMaxWait.textContent = stats.boarded ? shortDuration(stats.maxWaitMs) : '–';
  ui.statStairs.textContent = stats.stairs;
  ui.statEnergy.textContent = `${(stats.energyWh / 1000).toFixed(stats.energyWh < 10000 ? 2 : 1)} kWh`;
  ui.statWhPerTrip.textContent = stats.completed ? `${stats.whPerTrip.toFixed(1)} Wh` : '–';
  ui.statEnergy.title = `Motors and doors ${(stats.travelWh / 1000).toFixed(2)} kWh, standby ${(stats.standbyWh / 1000).toFixed(2)} kWh`;

  updateTwinTable();

  const run = state.run;
  const parking = { Stay: 'idle cabins stay where they stop', Lobby: 'idle cabins go back to the lobby', Spread: 'idle cabins spread along the building' };
  ui.runInfo.innerHTML = '';
  if (state.twin) {
    ui.runInfo.append(`Same passengers in both buildings: scenario ${run.scenario} (seed ${run.seed}). Green is the better side. `);
  } else {
    ui.runInfo.append(`${run.strategy} dispatch, ${parking[run.parking] || run.parking}. Scenario ${run.scenario} (seed ${run.seed}). `);
  }
  if (run.interventions > 0) {
    const handsOn = document.createElement('span');
    handsOn.className = 'hands-on';
    handsOn.textContent = `Hands-on run (${run.interventions} ${run.interventions === 1 ? 'intervention' : 'interventions'}).`;
    ui.runInfo.append(handsOn);
  }

  const batch = run.batch;
  ui.batchBanner.hidden = !batch.running;
  if (batch.running) ui.batchText.textContent = `Comparing ${batch.index + 1} of ${batch.total}: ${batch.label}`;
  for (const button of document.querySelectorAll('[data-wave], #newRunButton, #compareButton, #twinButton')) button.disabled = batch.running;

  updateProfileMarker();
  drawHistory();

  if (decisionWaiting && performance.now() - decisionShownAt >= 1200 && performance.now() >= decisionPinnedUntil) {
    showDecision(decisionWaiting);
  }
}

const TWIN_ROWS = [
  { title: 'Waiting now', value: (b) => b.waiting.length, format: String },
  { title: 'Delivered', value: (b) => b.stats.completed, format: String, higher: true },
  { title: 'Average wait', value: (b) => (b.stats.boarded ? b.stats.avgWaitMs : null), format: shortDuration },
  { title: '95% wait less', value: (b) => (b.stats.boarded ? b.stats.p95WaitMs : null), format: shortDuration },
  { title: 'Longest wait', value: (b) => (b.stats.boarded ? b.stats.maxWaitMs : null), format: shortDuration },
  { title: 'Took the stairs', value: (b) => b.stats.stairs, format: String },
  { title: 'Energy', value: (b) => b.stats.energyWh, format: (v) => `${(v / 1000).toFixed(v < 10000 ? 2 : 1)} kWh` },
  { title: 'Energy per trip', value: (b) => (b.stats.completed ? b.stats.whPerTrip : null), format: (v) => `${v.toFixed(1)} Wh` },
];

function updateTwinTable() {
  const twin = Boolean(state.twin);
  ui.numbers.hidden = twin;
  ui.twinTable.hidden = !twin;
  ui.twinStop.hidden = !twin;
  if (!twin) return;

  const head = document.createElement('thead');
  head.innerHTML = '<tr><th></th><th>Left</th><th>Right</th></tr>';
  const body = document.createElement('tbody');

  for (const row of TWIN_ROWS) {
    const left = row.value(state);
    const right = row.value(state.twin);
    const tr = document.createElement('tr');
    const title = document.createElement('td');
    title.textContent = row.title;
    tr.append(title);

    for (const [value, other] of [[left, right], [right, left]]) {
      const td = document.createElement('td');
      td.textContent = value === null ? '–' : row.format(value);
      if (value !== null && other !== null && value !== other && (row.higher ? value > other : value < other)) td.className = 'better';
      tr.append(td);
    }
    body.append(tr);
  }

  ui.twinTable.replaceChildren(head, body);
}

function drawProfile() {
  if (!config) return;
  const curve = config.trafficCurve;
  const max = Math.max(...curve) * 1.08;
  const points = curve.map((value, index) => `${(index / (curve.length - 1)) * 240},${48 - (value / max) * 44}`);

  ui.profileChart.innerHTML = `
    <rect id="profileElapsed" x="0" y="0" width="0" height="48" fill="rgba(29,40,51,0.08)"></rect>
    <polygon points="0,48 ${points.join(' ')} 240,48" fill="rgba(242,169,59,0.35)"></polygon>
    <polyline points="${points.join(' ')}" fill="none" stroke="#a8630a" stroke-width="1.5" vector-effect="non-scaling-stroke"></polyline>
    <line id="profileNow" x1="0" x2="0" y1="0" y2="48" stroke="#1d2833" stroke-width="2" vector-effect="non-scaling-stroke"></line>`;

  const pattern = config.settings.trafficPattern;
  ui.profileCaption.textContent = pattern === 'Office'
    ? 'Traffic today: office, in at 8, out at 19'
    : pattern === 'Residential'
      ? 'Traffic today: homes, out at 8, back at 19'
      : 'Traffic today: random trips';
}

function updateProfileMarker() {
  const marker = document.getElementById('profileNow');
  const elapsed = document.getElementById('profileElapsed');
  if (!marker || !state) return;
  const x = ((state.clock.timeOfDayMs % DAY_MS) / DAY_MS) * 240;
  marker.setAttribute('x1', x);
  marker.setAttribute('x2', x);
  elapsed.setAttribute('width', x);
}

function drawHistory() {
  const canvas = ui.history;
  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (canvas.width !== Math.round(width * ratio)) {
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
  }

  const ctx = canvas.getContext('2d');
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, width, height);

  ctx.strokeStyle = '#cfd6dd';
  ctx.beginPath();
  ctx.moveTo(0, height - 0.5);
  ctx.lineTo(width, height - 0.5);
  ctx.stroke();

  if (history.length < 2) return;

  const end = state.clock.nowMs;
  const start = end - HISTORY_WINDOW_MS;
  const max = Math.max(4, ...history.map((h) => Math.max(h.waiting, h.twin || 0)));
  const x = (t) => ((t - start) / HISTORY_WINDOW_MS) * width;
  const y = (v) => height - 4 - (v / max) * (height - 10);

  ctx.beginPath();
  history.forEach((point, index) => (index ? ctx.lineTo(x(point.t), y(point.waiting)) : ctx.moveTo(x(point.t), y(point.waiting))));
  ctx.lineTo(x(history[history.length - 1].t), height);
  ctx.lineTo(x(history[0].t), height);
  ctx.closePath();
  ctx.fillStyle = 'rgba(242,169,59,0.3)';
  ctx.fill();

  ctx.beginPath();
  history.forEach((point, index) => (index ? ctx.lineTo(x(point.t), y(point.waiting)) : ctx.moveTo(x(point.t), y(point.waiting))));
  ctx.strokeStyle = '#a8630a';
  ctx.lineWidth = 1.5;
  ctx.stroke();

  // The right building of a twin run, as a dashed line.
  const twinPoints = history.filter((point) => point.twin !== null);
  if (twinPoints.length > 1) {
    ctx.beginPath();
    twinPoints.forEach((point, index) => (index ? ctx.lineTo(x(point.t), y(point.twin)) : ctx.moveTo(x(point.t), y(point.twin))));
    ctx.strokeStyle = '#1d2833';
    ctx.setLineDash([4, 3]);
    ctx.stroke();
    ctx.setLineDash([]);
  }

  ctx.fillStyle = '#4b5967';
  ctx.font = '11px Barlow, sans-serif';
  ctx.fillText(twinPoints.length > 1 ? `max ${max}, left solid, right dashed` : `max ${max}`, 4, 11);
}

// ---------------------------------------------------------------------------------------------
// Building drawing

const COLORS = {
  shaft: '#252a30',
  rail: '#3a4048',
  slab: '#8d97a2',
  wall: '#e7eaee',
  wallNight: '#f3e3c2',
  facade: '#b9c1c9',
  facadeDark: '#7d8792',
  ink: '#1d2833',
  inkSoft: '#4b5967',
  lamp: '#f2a93b',
  lampDeep: '#a8630a',
  fault: '#d1452f',
  cabin: '#c9d0d7',
  cabinDark: '#8b96a1',
  cabinInside: '#f6e6c3',
  door: '#aab4be',
  select: 'rgba(242,169,59,0.35)',
  hover: 'rgba(29,40,51,0.07)',
};

function waitColor(ms) {
  // Green while the wait is short, amber after 20 s, red after 45 s.
  if (ms < 20000) return mixColor('#3a9a6b', '#f2a93b', ms / 20000);
  return mixColor('#f2a93b', '#d1452f', clamp((ms - 20000) / 25000, 0, 1));
}

function computeLayout(areaX, width, height, floors, elevators, top) {
  const bottom = 14;
  // The building is centred in its area and never wider than it needs to be.
  const buildingWidth = Math.min(width - 2 * Math.max(12, width * 0.04), 220 + elevators * 120);
  const x0 = areaX + (width - buildingWidth) / 2;
  const x1 = x0 + buildingWidth;
  const floorHeight = (height - top - bottom) / floors;
  const labelWidth = 38;
  // Shafts take what they need (up to 60% of the width), the landing with the queues gets the rest.
  const shaftWidth = clamp(((buildingWidth - labelWidth) * 0.6) / elevators, 26, 110);
  const landingX = x0 + labelWidth;
  const landingWidth = Math.max(60, buildingWidth - labelWidth - shaftWidth * elevators - 16);
  const shaftsX = landingX + landingWidth + 8;
  return { top, bottom, x0, x1, floorHeight, labelWidth, landingX, landingWidth, shaftsX, shaftWidth, floors, elevators, cabins: [] };
}

function floorTop(geometry, position) {
  return geometry.top + (geometry.floors - 1 - position) * geometry.floorHeight;
}

function cabinPosition(elevator, now, building) {
  if (elevator.motionTo === undefined) return elevator.floor;
  const progress = clamp((now - elevator.motionStartMs) / building.floorMs, 0, 1);
  return elevator.floor + (elevator.motionTo - elevator.floor) * progress;
}

function doorOpening(elevator, now, building) {
  const doorMs = Math.max(1, building.doorMs);
  const progress = clamp((now - elevator.doorsStartMs) / doorMs, 0, 1);
  switch (elevator.doors) {
    case 'open': return 1;
    case 'opening': return progress;
    case 'closing': return 1 - progress;
    default: return 0;
  }
}

function drawSkyDetails(ctx, width, hour) {
  // Sun by day, moon by night, moving along an arc over the roof.
  const night = nightAmount(hour);
  const dayPhase = clamp((hour - 6) / 13, 0, 1);
  const nightPhase = ((hour + 24 - 19) % 24) / 11;
  const phase = night < 0.5 ? dayPhase : clamp(nightPhase, 0, 1);
  const x = width * (0.1 + 0.8 * phase);
  const y = 44 - Math.sin(phase * Math.PI) * 30;

  ctx.save();
  if (night < 0.5) {
    ctx.fillStyle = 'rgba(255,214,120,0.95)';
    ctx.shadowColor = 'rgba(255,200,90,0.8)';
    ctx.shadowBlur = 18;
    ctx.beginPath();
    ctx.arc(x, y, 11, 0, Math.PI * 2);
    ctx.fill();
  } else {
    ctx.fillStyle = 'rgba(240,240,225,0.95)';
    ctx.beginPath();
    ctx.arc(x, y, 9, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = skyAt(hour).top;
    ctx.beginPath();
    ctx.arc(x + 4, y - 3, 8, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = `rgba(255,255,255,${0.7 * night})`;
    for (let index = 0; index < 40; index += 1) {
      const sx = (index * 97.13) % width;
      const sy = (index * 37.7) % 48;
      ctx.fillRect(sx, sy, 1.5, 1.5);
    }
  }
  ctx.restore();
}

function drawPerson(ctx, x, baseY, size, color, ring) {
  const head = size * 0.32;
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.arc(x, baseY - size + head, head, 0, Math.PI * 2);
  ctx.fill();
  ctx.beginPath();
  ctx.roundRect(x - size * 0.3, baseY - size + head * 2 + 1, size * 0.6, size - head * 2 - 1, size * 0.2);
  ctx.fill();

  if (ring) {
    ctx.strokeStyle = ring;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(x, baseY - size + head, head + 2, 0, Math.PI * 2);
    ctx.stroke();
  }
}

function drawTriangle(ctx, x, y, size, up, color) {
  ctx.fillStyle = color;
  ctx.beginPath();
  if (up) {
    ctx.moveTo(x, y - size);
    ctx.lineTo(x + size, y + size * 0.6);
    ctx.lineTo(x - size, y + size * 0.6);
  } else {
    ctx.moveTo(x, y + size);
    ctx.lineTo(x + size, y - size * 0.6);
    ctx.lineTo(x - size, y - size * 0.6);
  }
  ctx.closePath();
  ctx.fill();
}

function drawBuilding() {
  const canvas = ui.canvas;
  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
  }

  const ctx = canvas.getContext('2d');
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, width, height);
  if (!state) return;

  const now = simNow();
  const hour = (((state.clock.startTimeOfDayMs + now) % DAY_MS) / 3600000);
  updateSky(hour);
  drawSkyDetails(ctx, width, hour);

  const night = nightAmount(hour);
  const entries = buildingsOf(state);
  const areaWidth = width / entries.length;
  layouts = entries.map((entry, index) =>
    drawOneBuilding(ctx, entry, index * areaWidth, areaWidth, height, now, night, entries.length > 1));

  // Ground.
  ctx.fillStyle = '#6e7780';
  const groundY = floorTop(layouts[0], 0) + layouts[0].floorHeight;
  ctx.fillRect(0, groundY, width, height - groundY);
}

/** The buildings to draw: the main one, and the twin when a twin run is on. */
function buildingsOf(current) {
  const entries = [{ key: '', side: 'Left', data: current }];
  if (current.twin) entries.push({ key: 'twin', side: 'Right', data: current.twin });
  return entries;
}

function variantLabel(data) {
  const parking = { Stay: 'cabins stay', Lobby: 'back to lobby', Spread: 'spread' }[data.run.parking] || data.run.parking;
  return `${data.run.strategy}, ${parking}, ${data.building.elevators} cabins of ${data.building.maxPeople}`;
}

function drawOneBuilding(ctx, entry, areaX, areaWidth, height, now, night, twin) {
  const b = entry.data;
  const { floors, elevators: elevatorCount, maxPeople } = b.building;
  const g = computeLayout(areaX, areaWidth, height, floors, elevatorCount, twin ? 74 : 54);
  g.key = entry.key;

  if (twin) {
    ctx.fillStyle = night > 0.5 ? '#eef1f4' : '#1d2833';
    ctx.font = '600 15px "Barlow Condensed", sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    ctx.fillText(`${entry.side}: ${variantLabel(b)}`, (g.x0 + g.x1) / 2, 6);
  }

  // Facade and roof.
  ctx.fillStyle = COLORS.facade;
  ctx.fillRect(g.x0 - 6, g.top - 10, g.x1 - g.x0 + 12, height - g.top + 10);
  ctx.fillStyle = COLORS.facadeDark;
  ctx.fillRect(g.x0 - 10, g.top - 14, g.x1 - g.x0 + 20, 6);

  // Machine rooms on the roof, one per shaft.
  for (let index = 0; index < elevatorCount; index += 1) {
    const sx = g.shaftsX + index * g.shaftWidth + g.shaftWidth * 0.2;
    ctx.fillStyle = COLORS.facadeDark;
    ctx.fillRect(sx, g.top - 28, g.shaftWidth * 0.6, 14);
  }

  const waitingByFloor = new Map();
  for (const person of b.waiting) {
    if (!waitingByFloor.has(person.from)) waitingByFloor.set(person.from, []);
    waitingByFloor.get(person.from).push(person);
  }

  const elapsedSinceState = now - state.clock.nowMs;
  const width = areaX + areaWidth;
  const wall = mixColor(COLORS.wall, COLORS.wallNight, night * 0.8);

  for (let floor = 0; floor < floors; floor += 1) {
    const y = floorTop(g, floor);
    const h = g.floorHeight;

    // Landing wall.
    ctx.fillStyle = wall;
    ctx.fillRect(g.x0, y, g.x1 - g.x0, h);

    if (selectedFloor === floor) {
      ctx.fillStyle = COLORS.select;
      ctx.fillRect(g.x0, y, g.landingX + g.landingWidth - g.x0, h);
    } else if (hover && hover.kind === 'floor' && hover.floor === floor && (selectedFloor !== null || hover.building === g.key)) {
      ctx.fillStyle = selectedFloor !== null ? COLORS.select : COLORS.hover;
      ctx.fillRect(g.x0, y, g.landingX + g.landingWidth - g.x0, h);
    }

    // Slab.
    ctx.fillStyle = COLORS.slab;
    ctx.fillRect(g.x0, y + h - 2, g.x1 - g.x0, 2);

    // Floor number, like the plate next to the lift doors.
    ctx.fillStyle = COLORS.ink;
    ctx.font = `600 ${clamp(h * 0.45, 10, 20)}px "Barlow Condensed", sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(String(floor), g.x0 + g.labelWidth / 2, y + h / 2);

    // The door of the stairs, at the far end of the landing.
    const door = stairsDoor(g, floor);
    drawStairsDoor(ctx, door);

    // Waiting passengers, lined up towards the lifts.
    const people = waitingByFloor.get(floor) || [];
    const size = clamp(h * 0.62, 8, 22);
    const spacing = size * 0.75;
    const capacity = Math.max(1, Math.floor((g.landingWidth - 46 - door.w) / spacing));
    const shown = people.slice(0, capacity);
    shown.forEach((person, index) => {
      const px = g.landingX + g.landingWidth - 30 - index * spacing;
      const waited = person.waitMs + Math.max(0, elapsedSinceState);
      const ring = myCalls.has(person.id) ? COLORS.ink : person.evacuated ? COLORS.fault : null;
      drawPerson(ctx, px, y + h - 3, size, waitColor(waited), ring);
    });
    if (people.length > shown.length) {
      ctx.fillStyle = COLORS.inkSoft;
      ctx.font = `600 ${clamp(h * 0.4, 9, 14)}px Barlow, sans-serif`;
      ctx.textAlign = 'left';
      ctx.fillText(`+${people.length - shown.length}`, g.landingX + 2, y + h / 2);
    }

    // Call buttons: lit when somebody is waiting to go up or down.
    const goingUp = people.some((p) => p.to > floor);
    const goingDown = people.some((p) => p.to < floor);
    const bx = g.landingX + g.landingWidth - 10;
    const triangle = clamp(h * 0.12, 2.5, 5);
    if (floor < floors - 1) drawTriangle(ctx, bx, y + h * 0.33, triangle, true, goingUp ? COLORS.lamp : '#c3cad1');
    if (floor > 0) drawTriangle(ctx, bx, y + h * 0.7, triangle, false, goingDown ? COLORS.lamp : '#c3cad1');
  }

  drawWalkers(ctx, g, entry.key);

  // Shafts and cabins.
  g.cabins = [];
  b.elevators.forEach((elevator, index) => {
    const shaftX = g.shaftsX + index * g.shaftWidth + g.shaftWidth * 0.1;
    const shaftW = g.shaftWidth * 0.8;
    const shaftTop = g.top;
    const shaftBottom = floorTop(g, 0) + g.floorHeight;

    ctx.fillStyle = elevator.outOfOrder ? '#3a2a2a' : COLORS.shaft;
    ctx.fillRect(shaftX, shaftTop, shaftW, shaftBottom - shaftTop);
    ctx.fillStyle = COLORS.rail;
    ctx.fillRect(shaftX + 3, shaftTop, 2, shaftBottom - shaftTop);
    ctx.fillRect(shaftX + shaftW - 5, shaftTop, 2, shaftBottom - shaftTop);

    // Floors where this cabin has to stop.
    for (const stop of elevator.stops) {
      const sy = floorTop(g, stop.floor) + g.floorHeight / 2;
      ctx.fillStyle = COLORS.lamp;
      ctx.beginPath();
      ctx.arc(shaftX - 4, sy, 2.5, 0, Math.PI * 2);
      ctx.fill();
    }

    const position = cabinPosition(elevator, now, b.building);
    const cabinH = g.floorHeight * 0.84;
    const cabinW = shaftW * 0.86;
    const cx = shaftX + (shaftW - cabinW) / 2;
    const cy = floorTop(g, position) + g.floorHeight * 0.08;
    g.cabins.push({ id: elevator.id, x: cx, y: cy, w: cabinW, h: cabinH, outOfOrder: elevator.outOfOrder });

    // Interior, visible when the doors open.
    ctx.fillStyle = COLORS.cabinInside;
    ctx.fillRect(cx, cy, cabinW, cabinH);

    const riders = elevator.passengers.length;
    const riderSize = clamp(cabinH * 0.62, 7, 20);
    const perRow = Math.max(1, Math.floor((cabinW - 6) / (riderSize * 0.55)));
    for (let rider = 0; rider < Math.min(riders, perRow); rider += 1) {
      const passenger = elevator.passengers[rider];
      drawPerson(ctx, cx + 5 + riderSize * 0.3 + rider * riderSize * 0.55, cy + cabinH - 2, riderSize,
        myCalls.has(passenger.id) ? COLORS.ink : '#5f6f80', null);
    }

    // Doors sliding from the middle.
    const opening = doorOpening(elevator, now, b.building);
    const panel = (cabinW / 2) * (1 - opening);
    const doorGradient = ctx.createLinearGradient(cx, 0, cx + cabinW, 0);
    doorGradient.addColorStop(0, COLORS.cabinDark);
    doorGradient.addColorStop(0.5, COLORS.cabin);
    doorGradient.addColorStop(1, COLORS.cabinDark);
    ctx.fillStyle = doorGradient;
    ctx.fillRect(cx, cy, panel, cabinH);
    ctx.fillRect(cx + cabinW - panel, cy, panel, cabinH);
    if (panel > 1) {
      ctx.fillStyle = 'rgba(29,40,51,0.35)';
      ctx.fillRect(cx + panel - 1, cy, 1, cabinH);
      ctx.fillRect(cx + cabinW - panel, cy, 1, cabinH);
    }

    // Frame and load bar.
    ctx.strokeStyle = elevator.outOfOrder ? COLORS.fault : '#5d6875';
    ctx.lineWidth = elevator.outOfOrder ? 2 : 1;
    ctx.strokeRect(cx + 0.5, cy + 0.5, cabinW - 1, cabinH - 1);
    const load = clamp(riders / Math.max(1, maxPeople), 0, 1);
    ctx.fillStyle = load >= 1 ? COLORS.fault : COLORS.lamp;
    ctx.fillRect(cx, cy + cabinH - 3, cabinW * load, 3);

    if (elevator.outOfOrder) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(cx, cy, cabinW, cabinH);
      ctx.clip();
      ctx.strokeStyle = 'rgba(209,69,47,0.55)';
      ctx.lineWidth = 3;
      for (let stripe = -cabinH; stripe < cabinW; stripe += 9) {
        ctx.beginPath();
        ctx.moveTo(cx + stripe, cy + cabinH);
        ctx.lineTo(cx + stripe + cabinH, cy);
        ctx.stroke();
      }
      ctx.restore();
    }

    if (hover && hover.kind === 'cabin' && hover.cabin === elevator.id && hover.building === g.key) {
      ctx.strokeStyle = COLORS.lamp;
      ctx.lineWidth = 2;
      ctx.strokeRect(cx - 2, cy - 2, cabinW + 4, cabinH + 4);
    }

    // Label above the shaft: cabin name, direction lamp, people on board.
    const labelY = g.top - 21;
    const labelX = shaftX + shaftW / 2;
    ctx.fillStyle = elevator.outOfOrder ? COLORS.fault : '#f4f6f8';
    ctx.font = `700 ${clamp(g.shaftWidth * 0.2, 11, 15)}px "Barlow Condensed", sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(elevator.outOfOrder ? `${elevator.id} out` : elevator.id, labelX - (elevator.outOfOrder ? 0 : 8), labelY);
    if (!elevator.outOfOrder) {
      if (elevator.dir === 'up') drawTriangle(ctx, labelX + 6, labelY, 4, true, COLORS.lamp);
      else if (elevator.dir === 'down') drawTriangle(ctx, labelX + 6, labelY, 4, false, COLORS.lamp);
    }
    if (riders > perRow || cabinH > 26) {
      ctx.fillStyle = COLORS.ink;
      ctx.font = `600 ${clamp(cabinH * 0.3, 9, 13)}px Barlow, sans-serif`;
      ctx.textAlign = 'right';
      ctx.textBaseline = 'top';
      if (riders > 0 && opening < 0.5) {
        ctx.fillStyle = '#1d2833';
        ctx.fillText(String(riders), cx + cabinW - 3, cy + 2);
      }
    }
  });

  return g;
}

// ---------------------------------------------------------------------------------------------
// The stairs: passengers who lose their patience leave the queue and walk, one after the other,
// to the door of the stairs at the far end of the landing.

const WALK_MS = 1900;           // real time of the walk, whatever the simulation speed
const WALK_GAP_MS = 320;        // distance between two walkers leaving the same floor
const MAX_WALKERS = 60;
const walkers = [];
const lastWalkerStart = new Map();

function addWalker(data, building) {
  if (walkers.length >= MAX_WALKERS) return;
  const now = performance.now();
  const key = `${building}|${data.floor}`;
  const start = Math.max(now, (lastWalkerStart.get(key) || 0) + WALK_GAP_MS);
  lastWalkerStart.set(key, start);
  walkers.push({ building, floor: data.floor, start, mine: myCalls.has(data.id), color: waitColor(data.waitMs) });
}

function stairsDoor(g, floor) {
  const h = g.floorHeight;
  const w = clamp(h * 0.5, 7, 22);
  const doorH = Math.min(h - 4, w * 1.9);
  return { x: g.landingX + 4, y: floorTop(g, floor) + h - 2 - doorH, w, h: doorH };
}

function drawStairsDoor(ctx, door) {
  ctx.fillStyle = '#5e7c66';
  ctx.fillRect(door.x, door.y, door.w, door.h);
  ctx.strokeStyle = '#3f5646';
  ctx.lineWidth = 1;
  ctx.strokeRect(door.x + 0.5, door.y + 0.5, door.w - 1, door.h - 1);

  // Stairs pictogram: three steps climbing to the right.
  if (door.w >= 9) {
    const step = door.w * 0.18;
    const px = door.x + door.w * 0.22;
    const py = door.y + door.h * 0.62;
    ctx.strokeStyle = '#eef1f4';
    ctx.lineWidth = Math.max(1, door.w * 0.07);
    ctx.beginPath();
    ctx.moveTo(px, py + step);
    for (let index = 0; index < 3; index += 1) {
      ctx.lineTo(px + step * index, py + step * (1 - index));
      ctx.lineTo(px + step * (index + 1), py + step * (1 - index));
    }
    ctx.stroke();
  }
}

function drawWalkers(ctx, g, building) {
  const now = performance.now();

  for (let index = walkers.length - 1; index >= 0; index -= 1) {
    const walker = walkers[index];
    if (walker.building !== building) continue;
    if (walker.floor >= g.floors || now - walker.start > WALK_MS) {
      walkers.splice(index, 1);
      continue;
    }
    if (now < walker.start) continue;

    const progress = (now - walker.start) / WALK_MS;
    const door = stairsDoor(g, walker.floor);
    const fromX = g.landingX + g.landingWidth - 30;
    const toX = door.x + door.w / 2;
    const x = fromX + (toX - fromX) * Math.min(1, progress / 0.9);
    const size = clamp(g.floorHeight * 0.62, 8, 22);
    const bob = Math.abs(Math.sin(progress * Math.PI * 9)) * Math.max(1, size * 0.08);

    // The walker fades through the door during the last tenth of the walk.
    ctx.globalAlpha = progress < 0.9 ? 1 : (1 - progress) / 0.1;
    drawPerson(ctx, x, floorTop(g, walker.floor) + g.floorHeight - 3 - bob, size, walker.color, walker.mine ? COLORS.ink : null);
    ctx.globalAlpha = 1;
  }
}

function frame() {
  drawBuilding();
  requestAnimationFrame(frame);
}

// ---------------------------------------------------------------------------------------------
// Interaction with the building

function hitTest(event) {
  if (!layouts.length || !state) return null;
  const rect = ui.canvas.getBoundingClientRect();
  const x = event.clientX - rect.left;
  const y = event.clientY - rect.top;

  for (const g of layouts) {
    for (const cabin of g.cabins) {
      if (x >= cabin.x - 3 && x <= cabin.x + cabin.w + 3 && y >= cabin.y - 3 && y <= cabin.y + cabin.h + 3) {
        return { kind: 'cabin', building: g.key, cabin: cabin.id, outOfOrder: cabin.outOfOrder };
      }
    }

    if (x >= g.x0 && x <= g.landingX + g.landingWidth && y >= g.top) {
      const floor = g.floors - 1 - Math.floor((y - g.top) / g.floorHeight);
      if (floor >= 0 && floor < g.floors) return { kind: 'floor', building: g.key, floor };
    }
  }

  return null;
}

function setHint(text, active) {
  ui.hint.textContent = text;
  ui.hint.classList.toggle('active', Boolean(active));
}

const DEFAULT_HINT = 'Click a floor to call a lift, then the floor to go to. Click a cabin to put it out of order.';

ui.canvas.addEventListener('mousemove', (event) => {
  hover = hitTest(event);
  ui.canvas.style.cursor = hover ? 'pointer' : 'default';
  if (selectedFloor !== null) return;
  if (hover && hover.kind === 'cabin') {
    setHint(hover.outOfOrder ? `Click to put cabin ${hover.cabin} back in service.` : `Click to put cabin ${hover.cabin} out of order.`);
  } else {
    setHint(DEFAULT_HINT);
  }
});

ui.canvas.addEventListener('mouseleave', () => { hover = null; });

ui.canvas.addEventListener('click', async (event) => {
  const hit = hitTest(event);
  if (!hit) return;

  try {
    if (hit.kind === 'cabin') {
      await post('/api/elevator', { id: hit.cabin, outOfOrder: String(!hit.outOfOrder), building: hit.building });
      const where = state.twin ? ` of the ${hit.building === 'twin' ? 'right' : 'left'} building` : '';
      setHint(hit.outOfOrder
        ? `Cabin ${hit.cabin}${where} is going back into service.`
        : `Cabin ${hit.cabin}${where} stops at the next floor and lets everybody out. Click it again to repair it.`);
      return;
    }

    if (selectedFloor === null) {
      selectedFloor = hit.floor;
      setHint(`Calling from floor ${hit.floor}${state.twin ? ' (in both buildings)' : ''}: now click the floor to go to (Esc to cancel).`, true);
      return;
    }

    if (selectedFloor === hit.floor) {
      cancelSelection();
      return;
    }

    const from = selectedFloor;
    cancelSelection();
    // In a twin run the same passenger is added to both buildings.
    const answer = await post('/api/call', { from, to: hit.floor });
    for (const id of answer.ids || [answer.id]) myCalls.add(id);
  } catch (error) {
    setHint(error.message, true);
  }
});

function cancelSelection() {
  selectedFloor = null;
  setHint(DEFAULT_HINT);
}

// ---------------------------------------------------------------------------------------------
// Controls

let speedDragging = false;
let speedTimer = null;

async function control(params) {
  try {
    await post('/api/control', params);
  } catch (error) {
    setHint(error.message, true);
  }
}

function togglePause() {
  if (!state) return;
  control({ action: state.clock.paused ? 'resume' : 'pause' });
}

function setSpeed(speed) {
  ui.speedValue.textContent = speedLabel(speed);
  clearTimeout(speedTimer);
  speedTimer = setTimeout(() => control({ action: 'speed', value: speed.toFixed(3) }), 120);
}

ui.playPause.addEventListener('click', togglePause);
ui.stepButton.addEventListener('click', () => control({ action: 'step', ms: 60000 }));

ui.speedRange.addEventListener('input', () => {
  speedDragging = true;
  setSpeed(sliderToSpeed(Number(ui.speedRange.value)));
});
ui.speedRange.addEventListener('change', () => { speedDragging = false; });

for (const chip of document.querySelectorAll('[data-speed]')) {
  chip.addEventListener('click', () => setSpeed(Number(chip.dataset.speed)));
}

for (const button of document.querySelectorAll('[data-wave]')) {
  button.addEventListener('click', async () => {
    try {
      await post('/api/wave', { kind: button.dataset.wave, count: ui.waveCount.value || 30 });
    } catch (error) {
      setHint(error.message, true);
    }
  });
}

document.addEventListener('keydown', (event) => {
  const typing = ['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement.tagName) || document.querySelector('dialog[open]');
  if (typing) return;

  if (event.key === ' ') {
    event.preventDefault();
    togglePause();
  } else if (event.key === 's' || event.key === 'S') {
    control({ action: 'step', ms: 60000 });
  } else if (event.key === 'Escape') {
    cancelSelection();
  } else if ((event.key === '+' || event.key === '=') && state) {
    setSpeed(clamp(state.clock.speed * 2, minSpeed, maxSpeed));
  } else if (event.key === '-' && state) {
    setSpeed(clamp(state.clock.speed / 2, minSpeed, maxSpeed));
  }
});

ui.feedAll.addEventListener('change', () => ui.feed.replaceChildren());

// ---------------------------------------------------------------------------------------------
// New run and report

$('newRunButton').addEventListener('click', async () => {
  await loadConfig();
  const s = config.settings;
  const form = ui.runForm.elements;
  form.numberOfElevators.value = s.numberOfElevators;
  form.numberOfFloors.value = s.numberOfFloors;
  form.maxPeople.value = s.maxPeople;
  form.averageCallsPerDay.value = s.averageCallsPerDay;
  form.trafficPattern.value = s.trafficPattern;
  form.numberOfSimulationDays.value = s.numberOfSimulationDays;
  form.dayStartHour.value = s.dayStartHour;
  // The current speed, which the user may have changed, rather than the one of config.json.
  form.dayMinutes.value = state ? +(DAY_MS / 60000 / state.clock.speed).toFixed(2) : s.dayDurationSeconds / 60;
  form.randomSeed.value = s.randomSeed;
  form.floorSeconds.value = s.timeToReachTheNextFloorMs / 1000;
  form.doorSeconds.value = s.doorTransitionTimeMs / 1000;
  form.dwellSeconds.value = s.enterAndExitTimeMs / 1000;
  form.dispatchStrategy.value = s.dispatchStrategy;
  form.idleParking.value = s.idleParking;
  form.stairsPatienceSeconds.value = s.stairsPatienceSeconds;
  form.standbyPowerW.value = s.standbyPowerW;
  form.motorEfficiency.value = s.motorEfficiency;
  ui.runError.textContent = '';
  ui.runDialog.showModal();
});

$('runCancel').addEventListener('click', () => ui.runDialog.close());

ui.runForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = ui.runForm.elements;

  const params = {
    numberOfElevators: form.numberOfElevators.value,
    numberOfFloors: form.numberOfFloors.value,
    maxPeople: form.maxPeople.value,
    averageCallsPerDay: form.averageCallsPerDay.value,
    trafficPattern: form.trafficPattern.value,
    numberOfSimulationDays: form.numberOfSimulationDays.value,
    dayStartHour: form.dayStartHour.value,
    dayDurationSeconds: String(Number(form.dayMinutes.value) * 60),
    randomSeed: form.randomSeed.value || '0',
    timeToReachTheNextFloorMs: String(Math.round(Number(form.floorSeconds.value) * 1000)),
    doorTransitionTimeMs: String(Math.round(Number(form.doorSeconds.value) * 1000)),
    enterAndExitTimeMs: String(Math.round(Number(form.dwellSeconds.value) * 1000)),
    dispatchStrategy: form.dispatchStrategy.value,
    idleParking: form.idleParking.value,
    stairsPatienceSeconds: form.stairsPatienceSeconds.value || '0',
    standbyPowerW: form.standbyPowerW.value || '0',
    motorEfficiency: form.motorEfficiency.value || '0.75',
  };

  try {
    await post('/api/restart', params);
    ui.runDialog.close();
    history.length = 0;
    myCalls.clear();
    cancelSelection();
    await loadConfig();
  } catch (error) {
    ui.runError.textContent = error.message;
  }
});

async function openReport(finished) {
  try {
    const response = await fetch('/api/report', { cache: 'no-store' });
    const report = await response.json();
    let text = report.lines.join('\n');
    if (state && state.twin) {
      const twinResponse = await fetch('/api/report?building=twin', { cache: 'no-store' });
      const twinReport = await twinResponse.json();
      text = `Left building: ${variantLabel(state)}\n\n${text}\n\n\nRight building: ${variantLabel(state.twin)}\n\n${twinReport.lines.join('\n')}`;
    }
    ui.reportText.textContent = text;
    ui.reportNote.textContent = finished
      ? 'Everybody has arrived. This is the report the console version prints at the end of a run.'
      : 'The report of the run so far, as the console version prints it at the end.';
    ui.reportResume.hidden = !finished;
    ui.reportDialog.showModal();
  } catch (error) {
    setHint(error.message, true);
  }
}

$('reportButton').addEventListener('click', () => openReport(false));
ui.reportResume.addEventListener('click', () => {
  ui.reportDialog.close();
  control({ action: 'resume' });
});

// ---------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------
// Comparison of strategies

function checkedValues(name) {
  return [...ui.compareForm.querySelectorAll(`input[name="${name}"]:checked`)].map((input) => input.value);
}

function cabinCounts() {
  const text = ui.compareForm.elements.elevators.value.trim();
  if (!text) return [];
  return text.split(/[,\s]+/).filter(Boolean);
}

function updateCompareCount() {
  const strategies = checkedValues('strategy').length;
  const parkings = checkedValues('parking').length;
  const cabins = Math.max(1, cabinCounts().length);
  const variants = strategies * parkings * cabins;
  const days = Number(ui.compareForm.elements.numberOfSimulationDays.value) || 1;
  const startHour = Number(ui.compareForm.elements.dayStartHour.value) || 0;
  const hours = days * 24 - startHour;
  const seconds = Math.round(variants * hours * 3600 / maxSpeed) + variants * 2;
  ui.compareCount.textContent = variants
    ? `${variants} ${variants === 1 ? 'variant' : 'variants'}, about ${seconds < 120 ? `${seconds} s` : `${Math.round(seconds / 60)} min`} in total.`
    : 'Choose at least one strategy and one parking rule.';
}

$('compareButton').addEventListener('click', async () => {
  await loadConfig();
  const s = config.settings;
  const form = ui.compareForm.elements;
  form.numberOfFloors.value = s.numberOfFloors;
  form.maxPeople.value = s.maxPeople;
  form.averageCallsPerDay.value = s.averageCallsPerDay;
  form.trafficPattern.value = s.trafficPattern;
  form.numberOfSimulationDays.value = s.numberOfSimulationDays;
  form.dayStartHour.value = s.dayStartHour;
  // By default the passengers of the run on screen, so that it can be compared with the variants.
  form.randomSeed.value = state ? state.run.seed : s.randomSeed;
  form.elevators.value = String(s.numberOfElevators);
  ui.compareError.textContent = '';
  updateCompareCount();
  ui.compareDialog.showModal();
});

ui.compareForm.addEventListener('input', updateCompareCount);
$('compareCancel').addEventListener('click', () => ui.compareDialog.close());

ui.compareForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = ui.compareForm.elements;
  const strategies = checkedValues('strategy');
  const parkings = checkedValues('parking');
  if (!strategies.length || !parkings.length) {
    ui.compareError.textContent = 'Choose at least one strategy and one parking rule.';
    return;
  }

  try {
    await post('/api/batch', {
      numberOfFloors: form.numberOfFloors.value,
      maxPeople: form.maxPeople.value,
      averageCallsPerDay: form.averageCallsPerDay.value,
      trafficPattern: form.trafficPattern.value,
      numberOfSimulationDays: form.numberOfSimulationDays.value,
      dayStartHour: form.dayStartHour.value,
      randomSeed: form.randomSeed.value || '0',
      strategies: strategies.join(','),
      parkings: parkings.join(','),
      elevators: cabinCounts().join(','),
    });
    ui.compareDialog.close();
  } catch (error) {
    ui.compareError.textContent = error.message;
  }
});

// ---------------------------------------------------------------------------------------------
// Two buildings side by side

$('twinButton').addEventListener('click', async () => {
  await loadConfig();
  const s = config.settings;
  const form = ui.twinForm.elements;
  form.numberOfFloors.value = s.numberOfFloors;
  form.averageCallsPerDay.value = s.averageCallsPerDay;
  form.trafficPattern.value = s.trafficPattern;
  form.numberOfSimulationDays.value = s.numberOfSimulationDays;
  form.dayStartHour.value = s.dayStartHour;
  form.randomSeed.value = state ? state.run.seed : s.randomSeed;
  form.leftDispatchStrategy.value = s.dispatchStrategy;
  form.leftIdleParking.value = s.idleParking;
  form.leftNumberOfElevators.value = s.numberOfElevators;
  form.leftMaxPeople.value = s.maxPeople;
  form.rightDispatchStrategy.value = s.dispatchStrategy === 'Optimized' ? 'Nearest' : 'Optimized';
  form.rightIdleParking.value = s.idleParking;
  form.rightNumberOfElevators.value = s.numberOfElevators;
  form.rightMaxPeople.value = s.maxPeople;
  ui.twinError.textContent = '';
  ui.twinDialog.showModal();
});

$('twinCancel').addEventListener('click', () => ui.twinDialog.close());

ui.twinForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const params = Object.fromEntries(new FormData(ui.twinForm).entries());
  params.randomSeed = params.randomSeed || '0';
  // Keep the speed of the run on screen, at most the one where a day lasts 4 minutes, to watch it.
  params.dayDurationSeconds = String(DAY_MS / 1000 / Math.min(state ? state.clock.speed : 60, 360));

  try {
    await post('/api/twin', params);
    ui.twinDialog.close();
    history.length = 0;
    myCalls.clear();
    cancelSelection();
  } catch (error) {
    ui.twinError.textContent = error.message;
  }
});

ui.twinStop.addEventListener('click', () => post('/api/twin/stop', {}).catch((error) => setHint(error.message, true)));

$('batchCancel').addEventListener('click', () => post('/api/batch/cancel', {}).catch(() => {}));

// ---------------------------------------------------------------------------------------------
// Results: archive of the completed runs

let archive = [];

// Columns of the table: key, title, format, and for the metrics whether lower is better.
const COLUMNS = [
  { key: 'finishedAt', title: 'Finished', text: true },
  { key: 'strategy', title: 'Strategy', text: true },
  { key: 'parking', title: 'Idle cabins', text: true },
  { key: 'elevators', title: 'Cabins' },
  { key: 'maxPeople', title: 'People/cabin' },
  { key: 'trips', title: 'Trips' },
  { key: 'avgWaitMs', title: 'Avg wait', format: (v) => seconds(v), lower: true },
  { key: 'p95WaitMs', title: '95% wait', format: (v) => seconds(v), lower: true },
  { key: 'maxWaitMs', title: 'Max wait', format: (v) => seconds(v), lower: true },
  { key: 'avgJourneyMs', title: 'Avg journey', format: (v) => seconds(v), lower: true },
  { key: 'stairs', title: 'Stairs', lower: true },
  { key: 'energyKWh', title: 'Energy', format: (v) => `${v.toFixed(2)} kWh`, lower: true },
  { key: 'whPerTrip', title: 'Wh/trip', format: (v) => v.toFixed(1), lower: true },
  { key: 'travelledFloors', title: 'Floors run', lower: true },
  { key: 'doorCycles', title: 'Door cycles', lower: true },
];

const CHARTS = [
  { key: 'avgWaitMs', title: 'Average wait', format: (v) => seconds(v) },
  { key: 'p95WaitMs', title: '95% of the passengers wait less than', format: (v) => seconds(v) },
  { key: 'energyKWh', title: 'Energy for the whole run', format: (v) => `${v.toFixed(1)} kWh` },
  { key: 'stairs', title: 'Took the stairs', format: (v) => String(v) },
];

function scenarioLabel(run) {
  const pattern = { Office: 'office', Residential: 'homes', Uniform: 'random trips' }[run.pattern] || run.pattern;
  return `${run.scenario}: ${run.floors} floors, ${pattern}, ${run.callsPerDay} trips/day, ${run.days} ${run.days === 1 ? 'day' : 'days'} from ${String(Math.floor(run.startHour)).padStart(2, '0')}:${String(Math.round((run.startHour % 1) * 60)).padStart(2, '0')}, seed ${run.seed}`;
}

function runName(run) {
  return `${run.strategy}, ${run.parking}, ${run.elevators} cab.`;
}

async function openRuns(scenario) {
  try {
    const response = await fetch('/api/runs', { cache: 'no-store' });
    archive = (await response.json()).runs;
  } catch (error) {
    setHint(error.message, true);
    return;
  }

  // Scenarios, most recent first.
  const scenarios = [];
  for (const run of [...archive].reverse()) {
    if (!scenarios.some((item) => item.scenario === run.scenario)) scenarios.push(run);
  }

  ui.scenarioFilter.replaceChildren(...scenarios.map((run) => {
    const option = document.createElement('option');
    option.value = run.scenario;
    option.textContent = scenarioLabel(run);
    return option;
  }));

  const wanted = scenario || (state && scenarios.some((item) => item.scenario === state.run.scenario) ? state.run.scenario : null);
  if (wanted) ui.scenarioFilter.value = wanted;

  renderRuns();
  if (!ui.runsDialog.open) ui.runsDialog.showModal();
}

function renderRuns() {
  const scenario = ui.scenarioFilter.value;
  const rows = archive.filter((run) => run.scenario === scenario && (run.clean || ui.showHandsOn.checked));
  const handsOnHidden = archive.filter((run) => run.scenario === scenario && !run.clean).length;

  if (!archive.length) {
    ui.scenarioNote.textContent = 'No results yet: a run is saved when its last passenger arrives. Start a comparison to fill this table quickly.';
  } else {
    ui.scenarioNote.textContent = `${rows.length} ${rows.length === 1 ? 'run' : 'runs'} with exactly the same passengers` +
      (handsOnHidden && !ui.showHandsOn.checked ? `, ${handsOnHidden} hands-on ${handsOnHidden === 1 ? 'run' : 'runs'} hidden.` : '.');
  }

  // Best value of every metric among the rows shown.
  const best = {};
  for (const column of COLUMNS.filter((c) => c.lower)) {
    const values = rows.filter((run) => run.clean).map((run) => run[column.key]);
    // Only when there is something to choose: a column where every run has the same value has no best.
    if (values.length > 1 && Math.min(...values) !== Math.max(...values)) best[column.key] = Math.min(...values);
  }

  const head = document.createElement('tr');
  for (const column of COLUMNS) {
    const th = document.createElement('th');
    th.textContent = column.title;
    if (column.text) th.className = 'text';
    head.append(th);
  }
  head.append(document.createElement('th'));

  const body = rows.map((run) => {
    const tr = document.createElement('tr');
    if (!run.clean) tr.className = 'hands-on';
    for (const column of COLUMNS) {
      const td = document.createElement('td');
      const value = run[column.key];
      td.textContent = column.format ? column.format(value) : value;
      if (column.text) td.className = 'text';
      if (run.clean && best[column.key] !== undefined && value === best[column.key]) td.classList.add('best');
      tr.append(td);
    }
    const action = document.createElement('td');
    const watch = document.createElement('button');
    watch.type = 'button';
    watch.className = 'btn';
    watch.textContent = 'Watch';
    watch.title = 'Run this variant again live, at the current speed';
    watch.addEventListener('click', () => watchRun(run));
    action.append(watch);
    tr.append(action);
    return tr;
  });

  ui.runsTable.replaceChildren(head, ...body);
  renderCharts(rows.filter((run) => run.clean));
}

function renderCharts(rows) {
  if (rows.length < 2) {
    ui.runsCharts.replaceChildren();
    return;
  }

  const charts = CHARTS.filter((chart) => {
    const values = rows.map((run) => run[chart.key]);
    return Math.min(...values) !== Math.max(...values);
  });

  ui.runsCharts.replaceChildren(...charts.map((chart) => {
    const box = document.createElement('div');
    box.className = 'chart';
    const title = document.createElement('h3');
    title.textContent = chart.title;
    const list = document.createElement('ol');
    const values = rows.map((run) => run[chart.key]);
    const max = Math.max(...values, 1e-9);
    const min = Math.min(...values);

    const sorted = [...rows].sort((a, b) => a[chart.key] - b[chart.key]);
    list.append(...sorted.map((run) => {
      const item = document.createElement('li');
      if (run[chart.key] === min) item.className = 'best';
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = runName(run);
      name.title = runName(run);
      const bar = document.createElement('span');
      bar.className = 'bar';
      const fill = document.createElement('span');
      fill.style.width = `${Math.max(2, (run[chart.key] / max) * 100)}%`;
      bar.append(fill);
      const value = document.createElement('span');
      value.className = 'value';
      value.textContent = chart.format(run[chart.key]);
      item.append(name, bar, value);
      return item;
    }));

    box.append(title, list);
    return box;
  }));
}

async function watchRun(run) {
  try {
    await post('/api/restart', {
      numberOfElevators: run.elevators,
      numberOfFloors: run.floors,
      maxPeople: run.maxPeople,
      averageCallsPerDay: run.callsPerDay,
      trafficPattern: run.pattern,
      numberOfSimulationDays: run.days,
      dayStartHour: run.startHour,
      randomSeed: run.seed,
      dispatchStrategy: run.strategy,
      idleParking: run.parking,
      timeToReachTheNextFloorMs: run.floorMs,
      doorTransitionTimeMs: run.doorMs,
      enterAndExitTimeMs: run.dwellMs,
      stairsPatienceSeconds: run.patienceS,
      dayDurationSeconds: state ? String(DAY_MS / 1000 / Math.min(state.clock.speed, 360)) : '1440',
    });
    ui.runsDialog.close();
  } catch (error) {
    ui.scenarioNote.textContent = error.message;
  }
}

function exportCsv() {
  if (!archive.length) return;
  const keys = Object.keys(archive[0]);
  const quote = (value) => {
    const text = String(value);
    return /[",\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
  };
  const lines = [keys.join(','), ...archive.map((run) => keys.map((key) => quote(run[key] ?? '')).join(','))];
  const blob = new Blob([lines.join('\n') + '\n'], { type: 'text/csv' });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = 'elevator-runs.csv';
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

$('runsButton').addEventListener('click', () => openRuns(null));
ui.scenarioFilter.addEventListener('change', renderRuns);
ui.showHandsOn.addEventListener('change', renderRuns);
$('runsExport').addEventListener('click', exportCsv);
$('runsClear').addEventListener('click', async () => {
  if (!window.confirm('Delete every saved result? This cannot be undone.')) return;
  await post('/api/runs/clear', {});
  openRuns(null);
});

loadConfig().catch(() => { ui.offline.hidden = false; });
poll();
requestAnimationFrame(frame);
