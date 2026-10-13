--TEST--
External $ref resolver edge cases
--EXTENSIONS--
json_schema
--FILE--
<?php

$validator = new JsonSchema\Ext\Validator();

// Test 1: Resolver returning null
echo "Test 1: Resolver returning null\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    return null;
});
$schema = ['$ref' => 'nonexistent.json'];
$data = ['foo' => 'bar'];
$result = $validator->validate($data, $schema);
echo "Null resolver result handled: " . (!$result ? "PASS" : "FAIL") . "\n";

// Test 2: Resolver returning invalid non-array value (string)
echo "\nTest 2: Resolver returning string\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    return "invalid string";
});
$result = $validator->validate($data, $schema);
echo "String return handled: " . (!$result ? "PASS" : "FAIL") . "\n";

// Test 3: Resolver returning invalid non-array value (integer)
echo "\nTest 3: Resolver returning integer\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    return 42;
});
$result = $validator->validate($data, $schema);
echo "Integer return handled: " . (!$result ? "PASS" : "FAIL") . "\n";

// Test 4: Resolver returning a boolean schema
echo "\nTest 4: Resolver returning boolean\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    return true;
});
echo "True schema accepts: " . ($validator->validate($data, $schema) ? "PASS" : "FAIL") . "\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    return false;
});
echo "False schema rejects: " . (!$validator->validate($data, $schema) ? "PASS" : "FAIL") . "\n";
echo "Error: " . $validator->getErrors()[0]['message'] . "\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    return true;
});
echo "Fragment into boolean unresolved: " . (!$validator->validate($data, ['$ref' => 'x.json#/a']) ? "PASS" : "FAIL") . "\n";

// Test 5: Resolver throwing exception
echo "\nTest 5: Resolver throwing exception\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    throw new RuntimeException("Resolver error");
});
try {
    $result = $validator->validate($data, $schema);
    echo "Exception handled: " . (!$result ? "PASS" : "FAIL") . "\n";
} catch (Throwable $e) {
    echo "Exception propagated: " . $e->getMessage() . "\n";
}

// Test 6: Circular external refs (A -> B -> A)
echo "\nTest 6: Circular external refs\n";
$circularSchemas = [
    'schema-a.json' => [
        'type' => 'object',
        'properties' => [
            'b' => ['$ref' => 'schema-b.json']
        ]
    ],
    'schema-b.json' => [
        'type' => 'object',
        'properties' => [
            'a' => ['$ref' => 'schema-a.json']
        ]
    ]
];
$validator->setRefResolver(function(string $uri, string $baseUri) use ($circularSchemas) {
    return $circularSchemas[$uri] ?? null;
});
$schema = ['$ref' => 'schema-a.json'];

// Valid nested data should pass (use stdClass for empty objects)
$data = ['b' => ['a' => ['b' => new stdClass()]]];
$result = $validator->validate($data, $schema);
echo "Valid nested data: " . ($result ? "PASS" : "FAIL") . "\n";

// Invalid data (wrong type) should fail
$data = ['b' => 'not an object'];
$result = $validator->validate($data, $schema);
echo "Invalid type rejected: " . (!$result ? "PASS" : "FAIL") . "\n";

// Test 7: Clear resolver with null
echo "\nTest 7: Clear resolver with null\n";
$validator->setRefResolver(null);
$schema = ['$ref' => 'external.json'];
$result = $validator->validate($data, $schema);
echo "Cleared resolver handled: " . (!$result ? "PASS" : "FAIL") . "\n";

echo "\nAll edge case tests completed!\n";
?>
--EXPECT--
Test 1: Resolver returning null
Null resolver result handled: PASS

Test 2: Resolver returning string
String return handled: PASS

Test 3: Resolver returning integer
Integer return handled: PASS

Test 4: Resolver returning boolean
True schema accepts: PASS
False schema rejects: PASS
Error: Schema is false, validation always fails
Fragment into boolean unresolved: PASS

Test 5: Resolver throwing exception
Exception propagated: Resolver error

Test 6: Circular external refs
Valid nested data: PASS
Invalid type rejected: PASS

Test 7: Clear resolver with null
Cleared resolver handled: PASS

All edge case tests completed!
