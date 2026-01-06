--TEST--
Procedural API functions
--EXTENSIONS--
json_schema
--FILE--
<?php

$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string']
    ],
    'required' => ['name']
];

// Test json_schema_validate function
$validData = ['name' => 'John'];
$invalidData = ['age' => 30];

var_dump(json_schema_validate($validData, $schema));
var_dump(json_schema_validate($invalidData, $schema));

// Test json_schema_validate_with_errors function
$result = json_schema_validate_with_errors($validData, $schema);
echo "Valid result:\n";
echo "  valid: " . ($result['valid'] ? 'true' : 'false') . "\n";
echo "  errors: " . count($result['errors']) . "\n";

$result = json_schema_validate_with_errors($invalidData, $schema);
echo "Invalid result:\n";
echo "  valid: " . ($result['valid'] ? 'true' : 'false') . "\n";
echo "  errors: " . count($result['errors']) . "\n";
echo "  first error message: " . $result['errors'][0]['message'] . "\n";

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
Valid result:
  valid: true
  errors: 0
Invalid result:
  valid: false
  errors: 1
  first error message: Required property 'name' is missing
OK
