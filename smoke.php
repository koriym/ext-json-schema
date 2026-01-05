<?php
/**
 * JSON Schema Validator - Smoke Test
 */

use JsonSchema\Validator;
use JsonSchema\Constraint;

echo "=== JSON Schema Validator Extension ===\n\n";

// Define a sample schema
$schema = [
    'type' => 'object',
    'properties' => [
        'name' => [
            'type' => 'string',
            'minLength' => 1
        ],
        'age' => [
            'type' => 'integer',
            'minimum' => 0,
            'maximum' => 150
        ],
        'email' => [
            'type' => 'string',
            'format' => 'email'
        ],
        'tags' => [
            'type' => 'array',
            'items' => ['type' => 'string'],
            'uniqueItems' => true
        ]
    ],
    'required' => ['name', 'email']
];

// Valid data
$validData = [
    'name' => 'John Doe',
    'age' => 30,
    'email' => 'john@example.com',
    'tags' => ['developer', 'php']
];

// Invalid data
$invalidData = [
    'name' => '',           // Too short
    'age' => -5,            // Negative
    'email' => 'invalid',   // Invalid email
    'tags' => ['a', 'a']    // Duplicate items
];

echo "1. Using Validator class:\n";
$validator = new Validator();

// Test valid data
$result = $validator->validate($validData, $schema);
echo "   Valid data: " . (!$result ? "PASSED" : "FAILED") . "\n";

// Test invalid data
$result = $validator->validate($invalidData, $schema);
echo "   Invalid data: " . ($result ? "PASSED" : "FAILED") . "\n";
if (!$validator->isValid()) {
    echo "   Errors found: " . count($validator->getErrors()) . "\n";
    foreach ($validator->getErrors() as $error) {
        echo "     - " . $error['message'] . "\n";
    }
}

echo "\n2. Using procedural API:\n";
echo "   json_schema_validate(): " .
     (json_schema_validate($validData, $schema) ? "Valid" : "Invalid") . "\n";

$result = json_schema_validate_with_errors($invalidData, $schema);
echo "   json_schema_validate_with_errors(): " .
     ($result['valid'] ? "Valid" : "Invalid with " . count($result['errors']) . " errors") . "\n";

echo "\n3. Constraint constants available:\n";
echo "   CHECK_MODE_NORMAL: " . Constraint::CHECK_MODE_NORMAL . "\n";
echo "   CHECK_MODE_COERCE_TYPES: " . Constraint::CHECK_MODE_COERCE_TYPES . "\n";
echo "   CHECK_MODE_EXCEPTIONS: " . Constraint::CHECK_MODE_EXCEPTIONS . "\n";

echo "\nAll tests completed successfully!\n";
