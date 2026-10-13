--TEST--
External $ref resolution with PHP callback
--EXTENSIONS--
json_schema
--FILE--
<?php

// Simulated external schemas (including relative paths)
$externalSchemas = [
    'types.json' => [
        'definitions' => [
            'positiveInteger' => [
                'type' => 'integer',
                'minimum' => 1
            ],
            'email' => [
                'type' => 'string',
                'format' => 'email'
            ]
        ]
    ],
    'user.json' => [
        'type' => 'object',
        'properties' => [
            'name' => ['type' => 'string'],
            'age' => ['$ref' => 'types.json#/definitions/positiveInteger']
        ],
        'required' => ['name', 'age']
    ],
    // Relative path schemas
    '/schemas/v1/common/string-types.json' => [
        'definitions' => [
            'nonEmptyString' => [
                'type' => 'string',
                'minLength' => 1
            ]
        ]
    ],
    '/schemas/v1/models/product.json' => [
        'type' => 'object',
        'properties' => [
            'name' => ['$ref' => '../common/string-types.json#/definitions/nonEmptyString'],
            'price' => ['type' => 'number', 'minimum' => 0]
        ],
        'required' => ['name', 'price']
    ]
];

$validator = new JsonSchema\Validator();

// Set up resolver callback
$validator->setRefResolver(function(string $uri, string $baseUri) use ($externalSchemas) {
    // Simple resolution - just look up in our array
    if (isset($externalSchemas[$uri])) {
        return $externalSchemas[$uri];
    }
    return null;
});

// Test 1: Direct external reference
echo "Test 1: External \$ref to user.json\n";
$schema = ['$ref' => 'user.json'];
$data = ['name' => 'John', 'age' => 30];
$result = $validator->validate($data, $schema);
echo "Valid user: " . ($result ? "PASS" : "FAIL") . "\n";

// Test 2: Invalid data against external schema
echo "\nTest 2: Invalid data (negative age)\n";
$data = ['name' => 'John', 'age' => -5];
$result = $validator->validate($data, $schema);
echo "Invalid age rejected: " . (!$result ? "PASS" : "FAIL") . "\n";
$errors = $validator->getErrors();
if (!empty($errors)) {
    echo "Error: " . $errors[0]['message'] . "\n";
}

// Test 3: External reference with fragment
echo "\nTest 3: External \$ref with fragment\n";
$schema = [
    'type' => 'object',
    'properties' => [
        'count' => ['$ref' => 'types.json#/definitions/positiveInteger']
    ]
];
$data = ['count' => 5];
$result = $validator->validate($data, $schema);
echo "Valid count: " . ($result ? "PASS" : "FAIL") . "\n";

$data = ['count' => 0];
$result = $validator->validate($data, $schema);
echo "Zero count rejected: " . (!$result ? "PASS" : "FAIL") . "\n";

// Test 4: Without resolver, external refs should fail
echo "\nTest 4: Without resolver\n";
$validator2 = new JsonSchema\Validator();
$schema = ['$ref' => 'user.json'];
$data = ['name' => 'John', 'age' => 30];
$result = $validator2->validate($data, $schema);
echo "No resolver returns error: " . (!$result ? "PASS" : "FAIL") . "\n";

// Test 5: setBaseUri
echo "\nTest 5: Base URI\n";
$validator->setBaseUri('/schemas/v1/');
$validator->setRefResolver(function(string $uri, string $baseUri) use ($externalSchemas) {
    echo "Resolver called with baseUri: $baseUri\n";
    if (isset($externalSchemas[$uri])) {
        return $externalSchemas[$uri];
    }
    return null;
});
$schema = ['$ref' => 'user.json'];
$data = ['name' => 'Test', 'age' => 1];
$validator->validate($data, $schema);

// Test 6: Relative path resolution (./foo.json, ../bar.json)
echo "\nTest 6: Relative path resolution\n";
$validator3 = new JsonSchema\Validator();
$validator3->setBaseUri('/schemas/v1/models/');
$validator3->setRefResolver(function(string $uri, string $baseUri) use ($externalSchemas) {
    // Resolve relative paths
    if (str_starts_with($uri, './') || str_starts_with($uri, '../')) {
        // Simple path resolution: combine baseUri with relative path
        $basePath = rtrim($baseUri, '/');
        $parts = explode('/', $basePath);

        $relParts = explode('/', $uri);
        foreach ($relParts as $part) {
            if ($part === '..') {
                array_pop($parts);
            } elseif ($part !== '.' && $part !== '') {
                $parts[] = $part;
            }
        }
        $resolved = implode('/', $parts);
        echo "Resolved: $uri -> $resolved\n";
        return $externalSchemas[$resolved] ?? null;
    }

    // Absolute path
    return $externalSchemas[$uri] ?? null;
});

// Test with schema that uses relative $ref internally
$schema = ['$ref' => '/schemas/v1/models/product.json'];
$data = ['name' => 'Widget', 'price' => 9.99];
$result = $validator3->validate($data, $schema);
echo "Valid product: " . ($result ? "PASS" : "FAIL") . "\n";

// Test invalid data (empty name violates minLength: 1)
$data = ['name' => '', 'price' => 9.99];
$result = $validator3->validate($data, $schema);
echo "Empty name rejected: " . (!$result ? "PASS" : "FAIL") . "\n";

echo "\nAll tests completed!\n";
?>
--EXPECT--
Test 1: External $ref to user.json
Valid user: PASS

Test 2: Invalid data (negative age)
Invalid age rejected: PASS
Error: Value -5 is less than minimum 1

Test 3: External $ref with fragment
Valid count: PASS
Zero count rejected: PASS

Test 4: Without resolver
No resolver returns error: PASS

Test 5: Base URI
Resolver called with baseUri: /schemas/v1/
Resolver called with baseUri: /schemas/v1/
Resolver called with baseUri: /schemas/v1/user.json
Resolver called with baseUri: /schemas/v1/user.json

Test 6: Relative path resolution
Valid product: PASS
Empty name rejected: PASS

All tests completed!
