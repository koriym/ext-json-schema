--TEST--
String constraints validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test minLength
$schema = ['type' => 'string', 'minLength' => 3];
var_dump(json_schema_validate('hello', $schema)); // true
var_dump(json_schema_validate('hi', $schema));    // false

// Test maxLength
$schema = ['type' => 'string', 'maxLength' => 5];
var_dump(json_schema_validate('hello', $schema)); // true
var_dump(json_schema_validate('hello world', $schema)); // false

// Test pattern
$schema = ['type' => 'string', 'pattern' => '^[a-z]+$'];
var_dump(json_schema_validate('hello', $schema));  // true
var_dump(json_schema_validate('Hello', $schema));  // false

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
OK
