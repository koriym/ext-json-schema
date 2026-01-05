--TEST--
Array constraints validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test minItems
$schema = ['type' => 'array', 'minItems' => 2];
var_dump(json_schema_validate([1, 2, 3], $schema)); // true
var_dump(json_schema_validate([1], $schema));       // false

// Test maxItems
$schema = ['type' => 'array', 'maxItems' => 3];
var_dump(json_schema_validate([1, 2], $schema));       // true
var_dump(json_schema_validate([1, 2, 3, 4], $schema)); // false

// Test uniqueItems
$schema = ['type' => 'array', 'uniqueItems' => true];
var_dump(json_schema_validate([1, 2, 3], $schema)); // true
var_dump(json_schema_validate([1, 2, 2], $schema)); // false

// Test items schema
$schema = [
    'type' => 'array',
    'items' => ['type' => 'integer']
];
var_dump(json_schema_validate([1, 2, 3], $schema));     // true
var_dump(json_schema_validate([1, 'two', 3], $schema)); // false

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
