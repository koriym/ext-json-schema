--TEST--
Number constraints validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test minimum
$schema = ['type' => 'number', 'minimum' => 0];
var_dump(json_schema_validate(5, $schema));  // true
var_dump(json_schema_validate(-1, $schema)); // false

// Test maximum
$schema = ['type' => 'number', 'maximum' => 100];
var_dump(json_schema_validate(50, $schema));  // true
var_dump(json_schema_validate(150, $schema)); // false

// Test exclusiveMinimum (Draft-06+ style)
$schema = ['type' => 'number', 'exclusiveMinimum' => 0];
var_dump(json_schema_validate(1, $schema));   // true
var_dump(json_schema_validate(0, $schema));   // false

// Test multipleOf
$schema = ['type' => 'number', 'multipleOf' => 5];
var_dump(json_schema_validate(10, $schema)); // true
var_dump(json_schema_validate(7, $schema));  // false

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
OK
