--TEST--
Advanced $ref resolution (Draft4-7 URI/id/anchor semantics)
--EXTENSIONS--
json_schema
--FILE--
<?php
function check(string $label, $data, array $schema, bool $expected, array $options = []): void {
    $result = json_schema_validate($data, $schema, JsonSchema\Constraint::CHECK_MODE_NORMAL, $options);
    echo $label . ': ' . ($result === $expected ? 'PASS' : 'FAIL') . "\n";
}

$options = ['draft' => 'draft7'];

// $ref sibling keywords are ignored in Draft4-7.
$schema = [
    'definitions' => ['reffed' => ['type' => 'array']],
    'properties' => ['foo' => ['$ref' => '#/definitions/reffed', 'maxItems' => 2]],
];
check('ref sibling ignored', ['foo' => [1, 2, 3]], $schema, true, $options);
check('ref target still enforced', ['foo' => 'not-array'], $schema, false, $options);

// Recursive references are valid when the instance terminates.
$schema = [
    'definitions' => [
        'node' => [
            'type' => 'object',
            'properties' => [
                'value' => ['type' => 'integer'],
                'child' => ['$ref' => '#/definitions/node'],
            ],
            'required' => ['value'],
        ],
    ],
    '$ref' => '#/definitions/node',
];
check('recursive ref valid', ['value' => 1, 'child' => ['value' => 2]], $schema, true, $options);
check('recursive ref invalid', ['value' => 1, 'child' => ['value' => 'x']], $schema, false, $options);

// Plain-name anchors and URN document identifiers.
$schema = [
    '$id' => 'urn:uuid:deadbeef-1234-ff00-00ff-4321feebdaed',
    'properties' => ['foo' => ['$ref' => 'urn:uuid:deadbeef-1234-ff00-00ff-4321feebdaed#item']],
    'definitions' => ['item' => ['$id' => '#item', 'type' => 'string']],
];
check('urn anchor valid', ['foo' => 'bar'], $schema, true, $options);
check('urn anchor invalid', ['foo' => 10], $schema, false, $options);

// Absolute-path reference resolves against the current URI authority.
$schema = [
    '$id' => 'http://example.com/ref/absref.json',
    'definitions' => [
        'a' => ['$id' => 'http://example.com/ref/absref/foobar.json', 'type' => 'number'],
        'b' => ['$id' => 'http://example.com/absref/foobar.json', 'type' => 'string'],
    ],
    'allOf' => [['$ref' => '/absref/foobar.json']],
];
check('absolute path ref valid', 'string', $schema, true, $options);
check('absolute path ref invalid', 1, $schema, false, $options);

// Empty JSON Pointer tokens.
$schema = [
    'definitions' => ['' => ['definitions' => ['' => ['type' => 'number']]]],
    'allOf' => [['$ref' => '#/definitions//definitions/']],
];
check('empty pointer token valid', 1, $schema, true, $options);
check('empty pointer token invalid', 'x', $schema, false, $options);

// Empty JSON Pointer token must not fall back to numeric index 0.
$schema = [
    'allOf' => [['type' => 'string']],
    '$ref' => '#/allOf/',
];
check('empty pointer token no numeric fallback', 'ok', $schema, false, $options);

// Root-level empty JSON Pointer token (#/).
$schema = [
    '' => ['type' => 'string'],
    '$ref' => '#/',
];
check('root empty pointer token valid', 'ok', $schema, true, $options);
check('root empty pointer token invalid', 1, $schema, false, $options);

// $id inside annotation values must not pollute the schema URI registry.
$schema = [
    '$id' => 'http://example.com/annotations/root.json',
    'definitions' => [
        'target' => ['$id' => 'target.json', 'type' => 'integer'],
    ],
    'default' => ['$id' => 'target.json', 'type' => 'string'],
    'type' => 'object',
    'properties' => ['value' => ['$ref' => 'target.json']],
];
check('annotation id ignored valid', ['value' => 1], $schema, true, $options);
check('annotation id ignored invalid', ['value' => 'x'], $schema, false, $options);

// Remote resolver with relative refs inside the retrieved schema.
$remoteSchemas = [
    'http://example.com/schemas/root.json' => [
        '$id' => 'http://example.com/schemas/root.json',
        'type' => 'object',
        'properties' => ['name' => ['$ref' => 'defs/string.json']],
    ],
    'http://example.com/schemas/defs/string.json' => ['type' => 'string'],
];
$options['resolver'] = static fn(string $uri, string $baseUri, string $rawRef) => $remoteSchemas[$uri] ?? null;
$schema = ['$ref' => 'http://example.com/schemas/root.json'];
check('remote relative ref valid', ['name' => 'Jane'], $schema, true, $options);
check('remote relative ref invalid', ['name' => 42], $schema, false, $options);

?>
--EXPECT--
ref sibling ignored: PASS
ref target still enforced: PASS
recursive ref valid: PASS
recursive ref invalid: PASS
urn anchor valid: PASS
urn anchor invalid: PASS
absolute path ref valid: PASS
absolute path ref invalid: PASS
empty pointer token valid: PASS
empty pointer token invalid: PASS
empty pointer token no numeric fallback: PASS
root empty pointer token valid: PASS
root empty pointer token invalid: PASS
annotation id ignored valid: PASS
annotation id ignored invalid: PASS
remote relative ref valid: PASS
remote relative ref invalid: PASS
