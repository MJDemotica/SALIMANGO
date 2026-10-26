# MASTER PROMPT — S.A.L.I.M.A.N.G.O. / CRAB GRID SMART AERATION

**Repository Audit, Architecture Revision, Firmware Integration, Calibration, and Hardware Pinout Verification**

## 1. ROLE AND OBJECTIVE

Act as a senior full-stack software engineer, embedded systems engineer, IoT architect, and technical code reviewer.

Your task is to inspect, revise, and implement the existing `crab-grid-smart-aeration` repository according to this specification.

The project is an ESP32-based water-quality monitoring and smart aeration system intended for mud crab aquaculture in the Philippines.

**Work directly in the existing repository.** Do not create a disconnected demonstration, blindly replace the existing project with a generic template, or merely provide recommendations and isolated code snippets.

Prioritize:

1. Scientific and engineering correctness.
2. Hardware and firmware compatibility.
3. Accurate pin assignments and physical interface assumptions.
4. Functional water-quality monitoring.
5. Correct calibration and persistent configuration.
6. Safe actuator control.
7. Secure and reliable Supabase integration.
8. Removal of redundant and obsolete code.
9. Accurate documentation and verifiable test results.

Never fabricate sensor readings, calibration coefficients, pin assignments, hardware capabilities, test results, or completed integrations.

When a requirement cannot be verified or safely implemented, identify the limitation and implement an appropriate fallback without claiming the feature is complete.

## 2. REQUIRED WORKFLOW

### Phase A — Audit the repository and hardware reference

Before modifying files:

1. Inspect the repository structure, application entry points, firmware, dependencies, environment configuration, database code, calibration implementations, and documentation.
2. Locate and inspect `perfboard_build_guide.pdf`.
3. Determine whether the PDF is accessible, complete, and sufficiently clear to establish the intended project pin assignments.
4. Inspect the relevant existing files, including:
   - [`README.md`](http://README.md)
   - `docs/[APP.md](http://APP.md)`
   - `docs/[CALIBRATION.md](http://CALIBRATION.md)`
   - `docs/[FIRMWARE.md](http://FIRMWARE.md)`
   - `docs/[SETUP.md](http://SETUP.md)`
   - `docs/[STACKS.md](http://STACKS.md)`
   - `docs/[SYSTEM-ARCHITECTURE.md](http://SYSTEM-ARCHITECTURE.md)`
   - `docs/[BLOCK-DIAGRAM.md](http://BLOCK-DIAGRAM.md)`
   - `docs/[WIRING.md](http://WIRING.md)`
   - `docs/[Components.md](http://Components.md)`
   - `docs/[TESTING.md](http://TESTING.md)`
   - Existing ESP32 source files, sensor drivers, calibration code, database schemas, UI components, and configuration files.
5. Trace the complete data flow from sensors to ESP32, Supabase, dashboard, calibration storage, and actuator controls.
6. Identify conflicting implementations, duplicated formulas, outdated dependencies, incompatible architectural assumptions, and inconsistencies between the PDF, firmware, and documentation.

Treat the existing code as evidence of the current implementation, not proof that the implementation is correct.

Use `perfboard_build_guide.pdf` as the **primary project-specific reference for intended physical pin assignments and wiring** unless explicit, verified evidence establishes a newer hardware revision.

If the PDF is missing or inaccessible, do not invent its contents. Report the limitation and use only other verifiable project evidence.

### Phase B — Plan the changes

Briefly summarize:

- The current architecture.
- The target architecture.
- Hardware and firmware discrepancies.
- Files likely to require changes.
- Blocking uncertainties that genuinely require human input.

Do not stop at planning. Continue with implementation unless a critical unresolved hardware decision prevents safe progress.

Do not ask questions that can be answered through repository inspection.

### Phase C — Implement and verify

1. Revise the frontend and backend.
2. Integrate ESP32 telemetry and control.
3. Integrate existing calibration implementations.
4. Implement the five required sensing indicators.
5. Update database schemas and configuration.
6. Align firmware pin assignments with the hardware guide.
7. Remove obsolete Firebase, camera, and active LoRa functionality.
8. Update documentation, diagrams, and the change log.
9. Run available builds, linting, type checks, and tests.
10. Report remaining hardware-dependent and unverified items honestly.

## 3. REQUIRED TECHNOLOGY STACK

### 3.1 Frontend — React.js

Use React.js as the active frontend framework.

- Preserve the existing React setup if suitable.
- If the repository uses Next.js, migrate to a conventional React application, preferably Vite + React, unless a verified architectural dependency makes migration impractical.
- Preserve the existing language where reasonable; prefer TypeScript for new modules when practical.
- Reuse appropriate components and styling.
- Use reusable components, hooks, and centralized utilities.
- Ensure responsive behavior on desktop, tablet, and mobile browsers.
- Avoid unnecessary dependencies and duplicate implementations.

### 3.2 Backend — Supabase

Replace Firebase with Supabase.

Use Supabase and PostgreSQL for:

- Device identity and configuration.
- Sensor telemetry and history.
- Persistent calibration settings.
- Current operating mode and actuator state.
- Control commands and acknowledgements.
- Alerts and device events.
- Realtime dashboard updates.

Remove Firebase-specific packages, configuration, listeners, schemas, and documentation after replacing their functionality.

Do not retain Firebase and Supabase as competing active backends.

Use environment variables for configuration. Never expose privileged database credentials, service-role keys, or unrestricted access tokens in frontend code or ESP32 firmware.

Implement appropriate Row Level Security (RLS) policies and restricted device-write mechanisms.

### 3.3 Firmware — ESP32

Preserve the existing firmware environment, such as Arduino IDE or PlatformIO, where practical.

Retain working sensor drivers, verified hardware connections, and valid calibration algorithms. Modify them only when required by the revised architecture or when a specific defect is identified.

All pin assignments must be reconciled with `perfboard_build_guide.pdf`.

## 4. MANDATORY ARCHITECTURE CHANGES

### 4.1 Replace Firebase with Supabase

- Replace Firebase Realtime Database with an appropriate Supabase/PostgreSQL implementation.
- Migrate the existing data model.
- Implement suitable Realtime subscriptions.
- Update environment configuration and setup instructions.
- Remove obsolete Firebase code and dependencies.
- Document telemetry, calibration, and control data flows.

### 4.2 Remove all camera functionality

Camera functionality was not pursued.

Remove:

- ESP32-CAM and OV2640 support.
- Camera firmware and drivers.
- Video streaming and image capture.
- Camera-specific components and routes.
- Camera database fields and dependencies.
- Camera-related power requirements and BOM entries.
- Camera setup steps and references in active diagrams.

Do not leave dormant or partially functional camera features.

### 4.3 Remove LoRa from the current phase

LoRa is not available for the current implementation phase.

Remove active dependencies on:

- EBYTE E22 modules and radio drivers.
- LoRa packet encoding and decoding.
- Radio configuration and telemetry.
- RSSI/SNR monitoring.
- Radio-specific acknowledgements and retransmission logic.
- A separate LoRa house-bridge node.
- LoRa-specific firmware and dependencies.

The current architecture is:

**ESP32 sensors and actuators → Wi-Fi → Supabase → React dashboard.**

The ESP32 must communicate directly over Wi-Fi without requiring a second ESP32 bridge to relay telemetry.

Keep the software modular enough to accommodate LoRa in a future phase, but do not implement it now. Document LoRa as deferred.

### 4.4 Replace the previous website access method

There must be no conventional username-and-password login page.

Support these access methods:

1. A QR code printed on a sticker attached to the house-node enclosure.
2. A short, human-readable code that can be typed into the website.

Both methods should lead to the intended dashboard access flow.

Requirements:

- The QR code opens the intended dashboard URL.
- The typable code resolves to the correct device or dashboard.
- The interface is mobile-friendly.
- Do not embed privileged credentials in the QR code or access code.
- Do not treat a public device ID as a secret.
- Protect control operations with a suitable authorization mechanism.
- Provide a way to revoke or replace compromised access where practical.
- Document the security limitations of a shared physical sticker.

If the existing repository does not establish the provisioning method, implement the simplest secure architecture consistent with the no-login-page requirement and document remaining assumptions.

### 4.5 Configure the DFRobot Analog EC probe with K = 1

The project team has confirmed that the DFRobot Analog EC probe uses a cell constant of **K = 1**.

This is mandatory for the current implementation.

- Use K = 1 consistently for the applicable EC probe.
- Inspect firmware, configuration files, calibration functions, formulas, and documentation for conflicting values.
- Correct conflicting values that apply to this probe.
- Preserve the existing DFRobot-compatible calibration algorithm and temperature-compensation method.
- Distinguish the cell constant from software gain, calibration coefficients, and other correction factors.
- Avoid applying the cell-constant correction twice.
- Do not interpret K = 1 as a conductivity-to-salinity conversion factor.
- Update tests, documentation, and the change log.
- Verify the active EC measurement pipeline uses the intended configuration.

Do not claim that the physical sensor has been calibrated or validated merely because the software uses K = 1.

### 4.6 Use `perfboard_build_guide.pdf` for hardware pin assignments

Use `perfboard_build_guide.pdf` as the primary project-specific reference for the intended ESP32 GPIO assignments and physical wiring.

Before changing pin definitions:

1. Extract the documented connections from the PDF.
2. Compare them with the active firmware and existing pin configuration.
3. Identify every discrepancy between the guide, code, and wiring documentation.
4. Resolve conflicts using verified evidence.
5. If the conflict cannot be resolved confidently, flag it for human confirmation rather than guessing.

The guide must inform applicable assignments for:

- pH sensor.
- DFRobot Analog EC sensor.
- Temperature sensor.
- Dissolved oxygen sensor.
- Aeration and other supported actuators.
- I²C, SPI, UART, ADC, and digital interfaces where applicable.

Verify that selected GPIOs are suitable for their intended functions on the actual ESP32 board.

Do not assume that a pin is valid simply because the firmware compiles.

Keep the firmware pin configuration centralized where practical. Avoid unrelated hardcoded pin values in separate sensor drivers.

Synchronize firmware and documentation with the guide, including:

- `docs/[WIRING.md](http://WIRING.md)`
- `docs/[BLOCK-DIAGRAM.md](http://BLOCK-DIAGRAM.md)`
- `docs/[SYSTEM-ARCHITECTURE.md](http://SYSTEM-ARCHITECTURE.md)`
- `docs/[Components.md](http://Components.md)`
- `docs/[FIRMWARE.md](http://FIRMWARE.md)`
- Relevant pin maps and setup instructions.

If the guide conflicts with verified evidence of a newer hardware revision, document the conflict and use the confirmed current hardware design.

Clearly distinguish software/documentation consistency from physical verification of the actual perfboard.

## 5. MAIN TAB — WATER QUALITY SENSING

Create the primary dashboard tab named **Water Quality Sensing**.

Display five required measurements or indicators.

### 5.1 pH

Display:

- Current calibrated pH value.
- Sensor status.
- Last successful update.
- Calibration status.
- Warnings for invalid, stale, or configured out-of-range readings.

Preserve the existing pH sensor driver and calibration algorithm where valid.

### 5.2 Salinity

Display:

- Derived salinity in ppt.
- Conductivity in the appropriate unit, if available.
- Water temperature used for compensation, when applicable.
- Sensor status.
- Last successful update.
- Calibration status.

The DFRobot Analog EC configuration must use K = 1.

Do not assume conductivity in mS/cm is numerically equivalent to salinity in ppt.

Preserve the established conductivity conversion and temperature-compensation methods. Use a documented, appropriate conductivity-to-salinity conversion rather than an arbitrary multiplier.

Distinguish conductivity calibration from salinity validation.

### 5.3 Temperature

Display:

- Current water temperature in °C.
- Sensor status.
- Last successful update.
- Other temperature readings only if supported by the existing hardware.

Temperature must not have a separate calibration tab.

Do not invent a temperature calibration procedure.

If temperature compensation is used by EC or another sensor, make that dependency explicit.

### 5.4 Ammonia toxicity risk — Emerson et al. (1975)

Implement the project's intended ammonia toxicity calculation based on the verified Emerson et al. (1975) method.

Requirements:

- Inspect the existing formula and its source before modifying it.
- Verify the calculation, units, and assumptions.
- Use the required inputs, including pH and temperature.
- Centralize the calculation in one reusable module.
- Avoid conflicting formula copies in firmware and frontend.
- Recalculate when valid required inputs change.
- Handle missing, invalid, or stale inputs explicitly.
- Display the unionized ammonia fraction as a fraction or percentage according to the verified implementation.
- Clearly label it as an estimated NH₃ fraction or toxicity indicator.
- Do not claim to directly measure ammonia concentration.

The system does not have a confirmed direct total ammonia nitrogen (TAN) measurement in the current design. Do not invent TAN readings or display absolute ammonia concentration in ppm or mg/L without the necessary measured input and verified calculation.

If conflicting formula implementations exist, investigate and document the discrepancy instead of silently choosing one.

### 5.5 Dissolved oxygen

Display:

- Current DO reading in mg/L or ppm, consistent with the sensor output.
- Sensor status.
- Last successful update.
- Calibration status.
- Appropriate configured warnings for low or invalid readings.

Inspect the actual DO sensor driver and instrument documentation.

The project references the DFRobot SEN0237-A sensor and a DO9100 handheld reference meter. Determine which calibration procedures belong to the installed sensor and which apply only to the reference instrument.

Do not assume the two devices share the same calibration procedure.

### 5.6 Dashboard behavior

All cards must update automatically as new valid telemetry arrives.

Provide:

- Consistent units and precision.
- Last-updated timestamps.
- Sensor and connection status.
- Loading, disconnected, stale, and error states.
- Clear warning and critical indicators.
- A distinction between a legitimate zero and a failed sensor.
- A clear distinction between real and simulated data.

Never present simulated readings as live hardware measurements.

## 6. OPERATING MODE — AUTO / MANUAL / OFF

Add a prominent operating-mode selector with exactly three modes:

- AUTO
- MANUAL
- OFF

Display both the requested mode and confirmed device mode when they differ.

### AUTO

- Execute existing verified automatic-control logic on the ESP32.
- Preserve established thresholds and actuator mappings.
- Do not invent control thresholds or actuator behavior.
- Keep essential control and safety decisions local to the ESP32 where supported.

### MANUAL

- Expose only manual actions supported by the actual hardware.
- Display the current actuator state.
- Distinguish requested commands from confirmed device state.
- Reject unsupported or unsafe commands.
- Disable unavailable controls.

### OFF

- Implement only the OFF behavior supported by the existing hardware and safety design.
- Show a clear confirmation before accepting the request.
- Preserve safety interlocks.
- Do not disable essential life-support aeration if a verified safety rule requires it to remain active.

### Control communication and safety

- Send supported commands through the revised Wi-Fi/backend path.
- Validate commands on the ESP32 before actuation.
- Show pending, accepted, rejected, and timed-out status as appropriate.
- Do not mistake dashboard disconnection for device shutdown.
- Network loss must not stop local sensing or cause uncontrolled actuator behavior.
- A stale dashboard must not show old operating modes as confirmed current state.

Preserve verified physical controls and fail-safe mechanisms. Do not invent hardware features.

If the control path cannot be safely completed with the available hardware information, implement the software interface and clearly identify the hardware-dependent portion as pending.

## 7. CALIBRATION TAB

Create a separate **Calibration** tab with sections for:

1. pH calibration.
2. Salinity / electrical conductivity calibration.
3. Dissolved oxygen calibration.
4. Ammonia toxicity calculation information or applicable configuration.

Temperature must not have a separate calibration tab.

### 7.1 Reuse existing calibration code

Before creating new calibration functions:

1. Find all existing calibration implementations.
2. Identify the associated sensor and hardware.
3. Determine which implementation is authoritative.
4. Preserve valid formulas, coefficients, calibration points, and hardware assumptions.
5. Connect the existing implementation to the new interface.
6. Remove redundant implementations only after verifying their purpose.

Do not replace sensor-specific calibration algorithms with generic formulas or maintain competing implementations.

### 7.2 pH calibration

Inspect the existing PH-4502C implementation.

Documented buffers:

- pH 4.01.
- pH 6.86.
- pH 9.16.

Confirm the buffer labels and existing procedure before assuming calibration has been completed.

The interface must:

- Reuse the established calibration algorithm.
- Accept the required inputs.
- Validate calibration values.
- Display resulting parameters and status.
- Persist calibration settings.
- Ensure the ESP32 applies saved parameters to subsequent readings.
- Indicate incomplete or invalid calibration.

Do not invent calibration coefficients.

Reference: [https://cimpleo.com/blog/arduino-ph-meter-using-ph-4502c/](https://cimpleo.com/blog/arduino-ph-meter-using-ph-4502c/)

### 7.3 DFRobot Analog EC calibration — K = 1

The DFRobot Analog EC probe must use **cell constant K = 1** throughout the current implementation.

Documented conductivity standards:

- 1.413 mS/cm (1413 µS/cm).
- 12.88 mS/cm.

Use these standards according to the sensor's supported calibration procedure and the project's established protocol.

Requirements:

1. Set the cell constant to K = 1 in the appropriate firmware configuration.
2. Search for and resolve conflicting applicable cell-constant values.
3. Preserve the existing DFRobot-compatible calibration algorithm.
4. Preserve the documented temperature-compensation method.
5. Reuse existing calibration functions rather than implementing duplicate formulas.
6. Validate calibration inputs.
7. Persist calibration coefficients and relevant metadata.
8. Apply saved parameters to subsequent EC readings.
9. Display calibration status and the active cell constant in the interface where appropriate.
10. Distinguish conductivity from derived salinity.
11. Avoid double-applying the cell constant or calibration corrections.
12. Verify the EC probe's GPIO assignment against `perfboard_build_guide.pdf`.
13. Update tests and documentation to confirm consistent pin and K = 1 configuration.

References:

- Temperature compensation: [https://wiki.dfrobot.com/dfr0300/docs/19240](https://wiki.dfrobot.com/dfr0300/docs/19240)
- EC calibration: [https://wiki.dfrobot.com/dfr0300/docs/20349](https://wiki.dfrobot.com/dfr0300/docs/20349)

Do not claim physical calibration or validation merely because K = 1 is configured in software.

### 7.4 Dissolved oxygen calibration

Inspect the existing DO calibration code and hardware documentation.

Determine the installed ESP32 DO sensor's actual calibration requirements. Keep the DFRobot SEN0237-A procedure separate from the DO9100 handheld meter procedure unless the documentation supports their equivalence.

Requirements:

- Verify the sensor pin assignment against the hardware guide.
- Reuse applicable existing calibration code.
- Expose the inputs required by the established procedure.
- Persist calibration settings.
- Apply saved parameters to subsequent readings.
- Record the calibration timestamp and relevant metadata.
- Identify steps requiring physical measurements or reference instruments.
- Do not report calibration as complete before the required procedure is completed.

Reference: [https://wiki.dfrobot.com/sen0237-a/](https://wiki.dfrobot.com/sen0237-a/)

### 7.5 Ammonia-risk configuration

Ammonia risk is derived from other measurements; it is not an independent physical sensor.

Do not create a fake ammonia calibration workflow.

If the existing calculation has justified configurable parameters, provide an appropriate configuration interface. Otherwise, display the formula source, assumptions, and calculation status without unnecessary editable parameters.

### 7.6 Calibration persistence and activation

For every applicable sensor:

- Load saved settings during firmware initialization.
- Validate parameters before applying them.
- Apply active calibration to subsequent sensor readings.
- Use a safe fallback if calibration data is missing or invalid.
- Display calibration status in the dashboard.
- Store sensor identity, calibration values, timestamp, and applicable metadata.
- Preserve previous calibration records where appropriate.
- Confirm persistence and activation before reporting success.

Distinguish software configuration from physical calibration and validation.

## 8. SUPABASE DATABASE AND DATA FLOW

Inspect the previous Firebase data model and migrate relevant functionality to Supabase.

Create a maintainable schema for:

- Device identity and configuration.
- Latest sensor readings and status.
- Sensor history, where required.
- Calibration records and active calibration settings.
- Operating mode and confirmed actuator state.
- Control commands and acknowledgements.
- Relevant alerts and events.

Requirements:

1. Store measurements as numeric values, not formatted strings.
2. Keep units explicit.
3. Distinguish requested commands from confirmed device state.
4. Represent invalid readings separately from legitimate measurements.
5. Associate readings with calibration versions where practical.
6. Use appropriate timestamps, constraints, indexes, and relationships.
7. Avoid unnecessary high-frequency database writes.
8. Define sensible telemetry retention behavior.
9. Preserve calibration history where needed.
10. Provide reproducible SQL migrations or schema scripts.

Use Supabase Realtime for appropriate dashboard updates.

Define clear data flows for telemetry, calibration, device configuration, and control commands.

Use least-privilege RLS policies and secure device-write mechanisms. If server-side endpoints or Edge Functions are needed to authorize device operations, implement or explicitly document them.

Never expose privileged credentials to the browser or ESP32.

## 9. ESP32 TELEMETRY, PINOUT, AND FIRMWARE

Inspect the existing firmware and preserve working sensor integrations.

### 9.1 Pinout reference and verification

Before changing hardware-related code, inspect `perfboard_build_guide.pdf` and extract its relevant pin assignments.

For every connected component, reconcile the documented physical connections with the active firmware.

Verify, where applicable:

- ESP32 GPIO assignment.
- Sensor or module signal connection.
- Power supply and voltage requirements.
- Ground connections.
- ADC pin suitability for analog sensors.
- I²C SDA and SCL assignments.
- SPI MOSI, MISO, SCK, and chip-select assignments.
- UART TX/RX assignments.
- Digital input/output requirements.
- Boot-strapping restrictions.
- Input-only GPIO limitations.
- Peripheral and pin conflicts.
- Compatibility with the actual ESP32 board and connected hardware.

Do not assume all ESP32 variants have identical pin capabilities.

Do not change physical wiring assumptions silently. If the guide and firmware disagree, resolve the discrepancy using verified evidence or flag it for human confirmation.

Keep pin definitions centralized where practical. Sensor drivers should use the authoritative configuration rather than unrelated hardcoded assignments.

### 9.2 Telemetry contract

Establish a consistent data contract between ESP32, Supabase, and the frontend.

Where supported by the actual implementation, telemetry may include:

- Device identifier.
- Reading timestamp or sequence.
- pH.
- Electrical conductivity.
- Derived salinity.
- Temperature.
- Dissolved oxygen.
- Emerson-based unionized ammonia fraction or risk indicator.
- Sensor validity flags.
- Current operating mode.
- Confirmed actuator states.
- Calibration version or status.
- Device and connectivity faults.

Adapt the contract to actual hardware. Do not invent unsupported measurements.

### 9.3 Firmware requirements

- Validate raw sensor readings.
- Handle missing and out-of-range values.
- Keep sensor acquisition independent of the dashboard.
- Avoid blocking sensor loops with network operations.
- Use bounded retries and timeouts.
- Reconnect to Wi-Fi and the backend safely.
- Avoid unnecessary repeated writes.
- Keep control loops responsive.
- Separate hardware drivers from communication and application logic.
- Load and apply calibration settings consistently.
- Use DFRobot Analog EC cell constant K = 1.
- Follow verified GPIO assignments from `perfboard_build_guide.pdf`.
- Report connectivity failures honestly.
- Keep automatic-control decisions local to the ESP32 where supported.

Network failure must not disable local sensor acquisition or cause uncontrolled actuator behavior.

## 10. CODE CLEANUP AND REDUNDANCY REMOVAL

Review the entire relevant repository.

Remove redundant or obsolete implementations of:

- Calibration algorithms.
- Sensor conversions.
- Ammonia calculations.
- Unit conversions.
- Supabase/Firebase communication.
- LoRa communication.
- Camera functionality.
- Unused routes and UI components.
- Unused environment variables and configuration.
- Dead code and obsolete documentation.

Centralize shared formulas and conversions.

Ensure the DFRobot Analog EC cell constant K = 1 has one clear, authoritative configuration source where practical.

Ensure pin assignments have one authoritative configuration source where practical.

Do not delete files without verifying their references and purpose.

Do not rewrite working sensor drivers without a concrete technical reason.

## 11. REQUIRED USER INTERFACE

### Main tab — Water Quality Sensing

Display:

- pH.
- Salinity.
- Temperature.
- Dissolved oxygen.
- Emerson-based ammonia toxicity indicator.
- Device and data connection status.
- Last-update timestamps.
- AUTO / MANUAL / OFF controls.
- Sensor warnings and control status.

### Calibration tab

Display separate sections for:

- pH.
- Salinity / EC.
- Dissolved oxygen.
- Ammonia-risk information or justified calculation configuration.

Temperature must not have a calibration tab.

The EC section must use K = 1 and the existing calibration implementation.

### Access interface

Provide the QR-code and typable-code access flow without a conventional username/password login page.

Make the interface simple, mobile-friendly, and suitable for use near the physical equipment.

### General interface behavior

- Use readable cards, labels, and consistent units.
- Include loading, empty, disconnected, stale, and error states.
- Provide feedback for calibration saves and control requests.
- Confirm potentially disruptive operating-mode changes.
- Distinguish real sensor data from simulated development data.
- Preserve visible last-known values while clearly marking stale readings.

## 12. DOCUMENTATION UPDATES

Inspect and update the relevant existing documentation, including:

- [`README.md`](http://README.md)
- `docs/[APP.md](http://APP.md)`
- `docs/[CALIBRATION.md](http://CALIBRATION.md)`
- `docs/[FIRMWARE.md](http://FIRMWARE.md)`
- `docs/[SETUP.md](http://SETUP.md)`
- `docs/[STACKS.md](http://STACKS.md)`
- `docs/[SYSTEM-ARCHITECTURE.md](http://SYSTEM-ARCHITECTURE.md)`
- `docs/[BLOCK-DIAGRAM.md](http://BLOCK-DIAGRAM.md)`
- `docs/[FLOWCHART.md](http://FLOWCHART.md)`
- `docs/[WIRING.md](http://WIRING.md)`
- `docs/[Components.md](http://Components.md)`
- `docs/[TESTING.md](http://TESTING.md)`
- `docs/[TROUBLESHOOTING.md](http://TROUBLESHOOTING.md)`
- Relevant change logs and architecture documents.

Do not create unnecessary duplicate documentation.

Document the following architecture decisions:


| Area                  | Current-phase requirement                                            |
| --------------------- | -------------------------------------------------------------------- |
| Frontend              | React.js                                                             |
| Backend               | Supabase                                                             |
| Communication         | Direct ESP32 Wi-Fi                                                   |
| Separate house bridge | Not required                                                         |
| Camera                | Removed                                                              |
| LoRa                  | Deferred                                                             |
| Website access        | QR code or typable code                                              |
| EC probe              | DFRobot Analog EC, K = 1                                             |
| Pin assignments       | Referenced against `perfboard_build_guide.pdf`                       |
| Calibration           | Existing sensor-specific routines reused and persistently integrated |


Also document:

- The actual implemented directory structure.
- Environment variables and Supabase setup.
- Database migrations.
- Firmware flashing instructions.
- QR-code and access-code provisioning.
- The EC cell constant K = 1 and its role.
- EC calibration standards and temperature compensation.
- The Emerson formula source and assumptions.
- Pinout mappings extracted from the PDF.
- Resolved and unresolved pin conflicts.
- Remaining hardware dependencies.
- Features that are implemented, pending, deferred, or hardware-unverified.

All diagrams and wiring instructions must agree with the authoritative pin configuration and verified project hardware reference.

## 13. TESTING AND ACCEPTANCE CRITERIA

Do not declare the system complete merely because the frontend launches.

### 13.1 Frontend

Verify:

- The application builds.
- Both required tabs render.
- Sensor cards handle missing and stale data.
- Realtime updates work.
- Calibration inputs are validated.
- Calibration data is loaded and saved correctly.
- Control commands show accurate status.
- QR-code and typable-code access work as implemented.
- No conventional login page remains.
- No active camera or LoRa feature remains.

### 13.2 Sensor calculations and calibration

Verify:

- Existing calibration routines are reused correctly.
- Invalid calibration values are rejected.
- Saved calibration data persists.
- Firmware actually applies saved parameters.
- The Emerson calculation follows the verified formula and units.
- Missing or invalid pH and temperature values produce an unavailable risk result instead of a misleading value.
- Conductivity and salinity are not confused.
- Sensor errors are not displayed as valid zero readings.

For the DFRobot Analog EC probe, specifically verify:

- The configured cell constant is K = 1.
- No conflicting applicable cell constant remains in the active implementation.
- EC conversion and calibration use the configuration consistently.
- The cell constant and software calibration coefficients are not double-applied.
- Temperature compensation remains consistent with the verified method.
- Tests cover relevant conversion and calibration paths.
- The EC signal-pin assignment matches the hardware guide.

### 13.3 Pinout and electrical compatibility

Verify:

- The PDF was successfully located and inspected, or the limitation is explicitly documented.
- The active firmware pin definitions agree with the guide wherever the guide provides definitive assignments.
- No unresolved duplicate pin definitions remain in the active implementation.
- GPIO assignments are appropriate for their intended peripheral functions.
- ADC, I²C, SPI, UART, and digital interfaces are used appropriately where applicable.
- Relevant voltage and logic-level requirements have been checked against the documented hardware.
- Wiring documentation and diagrams agree with the firmware.

Do not claim that the physical perfboard has been verified merely because the firmware and documentation agree.

### 13.4 Supabase

Verify:

- Database migrations are valid.
- Required operations work under intended permissions.
- Realtime subscriptions function.
- Calibration changes persist.
- Device writes follow the intended authorization path.
- Unauthorized control requests are rejected.
- No privileged credentials appear in the frontend build or firmware.
- Failures do not produce false success messages.

### 13.5 Firmware

Compile the firmware when the required toolchain is available.

Verify, as far as possible:

- Existing sensor drivers compile.
- Calibration settings load and are applied.
- The EC configuration uses K = 1.
- Pin definitions match the verified project reference.
- Wi-Fi failure does not stop local sensing.
- Invalid readings are handled safely.
- Automatic-control logic remains functional.
- Commands are validated before actuation.
- LoRa is not required for the current-phase build.

Mark physical sensor accuracy, actual calibration, actuator operation, electrical compatibility on the assembled board, and live network integration as unverified unless they have genuinely been tested.

### 13.6 Final acceptance checklist

- Existing repository preserved and revised.
- React.js active frontend.
- Supabase replaces Firebase.
- Camera functionality removed.
- LoRa deferred and removed from active implementation.
- Direct ESP32 Wi-Fi communication implemented where supported.
- Five required sensing indicators displayed.
- Emerson (1975) formula verified.
- AUTO / MANUAL / OFF controls integrated with safe device behavior.
- Existing calibration code reused.
- Temperature has no calibration tab.
- DFRobot Analog EC uses K = 1.
- EC calibration and temperature compensation remain consistent.
- Pin assignments reconciled with `perfboard_build_guide.pdf`.
- Firmware and wiring documentation agree.
- Calibration settings persist and are applied.
- QR-code and typable-code access implemented.
- Access authorization and database security addressed.
- Redundant code and obsolete dependencies removed.
- Documentation and change log updated.
- Available builds and tests executed.
- Physical hardware verification status accurately reported.
- Remaining limitations accurately reported.

## 14. REQUIRED CHANGE LOG

Create or update a change log containing:

- Affected file or module.
- Previous behavior.
- New behavior.
- Reason for the change.
- Implementation and test status.
- Required migration or hardware action.

Explicitly document these seven changes:

1. Firebase replaced with Supabase.
2. Camera functionality removed.
3. LoRa deferred for the current phase.
4. Direct ESP32 Wi-Fi communication adopted.
5. QR-code or typable-code access adopted without a conventional login page.
6. DFRobot Analog EC probe configured with K = 1.
7. Firmware pin assignments and hardware documentation reconciled with `perfboard_build_guide.pdf`.

For the EC change, identify every relevant configuration or formula updated and whether the active measurement pipeline was verified.

For the pinout change, list the relevant GPIO assignments, affected files, discrepancies resolved, and any unresolved conflicts.

Use accurate status labels:

- Implemented and tested.
- Implemented but unverified.
- Pending.
- Deferred.

Do not describe deferred LoRa as implemented, simulated data as real telemetry, or untested hardware as verified.

## 15. FINAL IMPLEMENTATION REPORT

After modifying the repository, provide the following report.

### A. Repository audit

Summarize the original architecture, existing implementations, important conflicts, and missing components.

### B. Changes implemented

Describe the frontend, Supabase, firmware, sensing, calibration, control, access flow, pinout alignment, and code cleanup.

### C. Files changed

List significant created, modified, and deleted files with explanations.

### D. Hardware pinout report

Include:

- Whether `perfboard_build_guide.pdf` was found and inspected.
- The pin assignments extracted from the guide that are relevant to the implementation.
- The corresponding firmware definitions.
- Discrepancies found and how they were resolved.
- Any unresolved GPIO, voltage, or peripheral-interface conflicts.
- Whether only software/documentation consistency was verified or the actual perfboard was physically inspected and tested.

### E. Configuration and deployment

Provide environment-variable names, Supabase setup, migration steps, firmware instructions, and access-code/QR-code provisioning instructions.

Never include actual secrets.

### F. Verification results

List actual build, lint, type-check, and test commands run and their results.

Distinguish software verification from physical hardware testing.

### G. Remaining issues

Identify unresolved hardware questions, unverified calibration procedures, security limitations, and incomplete integrations.

### H. Architecture change summary

Confirm the implementation status of all seven mandatory changes.

## FINAL INSTRUCTION

Start by inspecting the existing repository and locating `perfboard_build_guide.pdf`. Then implement the revised system in place.

Do not stop after producing a plan, mockup, proposed schema, or isolated snippets.

Complete as much as the available repository and environment support, run available tests, update the documentation, and report limitations honestly.

Preserve working project functionality wherever compatible with the revised requirements. Reuse existing calibration code, apply the confirmed DFRobot Analog EC cell constant K = 1 consistently, align firmware pin assignments with the project-specific hardware guide, and never fabricate results or claim success without verification.