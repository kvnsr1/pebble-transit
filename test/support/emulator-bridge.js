'use strict';
// JSON-lines adapter for the real companion, with test-only API fixtures.
var create = require('./companion');
var readline = require('readline');
var app;
readline.createInterface({input: process.stdin}).on('line', function(line) {
  try {
    var input = JSON.parse(line);
    if (input.reset) {
      app = create({now: input.now, scenario: input.scenario});
      app.event('ready');
    } else if (input.event) { app.event(input.event, input.payload); }
    else if (input.message) { app.message(input.message); }
    if (input.advance) { app.advance(input.advance); }
    process.stdout.write(JSON.stringify({payload: app.latest(), requests: app.requests}) + '\n');
  } catch (error) { process.stdout.write(JSON.stringify({error: error.stack}) + '\n'); }
});
