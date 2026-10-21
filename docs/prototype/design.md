# Transit watch prototype

## Audit (base: f877ca5, latest main when work began)

The repository contains one native window and a custom drawing layer in
`src/c/main.c`. Its original screens are a three-row home page, a three-departure
board, and a five-stop timeline. Up/Down move a phone-side selection; Select
changes direction; a long Select opens details. Every 80 ms the entire window is
redrawn, even when nothing is moving. The original detail screens use a white
background and equally weighted departures.

`src/pkjs/index.js` owns location, selection, three-row payloads, directions,
favorites, API requests, a one-minute refresh budget, route/trip caches, and
settings. `transit.js` normalizes Transit v4 responses and merges live stop
times. The hosted settings page and saved setting names are independent of the
watch presentation. The manifest targets only Emery and uses keys 0–72.
The existing data tests cover normalization, mode filters, favorites, badges,
and real-time stop offsets. The build uses the repository's shared Pebble SDK
4.33.1 toolchain; the unmodified app builds and its data tests pass.

Retain the native window, drawing primitives, message transport, UUID, Transit
v4 endpoints/parameters, configuration URL, setting storage, filters, API key
handling, caches, and refresh budget. Replace watch layouts and button routing.
Extend presentation messages with viewport position, freshness/state, home stop
names, and per-stop prediction provenance. Adapt the three-row payload to a
sliding viewport, so navigation has no three-route page boundaries. Load trip
details when entering the timeline, rather than for a purely visual highlight.

## References inspected

- [Transit Apple Watch guide and official screenshots](https://help.transitapp.com/article/210-use-transit-on-apple-watch)
  (last updated March 10, 2026): route-colored nearby cards, large route numerals
  and ETAs, destination/nearest stop underneath, direction switch, pinned lines
  first, and a concise three-departure board.
- [Pebble icon style](https://github.com/pebble-dev/iconography/blob/master/STYLE-GUIDE.md)
  and [iconography](https://github.com/pebble-dev/iconography): integer geometry,
  two-pixel small icons, horizontal/vertical and 1:1, 2:1, 3:1 slopes.
- [Pebble SDK](https://developer.repebble.com/), the installed Emery headers,
  [hardware](https://developer.repebble.com/guides/tools-and-resources/hardware-information/),
  and [battery guidance](https://developer.repebble.com/guides/best-practices/conserving-battery-life/).
- The supplied All Things Pebble and pebbleos-core Read the Docs URLs could not
  be retrieved by the web reader. Platform/API support is verified against the
  installed SDK and actual Emery build/emulator instead.

The reference is a visual/interaction study. No Apple/Transit image, font, or
other proprietary interface asset is copied into the app.

## Screen hierarchy and controls

Nearby → departure board → trip stops. Back always moves one level up.

| Screen | Up | Select | Down | Hold Select | Hold Up |
| --- | --- | --- | --- | --- | --- |
| Nearby | Previous route | Open board | Next route | Other direction | Pin/unpin |
| Board | Next departure | Other direction | Open trip stops | Other direction | Pin/unpin |
| Stops | Previous stop | Next departure | Next stop | — | — |

Back exits only from Nearby. The primary navigation never needs a long press.
If there are no results, Select retries within the existing refresh budget.
Scrolling, pinning, and direction changes reuse nearby results. Entering the
timeline may request `/trip_details`; its five-minute cache remains in use.

## Drawing specification (200 × 228, Emery only)

- Black canvas; 6–8 px internal gutters; no watch-shaped frame or outer border.
- Nearby: 18 px header, three approximately 64 px route-colored cards with
  narrow black separation. A large badge on the left, two-line direction in the
  middle, and large numeric ETA on the right. Stop names occupy the lower line.
  A white selection rail preserves route color and identifies the active row.
- Board: full route color, large route identity and wrapped direction, a
  dominant first/selected departure (60 px SDK numerals), smaller following
  departures separated by a clean rule, nearest stop, and subtle button hints.
- Stops: black background, route-colored identity band and vertical track;
  a sliding three-row viewport over five real stops, with a larger selected
  row that reveals its full-width name and arrival time.
- Typography: system Gothic 14/18/24 Bold for labels/directions; Bitham 30 Black
  for route badges; LECO 32/60 Bold for numeric countdowns. Clock times and
  nonnumeric ETA states use appropriate full-character Gothic/Bitham fonts.
- API colors quantize to the nearest 2-bit channel. Foreground selection is
  checked after quantization; unreadable API foregrounds fall back to black or
  white. Secondary text uses a contrast-safe subdued treatment.
- Live prediction uses a small native signal glyph plus a label. Scheduled,
  saved/stale, expired, and unavailable departures are distinct. Cached data is
  never labeled Live, including immediately after reopening.
- Long text wraps on the board and timeline; compact list rows may ellipsize.
  The full available headsign is retained in the detail payload.

## States, responsiveness, and power

Initial location/loading, no nearby routes, no departures, missing credentials,
location failure, phone disconnect, service/network failure, and saved data all
have deliberate copy and a useful next action. Errors with cached results use
a compact status footer, preserving the route list/board. Full explanatory
states are used when no results exist.

Countdowns are calculated on-watch between refreshes. Freshness expires after
two minutes without a successful API response, or immediately when the phone
reports cached/error data or disconnects. A successful cache replay does not
renew the observation timestamp.

Only finite selection/list motion uses a short animation timer. Static screens
redraw on data/button events and the minute tick, avoiding the former perpetual
12.5 fps redraw loop. No bitmap-heavy backgrounds or additional dependencies.

## Validation plan

Build the production PBW; run the original transit tests plus companion
integration tests for viewport navigation, button messages, request budgets,
cache freshness, saved preferences, and error states. Install the native binary
in the Emery emulator. Drive all three screens and controls using realistic,
explicitly labeled API fixtures, and capture actual emulator screenshots under
`docs/prototype/screenshots/`. Fixture data stays in test tooling and is never
used by the production companion.
