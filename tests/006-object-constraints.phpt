--TEST--
Object constraints validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test required
$schema = [
    'type' => 'object',
    'required' => ['name', 'email']
];
var_dump(json_schema_validate(['name' => 'John', 'email' => 'john@example.com'], $schema)); // true
var_dump(json_schema_validate(['name' => 'John'], $schema)); // false

// Test properties
$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string'],
        'age' => ['type' => 'integer']
    ]
];
var_dump(json_schema_validate(['name' => 'John', 'age' => 30], $schema)); // true
var_dump(json_schema_validate(['name' => 'John', 'age' => 'thirty'], $schema)); // false

// Test additionalProperties: false
$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string']
    ],
    'additionalProperties' => false
];
var_dump(json_schema_validate(['name' => 'John'], $schema)); // true
var_dump(json_schema_validate(['name' => 'John', 'extra' => 'field'], $schema)); // false

// Test minProperties
$schema = ['type' => 'object', 'minProperties' => 2];
var_dump(json_schema_validate(['a' => 1, 'b' => 2], $schema)); // true
var_dump(json_schema_validate(['a' => 1], $schema)); // false

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
