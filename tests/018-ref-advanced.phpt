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

// ── Draft 7 ──────────────────────────────────────────────────────────────────
echo "=== draft7 ===\n";
$d7 = ['draft' => 'draft7'];

// $ref sibling keywords are ignored in Draft4-7.
$schema = [
    'definitions' => ['reffed' => ['type' => 'array']],
    'properties' => ['foo' => ['$ref' => '#/definitions/reffed', 'maxItems' => 2]],
];
check('ref sibling ignored', ['foo' => [1, 2, 3]], $schema, true, $d7);
check('ref target still enforced', ['foo' => 'not-array'], $schema, false, $d7);

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
check('recursive ref valid', ['value' => 1, 'child' => ['value' => 2]], $schema, true, $d7);
check('recursive ref invalid', ['value' => 1, 'child' => ['value' => 'x']], $schema, false, $d7);

// Plain-name anchors and URN document identifiers.
$schema = [
    '$id' => 'urn:uuid:deadbeef-1234-ff00-00ff-4321feebdaed',
    'properties' => ['foo' => ['$ref' => 'urn:uuid:deadbeef-1234-ff00-00ff-4321feebdaed#item']],
    'definitions' => ['item' => ['$id' => '#item', 'type' => 'string']],
];
check('urn anchor valid', ['foo' => 'bar'], $schema, true, $d7);
check('urn anchor invalid', ['foo' => 10], $schema, false, $d7);

// Absolute-path reference resolves against the current URI authority.
$schema = [
    '$id' => 'http://example.com/ref/absref.json',
    'definitions' => [
        'a' => ['$id' => 'http://example.com/ref/absref/foobar.json', 'type' => 'number'],
        'b' => ['$id' => 'http://example.com/absref/foobar.json', 'type' => 'string'],
    ],
    'allOf' => [['$ref' => '/absref/foobar.json']],
];
check('absolute path ref valid', 'string', $schema, true, $d7);
check('absolute path ref invalid', 1, $schema, false, $d7);

// Empty JSON Pointer tokens.
$schema = [
    'definitions' => ['' => ['definitions' => ['' => ['type' => 'number']]]],
    'allOf' => [['$ref' => '#/definitions//definitions/']],
];
check('empty pointer token valid', 1, $schema, true, $d7);
check('empty pointer token invalid', 'x', $schema, false, $d7);

// Empty JSON Pointer token must not fall back to numeric index 0.
$schema = [
    'allOf' => [['type' => 'string']],
    '$ref' => '#/allOf/',
];
check('empty pointer token no numeric fallback', 'ok', $schema, false, $d7);

// Root-level empty JSON Pointer token (#/).
$schema = [
    '' => ['type' => 'string'],
    '$ref' => '#/',
];
check('root empty pointer token valid', 'ok', $schema, true, $d7);
check('root empty pointer token invalid', 1, $schema, false, $d7);

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
check('annotation id ignored valid', ['value' => 1], $schema, true, $d7);
check('annotation id ignored invalid', ['value' => 'x'], $schema, false, $d7);

// Remote resolver with relative refs inside the retrieved schema.
$remoteSchemas = [
    'http://example.com/schemas/root.json' => [
        '$id' => 'http://example.com/schemas/root.json',
        'type' => 'object',
        'properties' => ['name' => ['$ref' => 'defs/string.json']],
    ],
    'http://example.com/schemas/defs/string.json' => ['type' => 'string'],
];
$d7['resolver'] = static fn(string $uri, string $baseUri, string $rawRef) => $remoteSchemas[$uri] ?? null;
$schema = ['$ref' => 'http://example.com/schemas/root.json'];
check('remote relative ref valid', ['name' => 'Jane'], $schema, true, $d7);
check('remote relative ref invalid', ['name' => 42], $schema, false, $d7);

// ── Draft 4 ──────────────────────────────────────────────────────────────────
echo "=== draft4 ===\n";
$d4 = ['draft' => 'draft4'];

// $ref sibling keywords are also ignored in Draft4.
$schema = [
    'definitions' => ['reffed' => ['type' => 'array']],
    'properties' => ['foo' => ['$ref' => '#/definitions/reffed', 'maxItems' => 2]],
];
check('d4 ref sibling ignored', ['foo' => [1, 2, 3]], $schema, true, $d4);
check('d4 ref target still enforced', ['foo' => 'not-array'], $schema, false, $d4);

// Draft4 uses `id` (not `$id`) for base URI changes.
$schema = [
    'id' => 'http://example.com/d4/root.json',
    'definitions' => [
        'str' => ['id' => 'str.json', 'type' => 'string'],
    ],
    'properties' => ['name' => ['$ref' => 'str.json']],
    'type' => 'object',
];
check('d4 id base uri ref valid', ['name' => 'hello'], $schema, true, $d4);
check('d4 id base uri ref invalid', ['name' => 42], $schema, false, $d4);

// Draft4 plain-name anchor via `id` with hash fragment.
$schema = [
    'id' => 'urn:uuid:aabbccdd-0000-1111-2222-333344445555',
    'properties' => ['val' => ['$ref' => 'urn:uuid:aabbccdd-0000-1111-2222-333344445555#num']],
    'definitions' => ['num' => ['id' => '#num', 'type' => 'number']],
];
check('d4 urn anchor valid', ['val' => 3.14], $schema, true, $d4);
check('d4 urn anchor invalid', ['val' => 'pi'], $schema, false, $d4);

// Recursive refs work the same way in Draft4.
$schema = [
    'id' => 'http://example.com/d4/tree.json',
    'definitions' => [
        'node' => [
            'type' => 'object',
            'properties' => [
                'v' => ['type' => 'integer'],
                'left' => ['$ref' => '#/definitions/node'],
                'right' => ['$ref' => '#/definitions/node'],
            ],
            'required' => ['v'],
        ],
    ],
    '$ref' => '#/definitions/node',
];
check('d4 recursive ref valid', ['v' => 1, 'left' => ['v' => 2], 'right' => ['v' => 3]], $schema, true, $d4);
check('d4 recursive ref invalid', ['v' => 'x'], $schema, false, $d4);

// ── Auto-detection via $schema ────────────────────────────────────────────────
echo "=== auto-detect ===\n";

// Auto-detect draft7 from $schema field; no options needed.
$schema = [
    '$schema' => 'http://json-schema.org/draft-07/schema#',
    '$id' => 'http://example.com/auto/d7.json',
    'definitions' => ['str' => ['$id' => 'types/str.json', 'type' => 'string']],
    'properties' => ['x' => ['$ref' => 'types/str.json']],
    'type' => 'object',
];
check('auto d7 id ref valid', ['x' => 'ok'], $schema, true);
check('auto d7 id ref invalid', ['x' => 99], $schema, false);

// Auto-detect draft4 from $schema field.
$schema = [
    '$schema' => 'http://json-schema.org/draft-04/schema#',
    'id' => 'http://example.com/auto/d4.json',
    'definitions' => ['num' => ['id' => 'types/num.json', 'type' => 'number']],
    'properties' => ['n' => ['$ref' => 'types/num.json']],
    'type' => 'object',
];
check('auto d4 id ref valid', ['n' => 1.5], $schema, true);
check('auto d4 id ref invalid', ['n' => 'one'], $schema, false);

?>
--EXPECT--
=== draft7 ===
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
=== draft4 ===
d4 ref sibling ignored: PASS
d4 ref target still enforced: PASS
d4 id base uri ref valid: PASS
d4 id base uri ref invalid: PASS
d4 urn anchor valid: PASS
d4 urn anchor invalid: PASS
d4 recursive ref valid: PASS
d4 recursive ref invalid: PASS
=== auto-detect ===
auto d7 id ref valid: PASS
auto d7 id ref invalid: PASS
auto d4 id ref valid: PASS
auto d4 id ref invalid: PASS
