'use strict';

var MODE_BY_ROUTE_TYPE = {
  0: {code: 0, key: 'Tram', name: 'Light rail'},
  1: {code: 1, key: 'Metro', name: 'Metro'},
  2: {code: 2, key: 'Rail', name: 'Commuter rail'},
  3: {code: 3, key: 'Bus', name: 'Bus'},
  4: {code: 4, key: 'Ferry', name: 'Ferry'},
  5: {code: 5, key: 'CableCar', name: 'Cable car'},
  6: {code: 6, key: 'Gondola', name: 'Gondola'},
  7: {code: 7, key: 'Funicular', name: 'Funicular'},
  11: {code: 8, key: 'Trolleybus', name: 'Trolleybus'},
  12: {code: 9, key: 'Monorail', name: 'Monorail'}
};

function text(value, fallback) {
  if (value === null || value === undefined || value === '') { return fallback || ''; }
  return String(value);
}

function compact(value, maxLength) {
  value = text(value).replace(/\s+/g, ' ').trim();
  return value.length > maxLength ? value.slice(0, Math.max(1, maxLength - 1)) + '…' : value;
}

function compareText(a, b) {
  a = text(a).toLowerCase();
  b = text(b).toLowerCase();
  return a < b ? -1 : (a > b ? 1 : 0);
}

function colorValue(value, fallback) {
  var clean = text(value).replace(/^#/, '');
  return /^[0-9a-fA-F]{6}$/.test(clean) ? parseInt(clean, 16) : fallback;
}

function modeFor(route) {
  var fallback = MODE_BY_ROUTE_TYPE[route.route_type] ||
    {code: 10, key: 'Transit', name: 'Transit'};
  var apiKey = text(route.mode_key, fallback.key);
  var lower = apiKey.toLowerCase();
  var code = fallback.code;
  if (/tram|streetcar|light.?rail/.test(lower)) { code = 0; }
  else if (/metro|subway|underground/.test(lower)) { code = 1; }
  else if (/rail|train|commuter/.test(lower)) { code = 2; }
  else if (/bus|coach/.test(lower)) { code = 3; }
  else if (/ferry|boat/.test(lower)) { code = 4; }
  else if (/cable/.test(lower)) { code = 5; }
  else if (/gondola/.test(lower)) { code = 6; }
  else if (/funicular/.test(lower)) { code = 7; }
  else if (/trolley/.test(lower)) { code = 8; }
  else if (/monorail/.test(lower)) { code = 9; }
  return {
    code: code,
    key: apiKey,
    name: compact(route.mode_name || fallback.name, 20),
    sortOrder: route.mode_sort_order || fallback.code
  };
}

function displayRouteName(route) {
  var displays = [route.compact_display_short_name, route.route_display_short_name];
  for (var i = 0; i < displays.length; i += 1) {
    var elements = displays[i] && displays[i].elements;
    if (elements && elements[1]) { return compact(elements[1], 12); }
  }
  return compact(route.route_short_name || route.route_long_name || 'Line', 12);
}

function canonicalItinerary(merged) {
  var itineraries = merged.itineraries || [];
  for (var i = 0; i < itineraries.length; i += 1) {
    if (itineraries[i].canonical_itinerary) { return itineraries[i]; }
  }
  return itineraries[0] || {};
}

function normalizeDirection(merged) {
  var itinerary = canonicalItinerary(merged);
  var departures = (merged.schedule_items || []).filter(function(item) {
    return !item.is_cancelled && item.departure_time;
  }).slice(0, 3).map(function(item) {
    return {
      departureTime: Number(item.departure_time) || 0,
      scheduledTime: Number(item.scheduled_departure_time || item.departure_time) || 0,
      realTime: Boolean(item.is_real_time),
      tripSearchKey: text(item.trip_search_key),
      itineraryId: text(item.internal_itinerary_id),
      rtTripId: text(item.rt_trip_id)
    };
  });
  return {
    directionId: Number(merged.direction_id) || 0,
    headsign: compact(itinerary.merged_headsign || itinerary.direction_headsign ||
      itinerary.headsign || 'Direction', 34),
    closestStopId: text(merged.closest_stop && merged.closest_stop.global_stop_id),
    closestStopName: compact(merged.closest_stop && merged.closest_stop.stop_name ||
      'Nearest stop', 42),
    departures: departures,
    stops: (itinerary.stops || []).map(function(stop) {
      return {id: text(stop.global_stop_id), name: compact(stop.stop_name, 42)};
    })
  };
}

function normalizeRoutes(body, options) {
  options = options || {};
  var favorites = options.favorites || [];
  var enabledModes = options.enabledModes || {};
  var now = options.now || Math.floor(Date.now() / 1000);
  var routes = (body && body.nearby_routes || []).map(function(route) {
    var mode = modeFor(route);
    var directions = (route.merged_itineraries || []).map(normalizeDirection)
      .filter(function(direction) { return direction.departures.length; });
    return {
      id: text(route.global_route_id),
      name: displayRouteName(route),
      longName: compact(route.route_long_name || route.route_network_name || mode.name, 36),
      networkName: compact(route.route_network_name, 24),
      routeColor: colorValue(route.route_color, 0x29A66A),
      textColor: colorValue(route.route_text_color, 0xFFFFFF),
      modeCode: mode.code,
      modeKey: mode.key,
      modeName: mode.name,
      modeSortOrder: mode.sortOrder,
      favorite: favorites.indexOf(text(route.global_route_id)) !== -1,
      directions: directions
    };
  }).filter(function(route) {
    return route.id && route.directions.length && enabledModes[route.modeKey] !== false;
  });

  routes.sort(function(a, b) {
    if (a.favorite !== b.favorite) { return a.favorite ? -1 : 1; }
    var aTime = a.directions[0].departures[0].departureTime || now + 86400;
    var bTime = b.directions[0].departures[0].departureTime || now + 86400;
    if (aTime !== bTime) { return aTime - bTime; }
    return compareText(a.name, b.name);
  });
  return routes.slice(0, 16);
}

function modeCatalog(routes, existing) {
  var map = {};
  (existing || []).forEach(function(mode) {
    if (mode && mode.key) { map[mode.key] = mode; }
  });
  (routes || []).forEach(function(route) {
    map[route.modeKey] = {
      key: route.modeKey,
      name: route.modeName,
      code: route.modeCode,
      sortOrder: route.modeSortOrder
    };
  });
  return Object.keys(map).map(function(key) { return map[key]; }).sort(function(a, b) {
    return (a.sortOrder || a.code || 0) - (b.sortOrder || b.code || 0) ||
      compareText(a.name, b.name);
  });
}

function availableModes(body, existing) {
  var catalog = existing || [];
  (body && body.networks || []).forEach(function(network) {
    (network.modes || []).forEach(function(mode) {
      catalog.push({
        key: text(mode.mode_key),
        name: compact(mode.mode_name || mode.mode_key, 24),
        code: modeFor({mode_key: mode.mode_key, mode_name: mode.mode_name}).code,
        sortOrder: mode.mode_sort_order || 0
      });
    });
  });
  return modeCatalog([], catalog);
}

function upcomingStops(body, direction, departure, limit) {
  var items = body && body.schedule_items || [];
  if (!items.length || !direction || !departure) { return []; }
  var startIndex = 0;
  for (var i = 0; i < items.length; i += 1) {
    if (items[i].stop && items[i].stop.global_stop_id === direction.closestStopId) {
      startIndex = i;
      break;
    }
  }
  var scheduledAtStart = Number(items[startIndex].departure_time || items[startIndex].arrival_time) ||
    departure.scheduledTime || departure.departureTime;
  var offset = departure.departureTime - scheduledAtStart;
  return items.slice(startIndex, startIndex + (limit || 5)).map(function(item) {
    var scheduledTime = Number(item.departure_time || item.arrival_time) || scheduledAtStart;
    return {
      id: text(item.stop && item.stop.global_stop_id),
      name: compact(item.stop && item.stop.stop_name || 'Stop', 34),
      scheduledTime: scheduledTime,
      time: scheduledTime + offset,
      realTime: Boolean(departure.realTime)
    };
  });
}

function routeAtStop(body, routeId, stopId) {
  var rows = body && body.route_departures || [];
  for (var i = 0; i < rows.length; i += 1) {
    if (text(rows[i].global_route_id) === routeId &&
        (!stopId || text(rows[i].global_stop_id) === stopId)) {
      return rows[i];
    }
  }
  return null;
}

function directionAtStop(route, directionId) {
  var directions = route && route.merged_itineraries || [];
  for (var i = 0; i < directions.length; i += 1) {
    if (Number(directions[i].direction_id) === Number(directionId)) { return directions[i]; }
  }
  return null;
}

function liveDepartures(body, routeId, direction) {
  if (!direction) { return []; }
  var route = routeAtStop(body, routeId, direction.closestStopId);
  var merged = directionAtStop(route, direction.directionId);
  return merged ? normalizeDirection(merged).departures : [];
}

function liveStopTimes(body, routeId, direction, departure, stops) {
  if (!direction || !departure) { return stops || []; }
  return (stops || []).map(function(stop) {
    var route = routeAtStop(body, routeId, stop.id);
    var merged = directionAtStop(route, direction.directionId);
    var items = merged && merged.schedule_items || [];
    var match = null;
    for (var i = 0; i < items.length; i += 1) {
      if ((departure.tripSearchKey && text(items[i].trip_search_key) === departure.tripSearchKey) ||
          (departure.rtTripId && text(items[i].rt_trip_id) === departure.rtTripId)) {
        match = items[i];
        break;
      }
    }
    if (!match) { return stop; }
    return {
      id: stop.id,
      name: stop.name,
      scheduledTime: stop.scheduledTime,
      time: Number(match.departure_time || match.arrival_time) || stop.time,
      realTime: Boolean(match.is_real_time)
    };
  });
}

module.exports = {
  availableModes: availableModes,
  colorValue: colorValue,
  displayRouteName: displayRouteName,
  modeCatalog: modeCatalog,
  modeFor: modeFor,
  liveDepartures: liveDepartures,
  liveStopTimes: liveStopTimes,
  normalizeRoutes: normalizeRoutes,
  upcomingStops: upcomingStops
};
