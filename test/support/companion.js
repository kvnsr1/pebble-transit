'use strict';
var fs = require('fs');
var path = require('path');
var vm = require('vm');
var fixtures = require('./fixtures');
var transit = require('../../src/pkjs/transit');
var manifest = require('../../package.json');

module.exports = function companion(options) {
  options = options || {};
  var now = options.now || Date.UTC(2026, 9, 20, 9, 41);
  var data = fixtures(now, options.scenario);
  var storage = options.storage || {};
  if (!storage['pebbleTransit.settings'] && options.scenario !== 'key') {
    storage['pebbleTransit.settings'] = JSON.stringify({apiKey: 'fixture-only', favorites: ['fixture:0'],
      enabledModes: {}, radius: 700});
  }
  storage['pebbleTransit.modesAt'] = String(now);
  var handlers = {}, messages = [], requests = [], timers = [], pending = [], acknowledgements = [];
  function Clock() {
    var args = Array.prototype.slice.call(arguments);
    return args.length ? new (Function.prototype.bind.apply(Date, [null].concat(args)))() : new Date(now);
  }
  Clock.now = function() { return now; };
  Clock.UTC = Date.UTC; Clock.parse = Date.parse;
  function XHR() {}
  XHR.prototype.open = function(method, url) { this.url = url; };
  XHR.prototype.setRequestHeader = function() {};
  XHR.prototype.send = function() {
    var self = this;
    requests.push(self.url);
    function respond() {
      if (options.scenario === 'network') { self.onerror(); return; }
      if (options.scenario === 'loading') { return; }
      self.status = options.status || 200;
      var body = /nearby_routes/.test(self.url) ? data.nearby : /stop_departures/.test(self.url) ? data.live :
        /trip_details/.test(self.url) ? data.trip(decodeURIComponent(self.url.match(/trip_search_key=([^&]+)/)[1])) : {networks: []};
      self.responseText = JSON.stringify(body);
      self.onload();
    }
    if (options.defer) { pending.push(respond); } else { respond(); }
  };
  var context = {
    console: {log: function() {}}, Date: Clock, XMLHttpRequest: XHR,
    require: function(name) {
      if (name === './transit') { return transit; }
      throw new Error('Unexpected module: ' + name);
    },
    localStorage: {
      getItem: function(k) { return storage[k] || null; },
      setItem: function(k, v) { storage[k] = String(v); },
      removeItem: function(k) { delete storage[k]; }
    },
    navigator: {geolocation: {getCurrentPosition: function(success, failure) {
      if (options.scenario === 'location') { failure(); }
      else { success({coords: {latitude: 34.06, longitude: -118.31}}); }
    }}},
    Pebble: {
      addEventListener: function(name, handler) { handlers[name] = handler; },
      sendAppMessage: function(payload, success) {
        Object.keys(payload).forEach(function(key) {
          if (manifest.pebble.messageKeys[key] === undefined) { throw new Error('Unknown key ' + key); }
        });
        messages.push(JSON.parse(JSON.stringify(payload)));
        if (success) {
          if (options.deferMessages) { acknowledgements.push(success); } else { success(); }
        }
      },
      openURL: function(url) { context.configurationUrl = url; }
    },
    setTimeout: function(callback, delay) { var timer = {callback: callback, at: now + delay}; timers.push(timer); return timer; },
    clearTimeout: function(timer) { timers = timers.filter(function(item) { return item !== timer; }); }
  };
  // Match the optional webpack context without ever reading a personal key file.
  context.require.context = function() {
    var files = function() { return options.bundledKey || ''; };
    files.keys = function() { return options.bundledKey ? ['./private-key.js'] : []; };
    return files;
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../../src/pkjs/index.js'), 'utf8'), context);
  return {
    context: context, messages: messages, requests: requests, storage: storage, pending: pending,
    acknowledgements: acknowledgements,
    event: function(name, payload) { handlers[name](payload || {}); },
    message: function(payload) { handlers.appmessage({payload: payload}); },
    latest: function() { return messages[messages.length - 1]; },
    advance: function(ms) {
      now += ms;
      var due = timers.filter(function(timer) { return timer.at <= now; });
      timers = timers.filter(function(timer) { return timer.at > now; });
      due.forEach(function(timer) { timer.callback(); });
    }
  };
};
