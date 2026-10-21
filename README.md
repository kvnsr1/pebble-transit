# Pebble Transit

Pebble Transit is a native Pebble Time 2 companion for nearby public transportation. This prototype adapts [Transit for Apple Watch](https://help.transitapp.com/article/210-use-transit-on-apple-watch) to Emery's 200 × 228 display and physical buttons: a dark, scrolling nearby list, route-colored departure boards with oversized countdowns, and an interactive stop timeline.

See the [design and architecture audit](docs/prototype/design.md), [actual emulator screenshot gallery and validation report](docs/prototype/README.md), and [test instructions](#tests).

Home route badges use at most three bold characters. Short route numbers and letters are preserved, directional names use familiar initials such as `E` for East, and longer multi-word names use contextual initials such as `VN` for Van Nuys.

The watch launcher and mobile Pebble app share the same transparent, borderless train icon. The packaged launcher resource is sized to Pebble's 25×25 menu-icon requirement, with a high-resolution source retained under `resources/images/`.

## Architecture

- The native C watch app targets `emery` (Pebble Time 2).
- PebbleKit JS runs in the Pebble phone app, requests the phone’s current location, calls Transit’s stable v4 API, caches the last successful nearby response, and sends compact messages to the watch.
- The hosted `config/` page controls Transit API credentials, nearby radius, API-provided transit mode classifications, and favorite lines.
- Live nearby data is refreshed no more than once per minute while the app is open. `Live` and a static signal glyph indicate a fresh real-time prediction; `Sched` indicates a timetable departure. `Saved` indicates cached data, an unavailable phone, or a prediction older than two minutes. Estimated downstream times are explicitly labeled.

The watch has no direct internet or GPS connection, so fresh data requires the paired phone. Cached departures remain visible through a temporary network or location failure.

## Build

This separate repository retains Pebble Flight’s toolchain and deployment flow. From the project directory:

```sh
./scripts/build.sh
```

The finished package is `build/pebble-transit.pbw`.

To build and launch in the Pebble Time 2 emulator:

```sh
./scripts/build.sh
./scripts/emulator.sh
```

For a physical watch, enable **Dev Connect** in the Pebble mobile app and install using the address it provides. The legacy local-network form is:

```sh
pebble install --phone PHONE_IP
```

## Transit API key

The personal API key is kept in the ignored file `src/pkjs/private-key.js`; it is not committed. On a clean checkout, either open the phone settings page and enter a key, or supply one for the first build:

```sh
TRANSIT_API_KEY='your_key' ./scripts/build.sh
```

The build script creates the ignored key module when it does not exist. To replace an existing bundled key, remove that local file and rebuild with `TRANSIT_API_KEY` set, or enter a replacement in phone settings. The settings page never receives the saved key; it only receives a boolean indicating that one exists.

Because PebbleKit JS makes requests from the phone, a credential bundled into a `.pbw` can ultimately be extracted. Use this design for a personal build. A broadly distributed build should send requests through a small authenticated proxy with its own per-user controls.

Transit’s free access is currently limited to five requests per minute and 1,500 per month. On the homepage, Pebble Transit refreshes all nearby lines together with one `/nearby_routes` request per minute. Scrolling and changing direction use that cached response and make no requests. In a detail view, the selected line refreshes once per minute through `/stop_departures`; that single live request also includes the displayed downstream stops. Mode discovery is weekly and `/trip_details` is requested only when a departure’s stop detail is needed.

## Publish the settings page

The companion currently opens:

```text
https://kvnsr1.github.io/pebble-transit/config/
```

GitHub Pages publishes the settings page from this repository’s `main` branch. If the repository or host changes, update `CONFIG_URL` near the top of `src/pkjs/index.js`, publish the `config/` folder over HTTPS, then rebuild the app.

## Controls

| Screen | Up | Select | Down | Hold Select | Hold Up | Back |
| --- | --- | --- | --- | --- | --- | --- |
| Nearby | Previous route | Open departures | Next route | Other direction | Pin/unpin | Exit |
| Departures | Next departure | Other direction | Open trip stops | Other direction | Pin/unpin | Nearby |
| Trip stops | Previous stop | Next departure | Next stop | — | — | Departures |

The route list scrolls one row at a time, keeping the selection near the middle. Nearby pinned routes sort first on refresh. Primary navigation uses short presses. On an empty/error screen, Select retries within the refresh budget. A route with only one direction stays on that direction.

The board emphasizes the selected departure and shows the other two underneath. Up cycles only the departures that exist. Down loads stops for that departure; Select on the timeline cycles departures and loads their stops. The timeline shows three readable rows at a time, with all five available stops accessible using Up/Down. The selected stop expands to reveal its name and arrival time.

Stop ETAs use Transit's live stop-departure data when available, with the selected trip's real-time offset as a fallback. The timeline distinguishes live stop times, estimates from a trip, scheduled times, and saved information. Times follow the watch's 12/24-hour preference. Countdowns use elapsed whole minutes; expired departures become `LEFT` instead of remaining `NOW` indefinitely.

## Configuration

Open Pebble Transit’s settings in the phone app to:

- enable or disable Transit’s localized mode classifications, including light rail, metro, commuter rail, bus, ferry, cable car, gondola, funicular, trolleybus, and monorail;
- pin any lines found in the most recent nearby response;
- choose a search radius from 150 to 1,500 metres; and
- add or replace the Transit Public API key.

Mode names are initially seeded from the GTFS route classifications, then replaced or extended with the `mode_key` and localized `mode_name` values returned by `/available_networks` and `/nearby_routes`.

## Tests

Run the original data tests and companion integration tests:

```sh
node test/transit.test.js
node test/companion.test.js
```

If Node is not on your shell path, use `./.tools/node/bin/node`. Test pure native presentation rules with the host C compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror test/presentation.test.c src/c/presentation.c -o /tmp/transit-presentation-test
/tmp/transit-presentation-test
```

Build and run the normal live companion with the existing scripts above. For reproducible visual testing without an API key, use the isolated fixture runner after building:

```sh
./.tools/uv-tools/pebble-tool/bin/python scripts/prototype-emulator.py
```

The runner uses the shared repository toolchain, a temporary Emery flash file, an unused loopback port, and the production native binary. It executes the actual companion JavaScript with synthetic Transit v4 responses and simulated phone APIs, checks physical button messages, and captures real 200 × 228 emulator screenshots. It never reads `private-key.js`, calls Transit, modifies an existing emulator, or puts fixture data into the production PBW. On another SDK layout, pass `--sdk-root /path/to/SDK` and run with a Python environment containing the Pebble SDK's `libpebble2` and Pillow packages. Use `--output /tmp/transit-screenshots` to keep new captures out of the working tree.

To disable finite selection animations at build time:

```sh
CFLAGS='-DTRANSIT_DISABLE_ANIMATION' ./scripts/build.sh
```

Run `./scripts/build.sh` again without `CFLAGS` to restore the normal prototype. Static screens never use a continuous animation timer.

API reference: [Transit API v4](https://api-doc.transitapp.com/v4.html)
