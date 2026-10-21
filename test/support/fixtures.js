'use strict';

// Synthetic Transit v4 responses. No production data or credentials.
module.exports = function fixtures(now, scenario) {
  var epoch = Math.floor(now / 1000);
  var definitions = [
    ['720', 'Santa Monica', 'Wilshire / Western', 'd52b1e', 3, 3, true],
    ['D', 'Downtown LA', 'Wilshire / Vermont', 'a05da5', 1, 7, true],
    ['B', 'North Hollywood', '7th Street / Metro Center', 'db261b', 1, 12, false],
    ['E', 'Downtown Santa Monica', 'Expo / Western', 'edbe00', 0, 18, true],
    ['CE', 'Pacific Surfliner / San Luis Obispo', 'Los Angeles Union Station', '176ba0', 2, 42, false],
    ['F', 'San Francisco Ferry Building', 'Oakland Ferry Terminal', '006f70', 4, 99, false]
  ];
  if (scenario === 'long') {
    definitions[0] = ['DASH Van Nuys', 'Universal City / Studio City via North Hollywood Station',
      'Lankershim Boulevard / Universal Hollywood Drive', 'ffff00', 3, 99, false];
  }
  var routes = definitions.map(function(d, ri) {
    return {
      global_route_id: 'fixture:' + ri, route_short_name: d[0], route_long_name: d[0] + ' Line',
      route_color: d[3], route_text_color: scenario === 'long' ? 'ffff00' : 'ffffff',
      route_type: d[4], merged_itineraries: [0, 1].map(function(direction) {
        return {
          direction_id: direction,
          closest_stop: {global_stop_id: 'fixture:' + ri + ':stop', stop_name: d[2]},
          itineraries: [{canonical_itinerary: true, direction_headsign: direction ? 'East LA / Atlantic' : d[1]}],
          schedule_items: scenario === 'no-departures' ? [] : [0, 1, 2].map(function(di) {
            return {departure_time: epoch + (d[5] + di * 9 + direction * 2) * 60 + 30,
              scheduled_departure_time: epoch + (d[5] + di * 9 + direction * 2) * 60,
              is_real_time: d[6] && di !== 2,
              trip_search_key: 'fixture:' + ri + ':' + direction + ':' + di};
          })
        };
      })
    };
  });
  return {
    nearby: {nearby_routes: scenario === 'empty' ? [] : routes},
    trip: function(key) {
      var parts = key.split(':'), ri = Number(parts[1]), direction = Number(parts[2]), di = Number(parts[3]);
      var names = [definitions[ri][2], 'Wilshire / Normandie', 'Wilshire / Vermont',
        'Westlake / MacArthur Park', '7th Street / Metro Center'];
      if (scenario === 'long') { names[2] = 'Lankershim Boulevard / Universal Hollywood Drive'; }
      return {schedule_items: names.map(function(name, si) {
        return {departure_time: epoch + (definitions[ri][5] + di * 9 + direction * 2 + si * 3) * 60,
          stop: {global_stop_id: 'fixture:' + ri + (si ? ':next' + si : ':stop'), stop_name: name}};
      })};
    },
    live: {route_departures: routes.map(function(route) {
      return {global_route_id: route.global_route_id, global_stop_id: route.merged_itineraries[0].closest_stop.global_stop_id,
        merged_itineraries: route.merged_itineraries};
    })}
  };
};
