'use strict';

var transit = require('./transit');
var bundledApiKey = require('./private-key');

var API_ROOT = 'https://external.transitapp.com/v4/public';
var CONFIG_URL = 'https://kvnsr1.github.io/pebble-transit/config/';
var SETTINGS_KEY = 'pebbleTransit.settings';
var CACHE_KEY = 'pebbleTransit.routeCache';
var MODES_KEY = 'pebbleTransit.modes';
var MODES_AT_KEY = 'pebbleTransit.modesAt';
var REFRESH_MS = 60 * 1000;
var MODE_REFRESH_MS = 7 * 24 * 60 * 60 * 1000;

var app = {
  routes: [],
  activeIndex: 0,
  pageStart: 0,
  directions: {},
  lastCoords: null,
  lastRefreshAt: 0,
  lastLiveRefreshAt: 0,
  lastLiveRouteId: '',
  requestActive: false,
  detailActive: false,
  departureIndex: 0,
  stops: [],
  detailLoading: false,
  error: '',
  updatedAt: '--'
};
var tripCache = {};
var liveRefreshTimer = null;

function readJson(key, fallback) {
  try { return JSON.parse(localStorage.getItem(key) || '') || fallback; }
  catch (ignore) { return fallback; }
}

function writeJson(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); }
  catch (ignore) {}
}

function legacyJsonParse(source) {
  var at = 0;
  var ch = ' ';

  function fail(message) { throw new SyntaxError(message + ' at ' + at); }
  function next(expected) {
    if (expected && expected !== ch) { fail("Expected '" + expected + "'"); }
    ch = source.charAt(at);
    at += 1;
    return ch;
  }
  function white() { while (ch && ch <= ' ') { next(); } }
  function stringValue() {
    var result = '';
    var hex;
    var value;
    if (ch !== '"') { fail('Expected string'); }
    while (next()) {
      if (ch === '"') { next(); return result; }
      if (ch === '\\') {
        next();
        if (ch === 'u') {
          value = 0;
          for (var i = 0; i < 4; i += 1) {
            hex = parseInt(next(), 16);
            if (!isFinite(hex)) { break; }
            value = value * 16 + hex;
          }
          result += String.fromCharCode(value);
        } else {
          var escapes = {'"': '"', '\\': '\\', '/': '/', b: '\b', f: '\f',
            n: '\n', r: '\r', t: '\t'};
          if (!Object.prototype.hasOwnProperty.call(escapes, ch)) { fail('Bad escape'); }
          result += escapes[ch];
        }
      } else { result += ch; }
    }
    fail('Bad string');
  }
  function numberValue() {
    var text = '';
    if (ch === '-') { text = '-'; next('-'); }
    while (ch >= '0' && ch <= '9') { text += ch; next(); }
    if (ch === '.') {
      text += '.';
      while (next() && ch >= '0' && ch <= '9') { text += ch; }
    }
    if (ch === 'e' || ch === 'E') {
      text += ch;
      next();
      if (ch === '-' || ch === '+') { text += ch; next(); }
      while (ch >= '0' && ch <= '9') { text += ch; next(); }
    }
    var number = Number(text);
    if (!isFinite(number)) { fail('Bad number'); }
    return number;
  }
  function wordValue() {
    if (ch === 't') { next('t'); next('r'); next('u'); next('e'); return true; }
    if (ch === 'f') { next('f'); next('a'); next('l'); next('s'); next('e'); return false; }
    if (ch === 'n') { next('n'); next('u'); next('l'); next('l'); return null; }
    fail('Unexpected token');
  }
  function arrayValue() {
    var array = [];
    next('[');
    white();
    if (ch === ']') { next(']'); return array; }
    while (ch) {
      array.push(value());
      white();
      if (ch === ']') { next(']'); return array; }
      next(',');
      white();
    }
    fail('Bad array');
  }
  function objectValue() {
    var object = {};
    next('{');
    white();
    if (ch === '}') { next('}'); return object; }
    while (ch) {
      var key = stringValue();
      white();
      next(':');
      object[key] = value();
      white();
      if (ch === '}') { next('}'); return object; }
      next(',');
      white();
    }
    fail('Bad object');
  }
  function value() {
    white();
    if (ch === '{') { return objectValue(); }
    if (ch === '[') { return arrayValue(); }
    if (ch === '"') { return stringValue(); }
    if (ch === '-' || (ch >= '0' && ch <= '9')) { return numberValue(); }
    return wordValue();
  }

  next();
  var result = value();
  white();
  if (ch) { fail('Unexpected trailing input'); }
  return result;
}

function parseApiJson(value) {
  try { return JSON.parse(value); }
  catch (firstError) {
    // PebbleKit JS's legacy ICU build throws on some large valid JSON documents.
    return legacyJsonParse(String(value));
  }
}

function defaultModes() {
  return [
    {key: 'Tram', name: 'Light rail', code: 0, sortOrder: 0},
    {key: 'Metro', name: 'Metro / subway', code: 1, sortOrder: 1},
    {key: 'Rail', name: 'Commuter rail', code: 2, sortOrder: 2},
    {key: 'Bus', name: 'Bus', code: 3, sortOrder: 3},
    {key: 'Ferry', name: 'Ferry', code: 4, sortOrder: 4},
    {key: 'CableCar', name: 'Cable car', code: 5, sortOrder: 5},
    {key: 'Gondola', name: 'Gondola', code: 6, sortOrder: 6},
    {key: 'Funicular', name: 'Funicular', code: 7, sortOrder: 7},
    {key: 'Trolleybus', name: 'Trolleybus', code: 8, sortOrder: 8},
    {key: 'Monorail', name: 'Monorail', code: 9, sortOrder: 9}
  ];
}

function cleanSettings(raw) {
  raw = raw || {};
  var enabledModes = raw.enabledModes || {};
  var favorites = Array.isArray(raw.favorites) ? raw.favorites.filter(function(value) {
    return typeof value === 'string' && value.length < 100;
  }).slice(0, 40) : [];
  return {
    apiKey: typeof raw.apiKey === 'string' ? raw.apiKey.trim() : '',
    enabledModes: enabledModes,
    favorites: favorites,
    radius: Math.max(150, Math.min(1500, Number(raw.radius) || 700))
  };
}

function settings() {
  var result = cleanSettings(readJson(SETTINGS_KEY, {}));
  result.apiKey = result.apiKey || bundledApiKey || '';
  return result;
}

function saveSettings(value) { writeJson(SETTINGS_KEY, cleanSettings(value)); }

function availableModes() {
  return transit.modeCatalog([], readJson(MODES_KEY, defaultModes()));
}

function formatUpdated() {
  var now = new Date();
  var hours = now.getHours();
  var minutes = now.getMinutes();
  var suffix = hours >= 12 ? 'p' : 'a';
  if (hours === 0) { hours = 12; }
  if (hours > 12) { hours -= 12; }
  return hours + ':' + (minutes < 10 ? '0' : '') + minutes + suffix;
}

function send(payload) {
  Pebble.sendAppMessage(payload, function() {}, function(error) {
    console.log('AppMessage failed: ' + JSON.stringify(error));
  });
}

function sendError(message) {
  app.error = message;
  send({IS_LOADING: 0, ERROR_MESSAGE: message.slice(0, 78)});
}

function currentRoute() { return app.routes[app.activeIndex] || null; }

function currentDirection(route) {
  route = route || currentRoute();
  if (!route || !route.directions.length) { return null; }
  var index = app.directions[route.id] || 0;
  if (index >= route.directions.length) { index = 0; }
  app.directions[route.id] = index;
  return route.directions[index];
}

function stopPayload(payload) {
  var keys = [
    ['STOP_1_NAME', 'STOP_1_TIME'], ['STOP_2_NAME', 'STOP_2_TIME'],
    ['STOP_3_NAME', 'STOP_3_TIME'], ['STOP_4_NAME', 'STOP_4_TIME'],
    ['STOP_5_NAME', 'STOP_5_TIME']
  ];
  payload.STOP_COUNT = app.stops.length;
  keys.forEach(function(pair, index) {
    var stop = app.stops[index];
    payload[pair[0]] = stop ? stop.name : '';
    payload[pair[1]] = stop ? stop.time : 0;
  });
  return payload;
}

function homePayload(payload) {
  var pageCount = Math.max(1, Math.ceil(app.routes.length / 3));
  payload.HOME_COUNT = Math.min(3, Math.max(0, app.routes.length - app.pageStart));
  payload.HOME_SELECTED = Math.max(0, app.activeIndex - app.pageStart);
  payload.PAGE_INDEX = Math.floor(app.pageStart / 3);
  payload.PAGE_COUNT = pageCount;
  for (var index = 0; index < 3; index += 1) {
    var route = app.routes[app.pageStart + index];
    var direction = route && currentDirection(route);
    var departure = direction && direction.departures[0];
    var prefix = 'HOME_' + (index + 1) + '_';
    payload[prefix + 'ROUTE'] = route ? route.name : '';
    payload[prefix + 'HEADSIGN'] = direction ? direction.headsign : '';
    payload[prefix + 'STOP'] = direction ? direction.closestStopName : '';
    payload[prefix + 'ETA'] = departure ? departure.departureTime : 0;
    payload[prefix + 'LIVE'] = departure && departure.realTime ? 1 : 0;
    payload[prefix + 'COLOR'] = route ? route.routeColor : 0x333333;
    payload[prefix + 'TEXT_COLOR'] = route ? route.textColor : 0xFFFFFF;
    payload[prefix + 'FAVORITE'] = route && route.favorite ? 1 : 0;
  }
  return payload;
}

function sendCurrent() {
  var route = currentRoute();
  if (!route) {
    sendError(app.error || 'No nearby lines. Check location and mode settings.');
    return;
  }
  var direction = currentDirection(route);
  var departures = direction.departures;
  var directionIndex = app.directions[route.id] || 0;
  var payload = {
    LINE_INDEX: app.activeIndex,
    LINE_COUNT: app.routes.length,
    ROUTE_NAME: route.name,
    ROUTE_LONG_NAME: route.longName,
    HEADSIGN: direction.headsign,
    STOP_NAME: direction.closestStopName,
    MODE_CODE: route.modeCode,
    MODE_NAME: route.modeName,
    ROUTE_COLOR: route.routeColor,
    ROUTE_TEXT_COLOR: route.textColor,
    IS_FAVORITE: route.favorite ? 1 : 0,
    ETA_1: departures[0] ? departures[0].departureTime : 0,
    ETA_2: departures[1] ? departures[1].departureTime : 0,
    ETA_3: departures[2] ? departures[2].departureTime : 0,
    LIVE_1: departures[0] && departures[0].realTime ? 1 : 0,
    LIVE_2: departures[1] && departures[1].realTime ? 1 : 0,
    LIVE_3: departures[2] && departures[2].realTime ? 1 : 0,
    UPDATED_AT: app.updatedAt,
    ERROR_MESSAGE: app.error,
    IS_LOADING: app.requestActive || app.detailLoading ? 1 : 0,
    DIRECTION_INDEX: directionIndex,
    DIRECTION_COUNT: route.directions.length,
    DETAIL_ACTIVE: app.detailActive ? 1 : 0,
    DEPARTURE_INDEX: app.departureIndex
  };
  send(homePayload(stopPayload(payload)));
}

function apiError(status) {
  if (status === 401 || status === 403) { return 'Transit API key was rejected. Open phone settings.'; }
  if (status === 429) { return 'Transit API limit reached. Saved departures shown.'; }
  if (status >= 500) { return 'Transit is temporarily unavailable. Saved departures shown.'; }
  return 'Transit service error (' + status + ').';
}

function requestJson(path, params, onSuccess, onError) {
  var config = settings();
  if (!config.apiKey) { onError('Open phone settings to add the Transit API key.'); return; }
  var query = Object.keys(params || {}).map(function(key) {
    return encodeURIComponent(key) + '=' + encodeURIComponent(params[key]);
  }).join('&');
  var request = new XMLHttpRequest();
  request.open('GET', API_ROOT + path + (query ? '?' + query : ''), true);
  request.setRequestHeader('Accept', 'application/json');
  request.setRequestHeader('Accept-Language', 'en');
  request.setRequestHeader('apiKey', config.apiKey);
  request.timeout = 18000;
  request.onload = function() {
    if (request.status < 200 || request.status >= 300) {
      onError(apiError(request.status));
      return;
    }
    var body;
    try { body = parseApiJson(request.responseText); }
    catch (parseError) {
      console.log('Transit parse failure for ' + path + ' (' + parseError + '): ' +
        String(request.responseText || '').length + ' bytes; tail=' +
        String(request.responseText || '').slice(-120));
      onError('Transit returned unreadable data.');
      return;
    }
    try { onSuccess(body); }
    catch (handlerError) {
      console.log('Transit data handler failure for ' + path + ': ' + handlerError);
      onError('Could not process Transit data.');
    }
  };
  request.onerror = function() { onError('Could not reach Transit. Saved departures shown.'); };
  request.ontimeout = function() { onError('Transit lookup timed out. Saved departures shown.'); };
  request.send();
}

function cacheRoutes() {
  writeJson(CACHE_KEY, {
    routes: app.routes,
    coords: app.lastCoords,
    refreshedAt: app.lastRefreshAt,
    updatedAt: app.updatedAt
  });
}

function loadCachedRoutes() {
  var cache = readJson(CACHE_KEY, null);
  if (!cache || !Array.isArray(cache.routes) || !cache.routes.length) { return; }
  app.routes = cache.routes;
  app.lastCoords = cache.coords || null;
  app.lastRefreshAt = cache.refreshedAt || 0;
  app.updatedAt = cache.updatedAt || '--';
  sendCurrent();
}

function refreshModeCatalog(coords) {
  var refreshedAt = Number(localStorage.getItem(MODES_AT_KEY)) || 0;
  if (Date.now() - refreshedAt < MODE_REFRESH_MS) { return; }
  localStorage.setItem(MODES_AT_KEY, String(Date.now()));
  requestJson('/available_networks', {
    lat: coords.lat,
    lon: coords.lon,
    include_modes: true
  }, function(body) {
    writeJson(MODES_KEY, transit.availableModes(body, availableModes()));
  }, function() { localStorage.removeItem(MODES_AT_KEY); });
}

function loadNearby(coords) {
  var config = settings();
  app.requestActive = true;
  app.error = '';
  if (app.routes.length) { sendCurrent(); }
  requestJson('/nearby_routes', {
    lat: coords.lat,
    lon: coords.lon,
    max_distance: config.radius,
    max_num_departures: 3,
    should_update_realtime: true,
    merge_platform_stops: true,
    include_stops_and_shapes: false,
    stop_detailed: false
  }, function(body) {
    app.requestActive = false;
    app.lastRefreshAt = Date.now();
    app.lastCoords = coords;
    app.updatedAt = formatUpdated();
    var selected = currentRoute();
    var currentId = selected && selected.id;
    app.routes = transit.normalizeRoutes(body, config);
    writeJson(MODES_KEY, transit.modeCatalog(app.routes, availableModes()));
    app.activeIndex = 0;
    if (currentId) {
      app.routes.some(function(route, index) {
        if (route.id === currentId) { app.activeIndex = index; return true; }
        return false;
      });
    }
    if (!app.routes.length) {
      sendError('No enabled transit lines found within ' + config.radius + ' m.');
      return;
    }
    app.pageStart = Math.floor(app.activeIndex / 3) * 3;
    app.lastLiveRefreshAt = Date.now();
    app.lastLiveRouteId = currentRoute().id;
    app.error = '';
    cacheRoutes();
    sendCurrent();
    refreshModeCatalog(coords);
  }, function(message) {
    app.requestActive = false;
    if (app.routes.length) { app.error = message; sendCurrent(); }
    else { sendError(message); }
  });
}

function refreshSelectedLine(force) {
  if (!app.detailActive) { sendCurrent(); return; }
  var route = currentRoute();
  var direction = currentDirection(route);
  if (!route || !direction || !direction.closestStopId || app.requestActive) {
    sendCurrent();
    return;
  }
  if (!force && app.lastLiveRouteId === route.id &&
      Date.now() - app.lastLiveRefreshAt < REFRESH_MS - 2000) {
    sendCurrent();
    return;
  }

  var stopIds = [direction.closestStopId];
  if (app.detailActive) {
    app.stops.forEach(function(stop) {
      if (stop.id && stopIds.indexOf(stop.id) === -1) { stopIds.push(stop.id); }
    });
  }
  var selected = selectedDeparture();
  var selectedTripKey = selected && selected.tripSearchKey;
  app.requestActive = true;
  requestJson('/stop_departures', {
    global_stop_ids: stopIds.join(','),
    max_num_departures: app.detailActive ? 8 : 3,
    should_update_realtime: true,
    merge_platform_stops: true,
    include_stops_and_shapes: false,
    stop_detailed: false
  }, function(body) {
    app.requestActive = false;
    var live = transit.liveDepartures(body, route.id, direction);
    if (live.length) {
      direction.departures = live;
      if (app.detailActive && selectedTripKey) {
        live.some(function(departure, index) {
          if (departure.tripSearchKey === selectedTripKey) {
            app.departureIndex = index;
            return true;
          }
          return false;
        });
      }
    }
    var currentDeparture = selectedDeparture();
    if (app.detailActive && app.stops.length) {
      app.stops = transit.liveStopTimes(body, route.id, direction,
        currentDeparture, app.stops);
    }
    app.lastLiveRefreshAt = Date.now();
    app.lastLiveRouteId = route.id;
    app.updatedAt = formatUpdated();
    app.error = '';
    cacheRoutes();
    sendCurrent();
  }, function(message) {
    app.requestActive = false;
    app.error = message;
    sendCurrent();
  });
}

function scheduleLiveRefresh(force) {
  if (liveRefreshTimer) { clearTimeout(liveRefreshTimer); }
  liveRefreshTimer = setTimeout(function() {
    liveRefreshTimer = null;
    refreshSelectedLine(force);
  }, 650);
}

function refresh(force) {
  if (app.requestActive) { return; }
  if (app.detailActive) {
    refreshSelectedLine(force);
    return;
  }
  if (!force && app.routes.length && Date.now() - app.lastRefreshAt < REFRESH_MS - 2000) {
    sendCurrent();
    return;
  }
  if (!settings().apiKey) {
    sendError('Open phone settings to add the Transit API key.');
    return;
  }
  navigator.geolocation.getCurrentPosition(function(position) {
    console.log('Transit location acquired');
    loadNearby({
      lat: Number(position.coords.latitude).toFixed(6),
      lon: Number(position.coords.longitude).toFixed(6)
    });
  }, function() {
    if (app.lastCoords) { loadNearby(app.lastCoords); }
    else { sendError('Location unavailable. Allow location access on your phone.'); }
  }, {enableHighAccuracy: true, timeout: 15000, maximumAge: 45000});
}

function moveSelection(delta) {
  if (!app.routes.length) { return; }
  app.activeIndex = (app.activeIndex + delta + app.routes.length) % app.routes.length;
  app.pageStart = Math.floor(app.activeIndex / 3) * 3;
  app.departureIndex = 0;
  app.stops = [];
  app.error = '';
  sendCurrent();
}

function changeDirection() {
  var route = currentRoute();
  if (!route) { return; }
  app.directions[route.id] = ((app.directions[route.id] || 0) + 1) % route.directions.length;
  app.detailActive = false;
  app.departureIndex = 0;
  app.stops = [];
  app.error = '';
  sendCurrent();
}

function selectedDeparture() {
  var direction = currentDirection();
  return direction && direction.departures[app.departureIndex] || null;
}

function loadTripDetails(refreshLive) {
  var direction = currentDirection();
  var departure = selectedDeparture();
  app.stops = [];
  if (!direction || !departure || !departure.tripSearchKey) {
    app.detailLoading = false;
    sendCurrent();
    return;
  }
  var cached = tripCache[departure.tripSearchKey];
  if (cached && Date.now() - cached.loadedAt < 5 * 60 * 1000) {
    app.stops = transit.upcomingStops(cached.body, direction, departure, 5);
    app.detailLoading = false;
    sendCurrent();
    if (refreshLive) { scheduleLiveRefresh(true); }
    return;
  }
  app.detailLoading = true;
  sendCurrent();
  requestJson('/trip_details', {
    trip_search_key: departure.tripSearchKey,
    include_continuation: false
  }, function(body) {
    tripCache[departure.tripSearchKey] = {body: body, loadedAt: Date.now()};
    app.stops = transit.upcomingStops(body, direction, departure, 5);
    app.detailLoading = false;
    app.error = '';
    sendCurrent();
    if (refreshLive) { scheduleLiveRefresh(true); }
  }, function(message) {
    app.detailLoading = false;
    app.error = message;
    sendCurrent();
  });
}

function openDetails() {
  if (!currentRoute()) { return; }
  app.detailActive = true;
  app.departureIndex = 0;
  app.error = '';
  loadTripDetails(true);
}

function closeDetails() {
  app.detailActive = false;
  app.detailLoading = false;
  app.stops = [];
  if (liveRefreshTimer) {
    clearTimeout(liveRefreshTimer);
    liveRefreshTimer = null;
  }
  refresh(false);
}

function cycleDeparture(delta) {
  var direction = currentDirection();
  if (!direction || !direction.departures.length) { return; }
  app.departureIndex = (app.departureIndex + delta + direction.departures.length) %
    direction.departures.length;
  loadTripDetails(false);
}

function toggleFavorite() {
  var route = currentRoute();
  if (!route) { return; }
  var config = settings();
  var index = config.favorites.indexOf(route.id);
  if (index === -1) { config.favorites.push(route.id); route.favorite = true; }
  else { config.favorites.splice(index, 1); route.favorite = false; }
  saveSettings(config);
  cacheRoutes();
  sendCurrent();
}

Pebble.addEventListener('ready', function() {
  loadCachedRoutes();
  refresh(true);
});

Pebble.addEventListener('appmessage', function(event) {
  var payload = event.payload;
  if (payload.REQUEST_ROW_DELTA) { moveSelection(Number(payload.REQUEST_ROW_DELTA)); }
  else if (payload.REQUEST_DIRECTION) { changeDirection(); }
  else if (payload.REQUEST_DETAILS) {
    if (Number(payload.REQUEST_DETAILS) === 2) { closeDetails(); }
    else { openDetails(); }
  }
  else if (payload.REQUEST_DEPARTURE) { cycleDeparture(Number(payload.REQUEST_DEPARTURE)); }
  else if (payload.REQUEST_TOGGLE_FAVORITE) { toggleFavorite(); }
  else if (payload.REQUEST_REFRESH) { refresh(payload.REQUEST_REFRESH === 1); }
});

Pebble.addEventListener('showConfiguration', function() {
  var config = settings();
  var state = {
    hasApiKey: Boolean(config.apiKey),
    enabledModes: config.enabledModes,
    favorites: config.favorites,
    radius: config.radius,
    modes: availableModes(),
    lines: app.routes.map(function(route) {
      return {
        id: route.id,
        name: route.name,
        longName: route.longName,
        modeName: route.modeName,
        color: route.routeColor
      };
    })
  };
  Pebble.openURL(CONFIG_URL + '#state=' + encodeURIComponent(JSON.stringify(state)));
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event.response || event.response === 'CANCELLED') { return; }
  try {
    var result = JSON.parse(decodeURIComponent(event.response));
    var current = settings();
    if (result.apiKey) { current.apiKey = String(result.apiKey).trim(); }
    current.enabledModes = result.enabledModes || {};
    current.favorites = Array.isArray(result.favorites) ? result.favorites : current.favorites;
    current.radius = result.radius;
    saveSettings(current);
    app.lastRefreshAt = 0;
    refresh(true);
  } catch (ignore) { sendError('Settings could not be saved.'); }
});
