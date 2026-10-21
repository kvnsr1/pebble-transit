'use strict';
var assert = require('assert');
var create = require('./support/companion');
var keys = require('../package.json').pebble.messageKeys;
var count = 0;
function test(name, run) { run(); count++; console.log('PASS ' + name); }
function ready(options) { var app = create(options); app.event('ready'); return app; }

test('sliding viewport follows all six routes without page jumps or API requests', function() {
  var c = ready(), initial = c.requests.length;
  var expected = [[1, 0, 1], [2, 1, 1], [3, 2, 1], [4, 3, 1], [5, 3, 2], [0, 0, 0]];
  expected.forEach(function(row) {
    c.message({REQUEST_ROW_DELTA: 1});
    var p = c.latest();
    assert.deepStrictEqual([p.LINE_INDEX, p.HOME_START, p.HOME_SELECTED], row);
    assert.ok(p.HOME_1_STOP.length);
  });
  c.message({REQUEST_ROW_DELTA: -1}); assert.strictEqual(c.latest().LINE_INDEX, 5);
  assert.strictEqual(c.requests.length, initial);
});

test('direction changes preserve board context and reuse loaded data', function() {
  var c = ready(), initial = c.requests.length;
  c.message({REQUEST_DETAILS: 1});
  c.message({REQUEST_DIRECTION: 2});
  assert.strictEqual(c.latest().DETAIL_ACTIVE, 1);
  assert.strictEqual(c.latest().HEADSIGN, 'East LA / Atlantic');
  c.message({REQUEST_DIRECTION: 2});
  assert.strictEqual(c.latest().HEADSIGN, 'Santa Monica');
  c.message({REQUEST_DETAILS: 2});
  c.message({REQUEST_DIRECTION: 1});
  assert.strictEqual(c.latest().DETAIL_ACTIVE, 0);
  assert.strictEqual(c.requests.length, initial);
});

test('boarding and cycling departures do not fetch trip details', function() {
  var c = ready(), initial = c.requests.length;
  c.message({REQUEST_DETAILS: 1});
  c.message({REQUEST_DEPARTURE: 1}); assert.strictEqual(c.latest().DEPARTURE_INDEX, 1);
  c.message({REQUEST_DEPARTURE: 1}); assert.strictEqual(c.latest().DEPARTURE_INDEX, 2);
  c.message({REQUEST_DEPARTURE: 1}); assert.strictEqual(c.latest().DEPARTURE_INDEX, 0);
  c.advance(700);
  assert.strictEqual(c.requests.length, initial);
});

test('refresh budget applies across route changes and details', function() {
  var c = ready(), initial = c.requests.length;
  for (var i = 0; i < 5; i++) {
    c.message({REQUEST_ROW_DELTA: 1}); c.message({REQUEST_DETAILS: 1}); c.advance(700);
    c.message({REQUEST_REFRESH: 2}); c.message({REQUEST_DETAILS: 2});
  }
  assert.strictEqual(c.requests.length, initial);
  c.advance(60000); c.message({REQUEST_REFRESH: 2});
  assert.strictEqual(c.requests.length, initial + 1);
});

test('timeline loads real trip data lazily and reuses its cache', function() {
  var c = ready(), initial = c.requests.length;
  c.message({REQUEST_DETAILS: 1}); c.message({REQUEST_STOPS: 1});
  assert.strictEqual(c.latest().STOP_COUNT, 5);
  assert.strictEqual(c.latest().STOP_1_NAME, 'Wilshire / Western');
  assert.strictEqual(c.latest().STOP_LIVE_FLAGS, 0);
  assert.strictEqual(c.latest().STOP_ESTIMATE_FLAGS, 31);
  assert.strictEqual(c.requests.length, initial + 1);
  c.message({REQUEST_STOPS: 2}); c.message({REQUEST_STOPS: 1});
  assert.strictEqual(c.requests.length, initial + 1);
  c.message({REQUEST_DEPARTURE: 1}); assert.strictEqual(c.requests.length, initial + 2);
});

test('late trip responses cannot replace a newer direction or reopen the timeline', function() {
  var c = ready({defer: true}); c.pending.shift()();
  c.message({REQUEST_DETAILS: 1}); c.message({REQUEST_STOPS: 1});
  var late = c.pending.shift();
  c.message({REQUEST_STOPS: 2}); c.message({REQUEST_DIRECTION: 2}); late();
  assert.strictEqual(c.latest().HEADSIGN, 'East LA / Atlantic');
  assert.strictEqual(c.latest().STOP_COUNT, 0);
  assert.strictEqual(c.latest().IS_LOADING, 0);
});

test('cached routes keep their original observation time and are explicitly saved', function() {
  var first = ready(), timestamp = first.latest().DATA_UPDATED_AT;
  var second = ready({storage: first.storage, scenario: 'network', now: Date.UTC(2026, 9, 20, 9, 46)});
  assert.strictEqual(second.latest().DATA_UPDATED_AT, timestamp);
  assert.strictEqual(second.latest().DATA_CACHED, 1);
  assert.strictEqual(second.latest().HOME_COUNT, 3);
});

test('a detail refresh does not freshen other nearby route predictions', function() {
  var c = ready(), timestamp = c.latest().DATA_UPDATED_AT;
  c.message({REQUEST_DETAILS: 1}); c.advance(61000); c.message({REQUEST_REFRESH: 2});
  assert.ok(c.latest().DATA_UPDATED_AT > timestamp);
  c.context.app.detailActive = false; c.context.sendCurrent();
  assert.strictEqual(c.latest().DATA_UPDATED_AT, timestamp);
});

test('pin/unpin preserves existing settings and writes the existing favorites key', function() {
  var c = ready();
  c.message({REQUEST_TOGGLE_FAVORITE: 1});
  var settings = JSON.parse(c.storage['pebbleTransit.settings']);
  assert.strictEqual(settings.favorites.length, 0); assert.strictEqual(settings.radius, 700);
  c.message({REQUEST_TOGGLE_FAVORITE: 1});
  settings = JSON.parse(c.storage['pebbleTransit.settings']);
  assert.deepStrictEqual(settings.favorites, ['fixture:0']);
  assert.strictEqual(c.latest().HOME_1_FAVORITE, 1);
});

test('configuration never includes the saved credential and saved radius/modes still apply', function() {
  var c = ready(); c.event('showConfiguration');
  assert.ok(c.context.configurationUrl.indexOf('fixture-only') === -1);
  c.event('webviewclosed', {response: encodeURIComponent(JSON.stringify({radius: 1200,
    enabledModes: {Bus: false}, favorites: ['fixture:2']}))});
  var settings = JSON.parse(c.storage['pebbleTransit.settings']);
  assert.strictEqual(settings.apiKey, 'fixture-only'); assert.strictEqual(settings.radius, 1200);
  assert.strictEqual(c.latest().ROUTE_NAME, 'B');
  assert.ok(c.requests[c.requests.length - 1].indexOf('max_distance=1200') !== -1);
});

test('empty, credential, location, and network failures have distinct states', function() {
  [['empty', 2], ['key', 3], ['location', 4], ['network', 5]].forEach(function(pair) {
    var c = ready({scenario: pair[0]});
    assert.strictEqual(c.latest().DATA_STATE, pair[1]);
    assert.strictEqual(c.latest().HOME_COUNT, 0); assert.strictEqual(c.latest().IS_LOADING, 0);
  });
});

test('routes with no departures remain browsable with unavailable ETA values', function() {
  var c = ready({scenario: 'no-departures'});
  assert.strictEqual(c.latest().HOME_COUNT, 3); assert.strictEqual(c.latest().ETA_1, 0);
  c.message({REQUEST_DETAILS: 1}); c.message({REQUEST_STOPS: 1});
  assert.strictEqual(c.latest().STOP_COUNT, 0); assert.strictEqual(c.latest().IS_LOADING, 0);
});

test('long destination and stop names survive the detail payload', function() {
  var c = ready({scenario: 'long'});
  assert.strictEqual(c.latest().HEADSIGN, 'Universal City / Studio City via North Hollywood Station');
  assert.strictEqual(c.latest().HOME_1_ROUTE, 'VN');
  c.message({REQUEST_DETAILS: 1}); c.message({REQUEST_STOPS: 1});
  assert.ok(c.latest().STOP_3_NAME.length > 34);
});

test('HTTP authentication, limits, and service failures preserve actionable copy', function() {
  [401, 429, 503].forEach(function(status) {
    var c = ready({status: status}); assert.strictEqual(c.latest().HOME_COUNT, 0);
    assert.ok(c.latest().ERROR_MESSAGE.length > 10);
    assert.strictEqual(c.latest().DATA_STATE, status === 401 ? 3 : 5);
  });
});

test('message keys remain backward compatible and row key offsets are contiguous', function() {
  assert.strictEqual(keys.REQUEST_REFRESH, 0); assert.strictEqual(keys.HOME_3_FAVORITE, 72);
  [1, 2, 3].forEach(function(i) {
    var fields = ['ROUTE', 'HEADSIGN', 'STOP', 'ETA', 'LIVE', 'COLOR', 'TEXT_COLOR', 'FAVORITE'];
    fields.forEach(function(field, offset) { assert.strictEqual(keys['HOME_' + i + '_' + field], keys['HOME_' + i + '_ROUTE'] + offset); });
  });
});
test('rapid navigation serializes phone messages and delivers the latest selection', function() {
  var c = ready({deferMessages: true});
  assert.strictEqual(c.messages.length, 1);
  c.message({REQUEST_ROW_DELTA: 1}); c.message({REQUEST_ROW_DELTA: 1}); c.message({REQUEST_ROW_DELTA: 1});
  assert.strictEqual(c.messages.length, 1);
  c.acknowledgements.shift()();
  assert.strictEqual(c.messages.length, 2);
  assert.strictEqual(c.latest().LINE_INDEX, 3);
  c.acknowledgements.shift()();
});

test('refreshing one cached detail leaves the home cache labeled saved', function() {
  var first = ready(), c = ready({storage: first.storage, scenario: 'loading'});
  c.context.app.requestActive = false;
  c.message({REQUEST_DETAILS: 1});
  c.context.currentDirection().cached = false;
  c.context.currentDirection().observedAt = Date.UTC(2026, 9, 20, 9, 42);
  c.context.sendCurrent(); assert.strictEqual(c.latest().DATA_CACHED, 0);
  c.context.app.detailActive = false; c.context.sendCurrent();
  assert.strictEqual(c.latest().DATA_CACHED, 1);
});
test('refreshing one direction does not renew the opposite direction timestamp', function() {
  var c = ready(), timestamp = c.latest().DATA_UPDATED_AT;
  c.message({REQUEST_DETAILS: 1}); c.advance(121000); c.message({REQUEST_REFRESH: 2});
  assert.ok(c.latest().DATA_UPDATED_AT > timestamp);
  c.message({REQUEST_DIRECTION: 2});
  assert.strictEqual(c.latest().DATA_UPDATED_AT, timestamp);
});

test('navigation cannot turn an offline cached response back into a live one', function() {
  var first = ready(), c = ready({storage: first.storage, scenario: 'network'});
  c.message({REQUEST_ROW_DELTA: 1}); c.message({REQUEST_DETAILS: 1});
  c.message({REQUEST_DIRECTION: 2});
  assert.strictEqual(c.latest().DATA_CACHED, 1);
});
test('an absent optional key module still starts and requests phone configuration', function() {
  var c = ready({scenario: 'key'});
  assert.strictEqual(c.latest().DATA_STATE, 3);
  assert.strictEqual(c.requests.length, 0);
});

test('an optional bundled key still works without changing saved settings', function() {
  var c = ready({scenario: 'key', bundledKey: 'synthetic-bundled-key'});
  assert.strictEqual(c.latest().DATA_STATE, 1);
  assert.strictEqual(c.latest().HOME_COUNT, 3);
  assert.strictEqual(c.requests.length, 1);
  assert.strictEqual(c.context.settings().apiKey, 'synthetic-bundled-key');
  assert.strictEqual(c.storage['pebbleTransit.settings'], undefined);
});
console.log(count + ' companion integration tests passed');
