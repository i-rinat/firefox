// |reftest| skip-if(release_or_beta) -- Intl.DateTimeFormat-formatRange is not released yet
// Copyright 2019 Google, Inc.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: >
  Throws a RangeError if startDate or endDate are undefined.
info: |
  Intl.DateTimeFormat.prototype.formatRangeToParts ( startDate , endDate )

  1. Let dtf be this value.
  2. If Type(dtf) is not Object, throw a TypeError exception.
  3. If dtf does not have an [[InitializedDateTimeFormat]] internal slot, throw a TypeError exception.
  4. If startDate is undefined or endDate is undefined, throw a RangeError exception.
  5. Let x be ? ToNumber(startDate).
  6. Let y be ? ToNumber(endDate).

features: [Intl.DateTimeFormat-formatRange]
---*/
var dtf = new Intl.DateTimeFormat();

assert.throws(RangeError, function() {
  dtf.formatRangeToParts(); // Not possible to poison this one
}, "no args");

var poison = { valueOf() { throw new Test262Error(); } };

assert.throws(RangeError, function() {
  dtf.formatRangeToParts(undefined, poison);
}, "date/undefined");

assert.throws(RangeError, function() {
  dtf.formatRangeToParts(poison, undefined);
}, "undefined/date");

assert.throws(RangeError, function() {
  dtf.formatRangeToParts(poison);
}, "only one arg");

assert.throws(RangeError, function() {
  dtf.formatRangeToParts(undefined, undefined);
}, "undefined/undefined");

reportCompare(0, 0);
