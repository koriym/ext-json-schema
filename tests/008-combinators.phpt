--TEST--
Combinator keywords (allOf, anyOf, oneOf, not)
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test allOf
$schema = [
    'allOf' => [
        ['type' => 'object'],
        ['required' => ['name']],
        ['properties' => ['name' => ['type' => 'string']]]
    ]
];
var_dump(json_schema_validate(['name' => 'John'], $schema)); // true
var_dump(json_schema_validate(['age' => 30], $schema));      // false

// Test anyOf
$schema = [
    'anyOf' => [
        ['type' => 'string'],
        ['type' => 'integer']
    ]
];
var_dump(json_schema_validate('hello', $schema)); // true
var_dump(json_schema_validate(42, $schema));      // true
var_dump(json_schema_validate(3.14, $schema));    // false

// Test oneOf
$schema = [
    'oneOf' => [
        ['type' => 'integer', 'maximum' => 10],
        ['type' => 'integer', 'minimum' => 20]
    ]
];
var_dump(json_schema_validate(5, $schema));  // true (matches first only)
var_dump(json_schema_validate(25, $schema)); // true (matches second only)
var_dump(json_schema_validate(15, $schema)); // false (matches neither)

// Test not
$schema = ['not' => ['type' => 'string']];
var_dump(json_schema_validate(42, $schema));      // true
var_dump(json_schema_validate('hello', $schema)); // false

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
OK
