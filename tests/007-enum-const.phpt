--TEST--
Enum and const validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test enum
$schema = ['enum' => ['red', 'green', 'blue']];
var_dump(json_schema_validate('red', $schema));    // true
var_dump(json_schema_validate('yellow', $schema)); // false

// Test enum with mixed types
$schema = ['enum' => [1, 'one', true, null]];
var_dump(json_schema_validate(1, $schema));       // true
var_dump(json_schema_validate('one', $schema));   // true
var_dump(json_schema_validate(true, $schema));    // true
var_dump(json_schema_validate(null, $schema));    // true
var_dump(json_schema_validate('two', $schema));   // false

// Test const (Draft-06+)
$schema = ['const' => 'exact'];
var_dump(json_schema_validate('exact', $schema)); // true
var_dump(json_schema_validate('other', $schema)); // false

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
OK
