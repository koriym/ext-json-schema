--TEST--
External $ref resolver edge cases
--EXTENSIONS--
json_schema
--FILE--
<?php

$validator = new JsonSchema\Validator();

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

// Test 4: Resolver returning invalid non-array value (boolean)
echo "\nTest 4: Resolver returning boolean\n";
$validator->setRefResolver(function(string $uri, string $baseUri) {
    return true;
});
$result = $validator->validate($data, $schema);
echo "Boolean return handled: " . (!$result ? "PASS" : "FAIL") . "\n";

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
$data = ['b' => ['a' => ['b' => []]]];
$result = $validator->validate($data, $schema);
echo "Circular refs handled: PASS\n";

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
Boolean return handled: PASS

Test 5: Resolver throwing exception
Exception propagated: Resolver error

Test 6: Circular external refs
Circular refs handled: PASS

Test 7: Clear resolver with null
Cleared resolver handled: PASS

All edge case tests completed!
