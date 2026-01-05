--TEST--
JsonSchema\Validator class functionality
--EXTENSIONS--
json_schema
--FILE--
<?php

use JsonSchema\Validator;
use JsonSchema\Constraint;

$validator = new Validator();

// Test getCheckMode and setCheckMode
echo "Default check mode: " . $validator->getCheckMode() . "\n";
$validator->setCheckMode(Constraint::CHECK_MODE_COERCE_TYPES);
echo "New check mode: " . $validator->getCheckMode() . "\n";

// Reset for further tests
$validator->setCheckMode(Constraint::CHECK_MODE_NORMAL);

// Test validation with errors
$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string', 'minLength' => 3],
        'age' => ['type' => 'integer', 'minimum' => 0]
    ],
    'required' => ['name', 'age']
];

$invalidData = [
    'name' => 'Jo', // too short
    'age' => -5     // negative
];

$validator->validate($invalidData, $schema);
$errors = $validator->getErrors();

echo "Number of errors: " . count($errors) . "\n";
echo "Is valid: " . ($validator->isValid() ? 'true' : 'false') . "\n";

// Test reset
$validator->reset();
echo "After reset, error count: " . count($validator->getErrors()) . "\n";

echo "OK\n";
?>
--EXPECT--
Default check mode: 1
New check mode: 4
Number of errors: 2
Is valid: false
After reset, error count: 0
OK
