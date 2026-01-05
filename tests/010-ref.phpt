--TEST--
$ref resolution
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test $ref with definitions
$schema = [
    'definitions' => [
        'address' => [
            'type' => 'object',
            'properties' => [
                'street' => ['type' => 'string'],
                'city' => ['type' => 'string']
            ],
            'required' => ['street', 'city']
        ]
    ],
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string'],
        'address' => ['$ref' => '#/definitions/address']
    ]
];

$validData = [
    'name' => 'John',
    'address' => [
        'street' => '123 Main St',
        'city' => 'New York'
    ]
];

$invalidData = [
    'name' => 'John',
    'address' => [
        'street' => '123 Main St'
        // missing city
    ]
];

var_dump(json_schema_validate($validData, $schema));   // true
var_dump(json_schema_validate($invalidData, $schema)); // false

// Test $ref to root
$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string'],
        'child' => ['$ref' => '#']
    ]
];

$validData = [
    'name' => 'Parent',
    'child' => [
        'name' => 'Child'
    ]
];

var_dump(json_schema_validate($validData, $schema)); // true

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
OK
