'use strict';

var assert = require('assert');
var transit = require('../src/pkjs/transit');

function route(id, name, modeKey, modeName, routeType, departure, color) {
  return {
    global_route_id: id,
    route_short_name: name,
    route_long_name: name + ' Crosstown',
    route_color: color || '29a66a',
    route_text_color: 'ffffff',
    route_type: routeType,
    mode_key: modeKey,
    mode_name: modeName,
    merged_itineraries: [{
      direction_id: 0,
      closest_stop: {global_stop_id: id + ':stop', stop_name: 'Main at First'},
      itineraries: [{
        canonical_itinerary: true,
        direction_headsign: 'Downtown',
        stops: [
          {global_stop_id: id + ':stop', stop_name: 'Main at First'},
          {global_stop_id: id + ':next', stop_name: 'Main at Second'}
        ]
      }],
      schedule_items: [
        {departure_time: departure, scheduled_departure_time: departure - 30,
          is_real_time: true, trip_search_key: id + ':trip1'},
        {departure_time: departure + 600, is_real_time: false, trip_search_key: id + ':trip2'},
        {departure_time: departure + 1200, is_cancelled: true, trip_search_key: id + ':trip3'}
      ]
    }]
  };
}

var body = {nearby_routes: [
  route('BUS:10', '10', 'Bus', 'Bus', 3, 2000, 'e1261c'),
  route('METRO:A', 'A', 'Metro', 'Subway', 1, 2300, '0066cc')
]};

(function normalizesLiveNearbyRoutes() {
  var routes = transit.normalizeRoutes(body, {now: 1000, favorites: []});
  assert.strictEqual(routes.length, 2);
  assert.strictEqual(routes[0].name, '10');
  assert.strictEqual(routes[0].modeCode, 3);
  assert.strictEqual(routes[0].routeColor, 0xe1261c);
  assert.strictEqual(routes[0].directions[0].headsign, 'Downtown');
  assert.strictEqual(routes[0].directions[0].departures.length, 2);
  assert.strictEqual(routes[0].directions[0].departures[0].realTime, true);
})();

(function sortsFavoritesAheadOfSoonerLines() {
  var routes = transit.normalizeRoutes(body, {favorites: ['METRO:A']});
  assert.strictEqual(routes[0].id, 'METRO:A');
  assert.strictEqual(routes[0].favorite, true);
})();

(function filtersUsingTransitModeKeys() {
  var routes = transit.normalizeRoutes(body, {enabledModes: {Bus: false, Metro: true}});
  assert.deepStrictEqual(routes.map(function(item) { return item.id; }), ['METRO:A']);
})();

(function mapsAllCoreGtfsModes() {
  assert.strictEqual(transit.modeFor({route_type: 0}).name, 'Light rail');
  assert.strictEqual(transit.modeFor({route_type: 2}).code, 2);
  assert.strictEqual(transit.modeFor({route_type: 4}).code, 4);
  assert.strictEqual(transit.modeFor({mode_key: 'CableCar', route_type: 5}).code, 5);
})();

(function offsetsStopTimesByRealtimeDeparture() {
  var routes = transit.normalizeRoutes(body, {});
  var direction = routes[0].directions[0];
  var departure = direction.departures[0];
  var trip = {schedule_items: [
    {departure_time: 1970, stop: {global_stop_id: 'BUS:10:stop', stop_name: 'Main at First'}},
    {departure_time: 2090, stop: {global_stop_id: 'BUS:10:next', stop_name: 'Main at Second'}}
  ]};
  var stops = transit.upcomingStops(trip, direction, departure, 5);
  assert.strictEqual(stops.length, 2);
  assert.strictEqual(stops[0].time, 2000);
  assert.strictEqual(stops[1].time, 2120);
})();

(function buildsModeCatalogFromAvailableNetworks() {
  var modes = transit.availableModes({networks: [{modes: [
    {mode_key: 'Ferry', mode_name: 'Ferry', mode_sort_order: 4},
    {mode_key: 'Rail', mode_name: 'Train', mode_sort_order: 2}
  ]}]}, []);
  assert.deepStrictEqual(modes.map(function(mode) { return mode.key; }), ['Rail', 'Ferry']);
})();

console.log('Transit data tests passed');
