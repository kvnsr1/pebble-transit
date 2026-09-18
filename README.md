# Pebble Transit

Pebble Transit is a Pebble Time 2 companion for nearby public transportation. It mirrors the glanceable part of Transit’s Nearby screen with three route-colored nearby lines per page, nearest stops, directions, and live departure countdowns. A detail view shows the next three departures and the upcoming stops for the selected trip.

## Architecture

- The native C watch app targets `emery` (Pebble Time 2).
- PebbleKit JS runs in the Pebble phone app, requests the phone’s current location, calls Transit’s stable v4 API, caches the last successful nearby response, and sends compact messages to the watch.
- The hosted `config/` page controls Transit API credentials, nearby radius, API-provided transit mode classifications, and favorite lines.
- Live nearby data is refreshed no more than once per minute while the app is open. The animated signal indicates a real-time prediction; `SCHED` indicates a static timetable departure.

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

Transit’s free access is currently limited to five requests per minute and 1,500 per month. On the homepage, Pebble Transit refreshes all nearby lines together with one `/nearby_routes` request per minute. Paging and changing the highlighted line use that cached response and make no requests. In a detail view, the selected line refreshes once per minute through `/stop_departures`; that single live request also includes the displayed downstream stops. Mode discovery is weekly and `/trip_details` is requested only when a departure’s stop detail is needed.

## Publish the settings page

The companion currently opens:

```text
https://kvnsr1.github.io/pebble-transit/config/
```

GitHub Pages publishes the settings page from this repository’s `main` branch. If the repository or host changes, update `CONFIG_URL` near the top of `src/pkjs/index.js`, publish the `config/` folder over HTTPS, then rebuild the app.

## Controls

### Nearby lines

- **Up / Down:** move the highlight line by line. Moving beyond the top or bottom row automatically reveals the previous or next group of three.
- **Select:** switch the highlighted line’s direction using already-loaded nearby data.
- **Hold Select:** open the highlighted line’s next-three-departures view.
- **Hold Up:** pin or unpin the highlighted line. Nearby pinned lines sort first after the next refresh.
- **Back:** exit.

### Line details

- **Select:** cycle the highlighted departure among the next three.
- **Up / Down:** toggle between the departure board and upcoming stops for the highlighted trip.
- **Back:** return to nearby lines.

Stop ETAs use Transit’s live stop-departure data when available, with the selected trip’s real-time offset as a fallback.

## Configuration

Open Pebble Transit’s settings in the phone app to:

- enable or disable Transit’s localized mode classifications, including light rail, metro, commuter rail, bus, ferry, cable car, gondola, funicular, trolleybus, and monorail;
- pin any lines found in the most recent nearby response;
- choose a search radius from 150 to 1,500 metres; and
- add or replace the Transit Public API key.

Mode names are initially seeded from the GTFS route classifications, then replaced or extended with the `mode_key` and localized `mode_name` values returned by `/available_networks` and `/nearby_routes`.

## Tests

Run the data-mapping tests with:

```sh
node test/transit.test.js
```

API reference: [Transit API v4](https://api-doc.transitapp.com/v4.html)
