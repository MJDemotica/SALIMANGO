import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';

// English is the reference; tests/i18n.test.ts checks that Filipino has the
// same keys. Technical units and sensor names stay in English in both.
const en = {
  appName: 'SALIMANGO Pond Monitor',
  language: 'Language',
  loading: 'Loading…',
  cancel: 'Cancel',
  errorGeneric: 'Something went wrong: {msg}',
  setupMissing:
    'This site is not configured yet: VITE_SUPABASE_URL and VITE_SUPABASE_ANON_KEY are missing (see web/.env.example).',
  sessionError: 'Could not start a session: {msg}',

  accessTitle: 'Open your pond dashboard',
  accessIntro: 'Scan the QR code on the controller box, or type the code printed under it.',
  accessCodeLabel: 'Access code',
  accessOpen: 'Open dashboard',
  accessOpening: 'Opening…',
  accessInvalid: 'That code was not recognised. Check the sticker and try again.',
  accessSaved: 'Ponds on this phone',
  accessForget: 'Remove',
  accessRevoked: 'This phone no longer has access (the code was changed). Scan the new sticker.',
  switchPond: 'Switch pond',

  connLive: 'Live',
  connStale: 'No new data',
  connNone: 'Waiting for the device',
  lastUpdate: 'Last update {ago}',
  agoSeconds: '{n} s ago',
  agoMinutes: '{n} min ago',
  agoHours: '{n} h ago',
  agoDays: '{n} d ago',
  staleBanner:
    'No report from the device for {ago}. The values below are the last known readings, not live. Check the device’s power and Wi-Fi.',
  noDataBanner: 'The device has not reported yet. Values will appear after its first report.',
  simulatedBanner: 'SIMULATED DATA — these readings come from the test simulator, not from the pond sensors.',
  realtimeOff: 'Live updates paused; refreshing every 30 s.',

  tabWater: 'Water Quality Sensing',
  tabCal: 'Calibration',
  tabSettings: 'Settings & Data',

  statusOk: 'OK',
  statusDegraded: 'Approximate',
  statusOverRange: 'Over range',
  statusUnderRange: 'Below range',
  statusFault: 'Sensor fault',
  statusNotPresent: 'Not detected',
  statusUnavailable: 'Not available',
  levelGood: 'Within target',
  levelWarning: 'Outside target',
  levelCritical: 'Critical',
  target: 'Target {range}',

  cardPh: 'pH',
  cardSalinity: 'Salinity',
  cardTemp: 'Water temperature',
  cardDo: 'Dissolved oxygen',
  cardNh3: 'Ammonia toxicity (NH₃ fraction)',
  calDefault: 'Not calibrated',
  calCalibrated: 'Calibrated (v{v})',
  calInvalid: 'Stored calibration invalid — using defaults',
  calInProgress: 'Calibrating',
  tempFallbackNote: 'Temperature sensor fault: computed at 25 °C, treat as approximate.',
  salEc: 'Conductivity {ec} mS/cm at {t} °C · EC25 {ec25} mS/cm',
  salOverRange: 'Above the K = 1 probe’s range (about 11 ppt). The exact salinity cannot be measured.',
  salUnderRange: 'Below 2 ppt (outside the PSS-78 range).',
  salUnavailable: 'Needs a valid conductivity and water temperature.',
  salMethod: 'PSS-78 practical salinity from conductivity and water temperature',
  tempSecond: 'Second probe {t} °C',
  tempUsedFor: 'Used to compensate pH, conductivity/salinity and DO.',
  doEmergency: 'DO EMERGENCY: both air pumps forced ON',
  nh3Note:
    'Estimated share of total ammonia in the toxic NH₃ form (Emerson et al., 1975). This is not an ammonia concentration; that needs a TAN test.',
  nh3Unavailable: 'Needs valid pH and water temperature.',
  nh3OutOfDomain: 'pH or temperature is outside the formula’s range (pH 6–10, 0–40 °C).',

  modeTitle: 'Operating mode',
  modeAUTO: 'AUTO',
  modeMANUAL: 'MANUAL',
  modeOFF: 'OFF',
  modeAutoHelp: 'The device decides: low DO → air pumps, low salinity → bilge pumps.',
  modeManualHelp: 'You switch each pump. Returns to AUTO after 5 min without a command.',
  modeOffHelp: 'All pumps off. Air pumps still turn on in a DO emergency.',
  modeConfirmed: 'Device mode',
  modeRequested: 'Requested',
  modeLastKnown: 'last known',
  modeSelector: 'Selector on the box: {pos}',
  modeSelectorOverrides: 'The selector on the box is at {pos}, so it decides the mode. Turn it to A to control from here.',
  modeSelectorFault: 'Selector wiring fault (both contacts closed); the device treats it as A.',
  modeManualExpires: 'MANUAL ends in {time} unless you send a command.',
  confirmOffTitle: 'Turn everything OFF?',
  confirmOffBody:
    'All pumps stop. Air pumps still turn on automatically if DO falls below 3 mg/L. Use OFF only for servicing, never as a normal setting while crabs are stocked.',
  confirmOffYes: 'Yes, turn OFF',

  cmdPending: 'Sending…',
  cmdDelivered: 'Waiting for the device…',
  cmdAccepted: 'Done',
  cmdRejected: 'Refused by the device: {msg}',
  cmdExpired: 'Not delivered: {msg}',
  cmdTimedOut: 'No answer from the device',
  cmdSuperseded: 'Replaced by a newer request',
  cmdSendFailed: 'Could not send: {msg}',

  pumpsTitle: 'Pumps',
  relayK1: 'Air pump 1 (K1)',
  relayK5: 'Air pump 2 (K5)',
  relayK3: 'Bilge pump 1 (K3)',
  relayK4: 'Bilge pump 2 (K4)',
  pumpOn: 'ON',
  pumpOff: 'OFF',
  pumpTurnOn: 'Turn on',
  pumpTurnOff: 'Turn off',
  pumpsManualOnly: 'Switch to MANUAL to control pumps one by one.',
  pumpsEmergencyLock: 'Air pumps cannot be turned off during a DO emergency.',
  bilgeTitle: 'Salinity correction',
  bilgeIDLE: 'Ready',
  bilgeRUNNING: 'Bilge pumps running',
  bilgeRESTING: 'Resting between runs',
  bilgeLOCKOUT: 'Locked out',
  bilgeLockoutBody:
    'Salinity did not recover after {n} tries. The bilge pumps will not run until salinity recovers or you clear the lockout.',
  bilgeClear: 'Clear lockout',

  eventsTitle: 'Recent events',
  eventsEmpty: 'No events yet.',

  historyTitle: 'History',
  range24h: '24 hours',
  range7d: '7 days',
  historyEmpty: 'No readings in this period.',
  historyNh3Unit: '% NH₃',

  calEcTitle: 'Salinity / electrical conductivity (EC)',
  calIntro:
    'Calibration runs on the device. Start, put the probe in the solution, wait until it is stable, capture, then save. Values are stored on the ESP32 and recorded here.',
  calStatus: 'Status',
  calNotCalibrated: 'Not calibrated — using factory defaults',
  calCalibratedVersion: 'Calibrated · version {v}',
  calRecordedAt: 'Recorded {date}',
  calNoRecord: 'Date unknown (saved before cloud logging)',
  calActiveValues: 'Active values',
  calStart: 'Start',
  calCapture: 'Capture',
  calSave: 'Save',
  calCancel: 'Cancel',
  calStable: 'Stable — ready to capture',
  calSettling: 'Still settling…',
  calCaptured: 'Captured',
  calHistory: 'Previous calibrations',
  calNoHistory: 'No calibrations recorded yet.',
  calNeedsLive: 'The device must be online to calibrate.',
  calTempNote:
    'Water temperature (DS18B20) has no calibration. Check it against a reference thermometer as described in docs/CALIBRATION.md.',
  phSteps:
    'Buffers 4.01, 6.86 and 9.18 are recognised automatically and corrected for temperature. One buffer corrects the offset only; two or three also correct the slope.',
  phPh7: 'Voltage at pH 7',
  phSlope: 'Slope',
  phCalTemp: 'Calibration temperature',
  phDivider: 'Divider ratio',
  phPoints: 'Buffers used',
  ecSteps: 'Standards 1.413 mS/cm and 12.88 mS/cm. 12.88 mS/cm is a conductivity standard, not seawater.',
  ecK: 'Cell constant K',
  ecCorrLow: 'Correction factor (low range)',
  ecCorrHigh: 'Correction factor (high range)',
  ecTempComp: 'Temperature compensation: 1.85 %/°C to 25 °C',
  ecStandards: 'Standards used',
  ecRangeNote:
    'K = 1 probe: reliable up to about 18 mS/cm (≈ 11 ppt at 25 °C). Conductivity calibration is not salinity validation: cross-check salinity with the refractometer.',
  ecPending: 'Pending (low / high)',
  doSteps:
    'SEN0237-A: saturation calibration with the wet probe in air or in well-aerated water. One point, or two points at least 5 °C apart. AUTO aeration is held while calibrating.',
  doDo9100:
    'The DO9100 handheld meter is a separate reference instrument with its own calibration (see its manual). Use it only to cross-check this probe; it does not change these values.',
  doPoints: 'Calibration points',
  doPoint: '{mv} mV at {t} °C',
  nh3CalTitle: 'Ammonia toxicity — calculation',
  nh3CalBody: 'Nothing to calibrate: the value is calculated on the device from the calibrated pH and the water temperature.',
  nh3CalFormula: 'pKa = 0.09018 + 2729.92 / T(K), T(K) = °C + 273.2;  NH₃ fraction = 1 / (10^(pKa − pH) + 1)',
  nh3CalSource:
    'Emerson, K., Russo, R. C., Lund, R. E. & Thurston, R. V. (1975). Aqueous ammonia equilibrium calculations: effect of pH and temperature. J. Fish. Res. Board Can. 32: 2379–2383.',
  nh3CalLimits:
    'Derived for fresh water. In brackish water the true NH₃ fraction is somewhat lower, so this estimate errs on the safe side. It is a fraction, not a concentration; it is shown only when pH is 6–10 and temperature 0–40 °C.',

  settingsTitle: 'Salinity correction settings',
  settingsIntro:
    'In AUTO, the bilge pumps run when salinity stays below salMin and stop at salMin + salHyst, after the maximum run time, or after the set number of failed tries.',
  setSalMin: 'salMin — bilge turns on below (ppt)',
  setSalMax: 'salMax — high-salinity warning (ppt)',
  setSalHyst: 'salHyst — recovery margin (ppt)',
  setMaxRun: 'Maximum bilge run (min)',
  setMinOff: 'Rest between runs (min)',
  setMaxFails: 'Tries before lockout',
  setRangeNote: 'The K = 1 probe cannot measure above about 11 ppt; readings above that count as “not low”.',
  setSave: 'Save settings',
  setSaving: 'Saving…',
  setSaved: 'Saved (version {v}). The device applies it on its next report.',
  setApplied: 'The device is using version {v}.',
  setPendingApply: 'The device is still on version {a}; version {b} applies on its next report.',
  setErrRange: '{field} must be between {min} and {max}.',
  setErrOrder: 'salMax must be higher than salMin.',
  retentionTitle: 'History storage',
  retentionKeep: 'Keep all history',
  retentionDelete: 'Delete history older than',
  retentionDays: 'days',
  retentionWarn: 'Deleted history cannot be recovered. Download the CSV first.',
  exportTitle: 'Download data',
  exportReadings: 'Download all readings (CSV)',
  exportEvents: 'Download all events (CSV)',
  exportProgress: 'Preparing… {n} rows',
  exportDone: 'Downloaded {n} rows.',
  exportEmpty: 'No data to download yet.',
  deviceTitle: 'Device',
  deviceFirmware: 'Firmware {v}',
  deviceWifi: 'Wi-Fi signal {rssi} dBm',
  deviceUptime: 'Running for {time}',
} as const;

export type MessageKey = keyof typeof en;

const fil: Record<MessageKey, string> = {
  appName: 'SALIMANGO Pond Monitor',
  language: 'Wika',
  loading: 'Naglo-load…',
  cancel: 'Kanselahin',
  errorGeneric: 'May nagkaproblema: {msg}',
  setupMissing:
    'Hindi pa naka-configure ang site: kulang ang VITE_SUPABASE_URL at VITE_SUPABASE_ANON_KEY (tingnan ang web/.env.example).',
  sessionError: 'Hindi masimulan ang session: {msg}',

  accessTitle: 'Buksan ang dashboard ng palaisdaan',
  accessIntro: 'I-scan ang QR code sa controller box, o i-type ang code na nakasulat sa ilalim nito.',
  accessCodeLabel: 'Access code',
  accessOpen: 'Buksan ang dashboard',
  accessOpening: 'Binubuksan…',
  accessInvalid: 'Hindi nakilala ang code. Tingnan muli ang sticker at subukan ulit.',
  accessSaved: 'Mga palaisdaan sa phone na ito',
  accessForget: 'Alisin',
  accessRevoked: 'Wala nang access ang phone na ito (pinalitan ang code). I-scan ang bagong sticker.',
  switchPond: 'Lumipat ng palaisdaan',

  connLive: 'Live',
  connStale: 'Walang bagong data',
  connNone: 'Hinihintay ang device',
  lastUpdate: 'Huling update {ago}',
  agoSeconds: '{n} s ang nakalipas',
  agoMinutes: '{n} min ang nakalipas',
  agoHours: '{n} oras ang nakalipas',
  agoDays: '{n} araw ang nakalipas',
  staleBanner:
    'Walang ulat mula sa device sa loob ng {ago}. Ang mga value sa ibaba ay huling alam na reading, hindi live. Tingnan ang kuryente at Wi-Fi ng device.',
  noDataBanner: 'Hindi pa nag-uulat ang device. Lalabas ang mga value pagkatapos ng unang ulat nito.',
  simulatedBanner: 'SIMULATED NA DATA — galing ang mga reading na ito sa test simulator, hindi sa mga sensor sa palaisdaan.',
  realtimeOff: 'Naka-pause ang live update; nagre-refresh tuwing 30 s.',

  tabWater: 'Kalidad ng Tubig',
  tabCal: 'Calibration',
  tabSettings: 'Settings at Data',

  statusOk: 'OK',
  statusDegraded: 'Tantiya lang',
  statusOverRange: 'Lampas sa range',
  statusUnderRange: 'Kulang sa range',
  statusFault: 'Sira ang sensor',
  statusNotPresent: 'Hindi makita',
  statusUnavailable: 'Hindi available',
  levelGood: 'Pasok sa target',
  levelWarning: 'Labas sa target',
  levelCritical: 'Delikado',
  target: 'Target {range}',

  cardPh: 'pH',
  cardSalinity: 'Alat (salinity)',
  cardTemp: 'Temperatura ng tubig',
  cardDo: 'Dissolved oxygen',
  cardNh3: 'Lason ng ammonia (NH₃ fraction)',
  calDefault: 'Hindi pa naka-calibrate',
  calCalibrated: 'Naka-calibrate (v{v})',
  calInvalid: 'Mali ang naka-save na calibration — default ang gamit',
  calInProgress: 'Nagca-calibrate',
  tempFallbackNote: 'Sira ang temperature sensor: kinompute sa 25 °C, tantiya lang ito.',
  salEc: 'Conductivity {ec} mS/cm sa {t} °C · EC25 {ec25} mS/cm',
  salOverRange: 'Lampas sa kaya ng K = 1 probe (mga 11 ppt). Hindi masukat ang eksaktong alat.',
  salUnderRange: 'Mas mababa sa 2 ppt (labas sa range ng PSS-78).',
  salUnavailable: 'Kailangan ng tamang conductivity at temperatura ng tubig.',
  salMethod: 'PSS-78 practical salinity mula sa conductivity at temperatura ng tubig',
  tempSecond: 'Pangalawang probe {t} °C',
  tempUsedFor: 'Ginagamit para i-compensate ang pH, conductivity/alat at DO.',
  doEmergency: 'DO EMERGENCY: sapilitang naka-ON ang dalawang air pump',
  nh3Note:
    'Tantiyang bahagi ng kabuuang ammonia na nasa nakalalasong anyong NH₃ (Emerson et al., 1975). Hindi ito dami ng ammonia; kailangan ng TAN test para doon.',
  nh3Unavailable: 'Kailangan ng tamang pH at temperatura ng tubig.',
  nh3OutOfDomain: 'Labas sa range ng formula ang pH o temperatura (pH 6–10, 0–40 °C).',

  modeTitle: 'Operating mode',
  modeAUTO: 'AUTO',
  modeMANUAL: 'MANUAL',
  modeOFF: 'OFF',
  modeAutoHelp: 'Ang device ang magpapasya: mababang DO → air pump, mababang alat → bilge pump.',
  modeManualHelp: 'Ikaw ang magbubukas/magpapatay ng bawat pump. Babalik sa AUTO kapag 5 min walang utos.',
  modeOffHelp: 'Patay lahat ng pump. Bubukas pa rin ang air pump kapag DO emergency.',
  modeConfirmed: 'Mode ng device',
  modeRequested: 'Hiniling',
  modeLastKnown: 'huling alam',
  modeSelector: 'Selector sa box: {pos}',
  modeSelectorOverrides:
    'Nasa {pos} ang selector sa box, kaya ito ang nagpapasya ng mode. Ilipat ito sa A para makontrol mula rito.',
  modeSelectorFault: 'May sira sa wiring ng selector (parehong sarado); itinuturing ito ng device na A.',
  modeManualExpires: 'Matatapos ang MANUAL sa loob ng {time} kung walang bagong utos.',
  confirmOffTitle: 'Patayin lahat?',
  confirmOffBody:
    'Titigil lahat ng pump. Kusang bubukas pa rin ang air pump kapag bumaba ang DO sa 3 mg/L. Gamitin lang ang OFF kapag may inaayos, hindi bilang karaniwang setting habang may alimango.',
  confirmOffYes: 'Oo, i-OFF',

  cmdPending: 'Ipinapadala…',
  cmdDelivered: 'Hinihintay ang device…',
  cmdAccepted: 'Tapos na',
  cmdRejected: 'Tinanggihan ng device: {msg}',
  cmdExpired: 'Hindi naipadala: {msg}',
  cmdTimedOut: 'Walang sagot mula sa device',
  cmdSuperseded: 'Pinalitan ng mas bagong utos',
  cmdSendFailed: 'Hindi maipadala: {msg}',

  pumpsTitle: 'Mga pump',
  relayK1: 'Air pump 1 (K1)',
  relayK5: 'Air pump 2 (K5)',
  relayK3: 'Bilge pump 1 (K3)',
  relayK4: 'Bilge pump 2 (K4)',
  pumpOn: 'ON',
  pumpOff: 'OFF',
  pumpTurnOn: 'Buksan',
  pumpTurnOff: 'Patayin',
  pumpsManualOnly: 'Ilipat sa MANUAL para makontrol isa-isa ang mga pump.',
  pumpsEmergencyLock: 'Hindi mapapatay ang air pump habang DO emergency.',
  bilgeTitle: 'Pagwawasto ng alat',
  bilgeIDLE: 'Handa',
  bilgeRUNNING: 'Tumatakbo ang bilge pump',
  bilgeRESTING: 'Nagpapahinga sa pagitan ng takbo',
  bilgeLOCKOUT: 'Naka-lockout',
  bilgeLockoutBody:
    'Hindi bumalik ang alat pagkatapos ng {n} subok. Hindi tatakbo ang bilge pump hangga’t hindi bumabalik ang alat o hindi mo kina-clear ang lockout.',
  bilgeClear: 'I-clear ang lockout',

  eventsTitle: 'Mga kamakailang pangyayari',
  eventsEmpty: 'Wala pang pangyayari.',

  historyTitle: 'Kasaysayan',
  range24h: '24 oras',
  range7d: '7 araw',
  historyEmpty: 'Walang reading sa panahong ito.',
  historyNh3Unit: '% NH₃',

  calEcTitle: 'Alat / electrical conductivity (EC)',
  calIntro:
    'Sa device mismo tumatakbo ang calibration. Pindutin ang Simulan, ilagay ang probe sa solution, hintaying maging stable, i-capture, saka i-save. Naka-save ang mga value sa ESP32 at nakatala rito.',
  calStatus: 'Status',
  calNotCalibrated: 'Hindi pa naka-calibrate — factory default ang gamit',
  calCalibratedVersion: 'Naka-calibrate · bersyon {v}',
  calRecordedAt: 'Naitala {date}',
  calNoRecord: 'Hindi alam ang petsa (na-save bago ang cloud logging)',
  calActiveValues: 'Kasalukuyang mga value',
  calStart: 'Simulan',
  calCapture: 'I-capture',
  calSave: 'I-save',
  calCancel: 'Kanselahin',
  calStable: 'Stable na — puwede nang i-capture',
  calSettling: 'Hindi pa stable…',
  calCaptured: 'Na-capture',
  calHistory: 'Mga nakaraang calibration',
  calNoHistory: 'Wala pang naitalang calibration.',
  calNeedsLive: 'Kailangang online ang device para mag-calibrate.',
  calTempNote:
    'Walang calibration ang temperatura ng tubig (DS18B20). Ihambing ito sa reference thermometer ayon sa docs/CALIBRATION.md.',
  phSteps:
    'Awtomatikong nakikilala ang buffer 4.01, 6.86 at 9.18 at kino-correct para sa temperatura. Offset lang ang naitatama ng isang buffer; pati slope kapag dalawa o tatlo.',
  phPh7: 'Boltahe sa pH 7',
  phSlope: 'Slope',
  phCalTemp: 'Temperatura sa calibration',
  phDivider: 'Divider ratio',
  phPoints: 'Mga buffer na ginamit',
  ecSteps: 'Standard 1.413 mS/cm at 12.88 mS/cm. Ang 12.88 mS/cm ay conductivity standard, hindi tubig-dagat.',
  ecK: 'Cell constant K',
  ecCorrLow: 'Correction factor (mababang range)',
  ecCorrHigh: 'Correction factor (mataas na range)',
  ecTempComp: 'Temperature compensation: 1.85 %/°C papuntang 25 °C',
  ecStandards: 'Mga standard na ginamit',
  ecRangeNote:
    'K = 1 probe: maaasahan hanggang mga 18 mS/cm (≈ 11 ppt sa 25 °C). Hindi pa salinity validation ang conductivity calibration: ihambing ang alat sa refractometer.',
  ecPending: 'Nakabinbin (mababa / mataas)',
  doSteps:
    'SEN0237-A: saturation calibration na basa ang probe sa hangin o sa tubig na maraming hangin. Isang punto, o dalawang punto na may hindi bababa sa 5 °C na pagitan. Naka-hold ang AUTO aeration habang nagca-calibrate.',
  doDo9100:
    'Ang DO9100 handheld meter ay hiwalay na reference instrument na may sariling calibration (tingnan ang manual nito). Gamitin lang ito para ihambing ang probe na ito; hindi nito binabago ang mga value na ito.',
  doPoints: 'Mga calibration point',
  doPoint: '{mv} mV sa {t} °C',
  nh3CalTitle: 'Lason ng ammonia — kalkulasyon',
  nh3CalBody:
    'Walang kailangang i-calibrate: kinakalkula ito ng device mula sa naka-calibrate na pH at temperatura ng tubig.',
  nh3CalFormula: 'pKa = 0.09018 + 2729.92 / T(K), T(K) = °C + 273.2;  NH₃ fraction = 1 / (10^(pKa − pH) + 1)',
  nh3CalSource:
    'Emerson, K., Russo, R. C., Lund, R. E. & Thurston, R. V. (1975). Aqueous ammonia equilibrium calculations: effect of pH and temperature. J. Fish. Res. Board Can. 32: 2379–2383.',
  nh3CalLimits:
    'Ginawa para sa tubig-tabang. Sa maalat-alat na tubig, medyo mas mababa ang tunay na NH₃ fraction, kaya nasa ligtas na panig ang tantiyang ito. Fraction ito, hindi dami; ipinapakita lang kapag pH 6–10 at temperatura 0–40 °C.',

  settingsTitle: 'Settings ng pagwawasto ng alat',
  settingsIntro:
    'Sa AUTO, tatakbo ang bilge pump kapag nanatiling mas mababa sa salMin ang alat, at titigil sa salMin + salHyst, pagkatapos ng maximum na takbo, o pagkatapos ng itinakdang bilang ng bigong subok.',
  setSalMin: 'salMin — bubukas ang bilge kapag mas mababa (ppt)',
  setSalMax: 'salMax — babala sa sobrang alat (ppt)',
  setSalHyst: 'salHyst — palugit bago ituring na bumalik (ppt)',
  setMaxRun: 'Pinakamahabang takbo ng bilge (min)',
  setMinOff: 'Pahinga sa pagitan ng takbo (min)',
  setMaxFails: 'Bilang ng subok bago mag-lockout',
  setRangeNote: 'Hindi kayang sukatin ng K = 1 probe ang lampas mga 11 ppt; itinuturing na “hindi mababa” ang reading na lampas doon.',
  setSave: 'I-save ang settings',
  setSaving: 'Sine-save…',
  setSaved: 'Na-save (bersyon {v}). Ia-apply ng device sa susunod nitong ulat.',
  setApplied: 'Bersyon {v} ang gamit ng device.',
  setPendingApply: 'Nasa bersyon {a} pa ang device; ia-apply ang bersyon {b} sa susunod nitong ulat.',
  setErrRange: 'Ang {field} ay dapat nasa pagitan ng {min} at {max}.',
  setErrOrder: 'Dapat mas mataas ang salMax kaysa salMin.',
  retentionTitle: 'Pag-iimbak ng kasaysayan',
  retentionKeep: 'Itago lahat ng kasaysayan',
  retentionDelete: 'Burahin ang kasaysayang mas luma sa',
  retentionDays: 'araw',
  retentionWarn: 'Hindi na maibabalik ang nabura. I-download muna ang CSV.',
  exportTitle: 'I-download ang data',
  exportReadings: 'I-download lahat ng reading (CSV)',
  exportEvents: 'I-download lahat ng pangyayari (CSV)',
  exportProgress: 'Inihahanda… {n} row',
  exportDone: 'Na-download ang {n} row.',
  exportEmpty: 'Wala pang data na mada-download.',
  deviceTitle: 'Device',
  deviceFirmware: 'Firmware {v}',
  deviceWifi: 'Lakas ng Wi-Fi {rssi} dBm',
  deviceUptime: 'Tumatakbo nang {time}',
};

export const dictionaries = { en, fil } as const;
export type Lang = keyof typeof dictionaries;
const STORAGE_KEY = 'salimango.lang';

export type Translate = (key: MessageKey, vars?: Record<string, string | number>) => string;

export function translate(lang: Lang, key: MessageKey, vars?: Record<string, string | number>): string {
  const template: string = dictionaries[lang][key] ?? en[key];
  return vars ? template.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? String(vars[k]) : m)) : template;
}

interface I18nValue {
  lang: Lang;
  setLang: (l: Lang) => void;
  t: Translate;
}

const I18nContext = createContext<I18nValue>({
  lang: 'en',
  setLang: () => {},
  t: (k, v) => translate('en', k, v),
});

function initialLang(): Lang {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved === 'en' || saved === 'fil') return saved;
  } catch {
    /* storage unavailable */
  }
  return typeof navigator !== 'undefined' && /^(fil|tl)/i.test(navigator.language) ? 'fil' : 'en';
}

export function I18nProvider({ children, lang: forced }: { children: ReactNode; lang?: Lang }) {
  const [lang, setLangState] = useState<Lang>(forced ?? initialLang);
  const setLang = useCallback((l: Lang) => {
    setLangState(l);
    try {
      localStorage.setItem(STORAGE_KEY, l);
    } catch {
      /* ignore */
    }
    document.documentElement.lang = l === 'fil' ? 'fil' : 'en';
  }, []);
  const value = useMemo<I18nValue>(() => ({ lang, setLang, t: (k, v) => translate(lang, k, v) }), [lang, setLang]);
  return <I18nContext.Provider value={value}>{children}</I18nContext.Provider>;
}

export function useI18n(): I18nValue {
  return useContext(I18nContext);
}

/** "12 s ago", "3 min ago", … */
export function formatAgo(t: Translate, seconds: number): string {
  if (!Number.isFinite(seconds)) return '—';
  if (seconds < 90) return t('agoSeconds', { n: Math.round(seconds) });
  if (seconds < 5400) return t('agoMinutes', { n: Math.round(seconds / 60) });
  if (seconds < 172800) return t('agoHours', { n: Math.round(seconds / 3600) });
  return t('agoDays', { n: Math.round(seconds / 86400) });
}

export function formatDuration(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  if (d) return `${d} d ${h} h`;
  if (h) return `${h} h ${m} min`;
  if (m) return `${m} min ${s % 60} s`;
  return `${s} s`;
}
