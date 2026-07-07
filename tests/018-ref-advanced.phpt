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

foreach (['draft4', 'draft6', 'draft7'] as $draft) {
    $options = ['draft' => $draft];
    // Draft-04 uses "id"; Draft-06/07 use "$id".
    $idKey = $draft === 'draft4' ? 'id' : '$id';

    // $ref sibling keywords are ignored in Draft4-7.
    $schema = [
        'definitions' => ['reffed' => ['type' => 'array']],
        'properties' => ['foo' => ['$ref' => '#/definitions/reffed', 'maxItems' => 2]],
    ];
    check("[$draft] ref sibling ignored", ['foo' => [1, 2, 3]], $schema, true, $options);
    check("[$draft] ref target still enforced", ['foo' => 'not-array'], $schema, false, $options);

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
    check("[$draft] recursive ref valid", ['value' => 1, 'child' => ['value' => 2]], $schema, true, $options);
    check("[$draft] recursive ref invalid", ['value' => 1, 'child' => ['value' => 'x']], $schema, false, $options);

    // Plain-name anchors and URN document identifiers.
    $schema = [
        $idKey => 'urn:uuid:deadbeef-1234-ff00-00ff-4321feebdaed',
        'properties' => ['foo' => ['$ref' => 'urn:uuid:deadbeef-1234-ff00-00ff-4321feebdaed#item']],
        'definitions' => ['item' => [$idKey => '#item', 'type' => 'string']],
    ];
    check("[$draft] urn anchor valid", ['foo' => 'bar'], $schema, true, $options);
    check("[$draft] urn anchor invalid", ['foo' => 10], $schema, false, $options);

    // Absolute-path reference resolves against the current URI authority.
    $schema = [
        $idKey => 'http://example.com/ref/absref.json',
        'definitions' => [
            'a' => [$idKey => 'http://example.com/ref/absref/foobar.json', 'type' => 'number'],
            'b' => [$idKey => 'http://example.com/absref/foobar.json', 'type' => 'string'],
        ],
        'allOf' => [['$ref' => '/absref/foobar.json']],
    ];
    check("[$draft] absolute path ref valid", 'string', $schema, true, $options);
    check("[$draft] absolute path ref invalid", 1, $schema, false, $options);

    // Empty JSON Pointer tokens.
    $schema = [
        'definitions' => ['' => ['definitions' => ['' => ['type' => 'number']]]],
        'allOf' => [['$ref' => '#/definitions//definitions/']],
    ];
    check("[$draft] empty pointer token valid", 1, $schema, true, $options);
    check("[$draft] empty pointer token invalid", 'x', $schema, false, $options);

    // Empty JSON Pointer token must not fall back to numeric index 0.
    $schema = [
        'allOf' => [['type' => 'string']],
        '$ref' => '#/allOf/',
    ];
    check("[$draft] empty pointer token no numeric fallback", 'ok', $schema, false, $options);

    // Root-level empty JSON Pointer token (#/).
    $schema = [
        '' => ['type' => 'string'],
        '$ref' => '#/',
    ];
    check("[$draft] root empty pointer token valid", 'ok', $schema, true, $options);
    check("[$draft] root empty pointer token invalid", 1, $schema, false, $options);

    // $id inside annotation values must not pollute the schema URI registry.
    $schema = [
        $idKey => 'http://example.com/annotations/root.json',
        'definitions' => [
            'target' => [$idKey => 'target.json', 'type' => 'integer'],
        ],
        'default' => [$idKey => 'target.json', 'type' => 'string'],
        'type' => 'object',
        'properties' => ['value' => ['$ref' => 'target.json']],
    ];
    check("[$draft] annotation id ignored valid", ['value' => 1], $schema, true, $options);
    check("[$draft] annotation id ignored invalid", ['value' => 'x'], $schema, false, $options);

    // Network-path reference (RFC 3986): //host/path inherits scheme from base.
    $netPathSchemas = [
        'http://other.example.com/netref.json' => ['type' => 'integer'],
    ];
    $options['resolver'] = static fn(string $uri, string $baseUri, string $rawRef) => $netPathSchemas[$uri] ?? null;
    $schema = [
        $idKey => 'http://example.net/root.json',
        'properties' => ['value' => ['$ref' => '//other.example.com/netref.json']],
    ];
    check("[$draft] network-path ref valid", ['value' => 42], $schema, true, $options);
    check("[$draft] network-path ref invalid", ['value' => 'x'], $schema, false, $options);

    // Query-only reference (RFC 3986): ?query replaces the query on the base URI.
    $querySchemas = [
        'http://example.net/root.json?v=1' => ['type' => 'string'],
    ];
    $options['resolver'] = static fn(string $uri, string $baseUri, string $rawRef) => $querySchemas[$uri] ?? null;
    $schema = [
        $idKey => 'http://example.net/root.json',
        'properties' => ['value' => ['$ref' => '?v=1']],
    ];
    check("[$draft] query-only ref valid", ['value' => 'ok'], $schema, true, $options);
    check("[$draft] query-only ref invalid", ['value' => 5], $schema, false, $options);

    // Remote resolver with relative refs inside the retrieved schema.
    $remoteSchemas = [
        'http://example.com/schemas/root.json' => [
            $idKey => 'http://example.com/schemas/root.json',
            'type' => 'object',
            'properties' => ['name' => ['$ref' => 'defs/string.json']],
        ],
        'http://example.com/schemas/defs/string.json' => ['type' => 'string'],
    ];
    $options['resolver'] = static fn(string $uri, string $baseUri, string $rawRef) => $remoteSchemas[$uri] ?? null;
    $schema = ['$ref' => 'http://example.com/schemas/root.json'];
    check("[$draft] remote relative ref valid", ['name' => 'Jane'], $schema, true, $options);
    check("[$draft] remote relative ref invalid", ['name' => 42], $schema, false, $options);
}

?>
--EXPECT--
[draft4] ref sibling ignored: PASS
[draft4] ref target still enforced: PASS
[draft4] recursive ref valid: PASS
[draft4] recursive ref invalid: PASS
[draft4] urn anchor valid: PASS
[draft4] urn anchor invalid: PASS
[draft4] absolute path ref valid: PASS
[draft4] absolute path ref invalid: PASS
[draft4] empty pointer token valid: PASS
[draft4] empty pointer token invalid: PASS
[draft4] empty pointer token no numeric fallback: PASS
[draft4] root empty pointer token valid: PASS
[draft4] root empty pointer token invalid: PASS
[draft4] annotation id ignored valid: PASS
[draft4] annotation id ignored invalid: PASS
[draft4] network-path ref valid: PASS
[draft4] network-path ref invalid: PASS
[draft4] query-only ref valid: PASS
[draft4] query-only ref invalid: PASS
[draft4] remote relative ref valid: PASS
[draft4] remote relative ref invalid: PASS
[draft6] ref sibling ignored: PASS
[draft6] ref target still enforced: PASS
[draft6] recursive ref valid: PASS
[draft6] recursive ref invalid: PASS
[draft6] urn anchor valid: PASS
[draft6] urn anchor invalid: PASS
[draft6] absolute path ref valid: PASS
[draft6] absolute path ref invalid: PASS
[draft6] empty pointer token valid: PASS
[draft6] empty pointer token invalid: PASS
[draft6] empty pointer token no numeric fallback: PASS
[draft6] root empty pointer token valid: PASS
[draft6] root empty pointer token invalid: PASS
[draft6] annotation id ignored valid: PASS
[draft6] annotation id ignored invalid: PASS
[draft6] network-path ref valid: PASS
[draft6] network-path ref invalid: PASS
[draft6] query-only ref valid: PASS
[draft6] query-only ref invalid: PASS
[draft6] remote relative ref valid: PASS
[draft6] remote relative ref invalid: PASS
[draft7] ref sibling ignored: PASS
[draft7] ref target still enforced: PASS
[draft7] recursive ref valid: PASS
[draft7] recursive ref invalid: PASS
[draft7] urn anchor valid: PASS
[draft7] urn anchor invalid: PASS
[draft7] absolute path ref valid: PASS
[draft7] absolute path ref invalid: PASS
[draft7] empty pointer token valid: PASS
[draft7] empty pointer token invalid: PASS
[draft7] empty pointer token no numeric fallback: PASS
[draft7] root empty pointer token valid: PASS
[draft7] root empty pointer token invalid: PASS
[draft7] annotation id ignored valid: PASS
[draft7] annotation id ignored invalid: PASS
[draft7] network-path ref valid: PASS
[draft7] network-path ref invalid: PASS
[draft7] query-only ref valid: PASS
[draft7] query-only ref invalid: PASS
[draft7] remote relative ref valid: PASS
[draft7] remote relative ref invalid: PASS
