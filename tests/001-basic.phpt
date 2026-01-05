--TEST--
Basic JSON Schema validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test basic type validation
$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string'],
        'age' => ['type' => 'integer']
    ],
    'required' => ['name']
];

$validData = ['name' => 'John', 'age' => 30];
$invalidData = ['age' => 30];

$validator = new JsonSchema\Validator();

// Test valid data
$result = $validator->validate($validData, $schema);
var_dump($result);
var_dump($validator->isValid());
var_dump(count($validator->getErrors()));

// Test invalid data
$result = $validator->validate($invalidData, $schema);
var_dump($result);
var_dump($validator->isValid());
var_dump(count($validator->getErrors()) > 0);

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(true)
int(0)
bool(false)
bool(false)
bool(true)
OK
