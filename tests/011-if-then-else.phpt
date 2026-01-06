--TEST--
if/then/else validation (Draft-07+)
--EXTENSIONS--
json_schema
--FILE--
<?php

$schema = [
    'type' => 'object',
    'if' => [
        'properties' => [
            'type' => ['const' => 'premium']
        ]
    ],
    'then' => [
        'required' => ['discount']
    ],
    'else' => [
        'required' => ['standard']
    ]
];

// Premium user with discount - valid
var_dump(json_schema_validate([
    'type' => 'premium',
    'discount' => 20
], $schema)); // true

// Premium user without discount - invalid
var_dump(json_schema_validate([
    'type' => 'premium'
], $schema)); // false

// Standard user with standard field - valid
var_dump(json_schema_validate([
    'type' => 'basic',
    'standard' => true
], $schema)); // true

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
OK
