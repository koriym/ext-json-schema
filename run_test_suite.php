<?php
/**
 * JSON Schema Test Suite Runner
 *
 * Runs the official JSON Schema Test Suite against our PECL extension.
 */

if (!extension_loaded('json_schema')) {
    die("json_schema extension not loaded\n");
}

$testSuiteDir = __DIR__ . '/JSON-Schema-Test-Suite/tests';
$remotesDir = __DIR__ . '/JSON-Schema-Test-Suite/remotes';

if (!is_dir($testSuiteDir)) {
    fwrite(STDERR, "JSON Schema Test Suite not found: $testSuiteDir\n");
    fwrite(STDERR, "Run: git submodule update --init --recursive\n");
    exit(1);
}

// Drafts to test
$drafts = ['draft4', 'draft6', 'draft7'];

// Files to skip (not implemented or require external features)
$skipFiles = [
];

// Individual tests to skip
$skipTests = [
    // Format tests that we don't fully support
    'format.json' => [
        'validation of IRIs',
        'validation of IRI references',
        'validation of JSON pointers',
        'validation of relative JSON pointers',
        'validation of URI references',
        'validation of URI templates',
        'validation of IDN hostnames',
        'validation of IDN e-mail addresses',
        'validation of regexes',
        'validation of duration',
    ],
];

$totalTests = 0;
$passedTests = 0;
$failedTests = 0;
$skippedTests = 0;
$failures = [];
$missingDrafts = [];

/**
 * Convert stdClass objects to arrays recursively for schema.
 * Note: Data objects are kept as-is to preserve object/array distinction.
 */
function schemaToArray($schema) {
    if (is_object($schema)) {
        $result = [];
        foreach ($schema as $key => $value) {
            $result[$key] = schemaToArray($value);
        }
        return $result;
    }
    if (is_array($schema)) {
        return array_map('schemaToArray', $schema);
    }
    return $schema;
}

function metaSchemaForDraft(string $draft): array {
    $types = ['array', 'boolean', 'integer', 'null', 'number', 'object', 'string'];
    $typeSchema = [
        'anyOf' => [
            ['enum' => $types],
            [
                'type' => 'array',
                'items' => ['enum' => $types],
                'minItems' => 1,
                'uniqueItems' => true,
            ],
        ],
    ];
    $schema = [
        'type' => ['object', 'boolean'],
        'properties' => [
            'type' => $typeSchema,
            'minLength' => ['type' => 'integer', 'minimum' => 0],
            'maxLength' => ['type' => 'integer', 'minimum' => 0],
            'minItems' => ['type' => 'integer', 'minimum' => 0],
            'maxItems' => ['type' => 'integer', 'minimum' => 0],
            'minProperties' => ['type' => 'integer', 'minimum' => 0],
            'maxProperties' => ['type' => 'integer', 'minimum' => 0],
        ],
        'additionalProperties' => true,
    ];

    return [
        'type' => ['object', 'boolean'],
        'properties' => [
            'type' => $typeSchema,
            'minLength' => ['type' => 'integer', 'minimum' => 0],
            'maxLength' => ['type' => 'integer', 'minimum' => 0],
            'minItems' => ['type' => 'integer', 'minimum' => 0],
            'maxItems' => ['type' => 'integer', 'minimum' => 0],
            'minProperties' => ['type' => 'integer', 'minimum' => 0],
            'maxProperties' => ['type' => 'integer', 'minimum' => 0],
            'definitions' => [
                'type' => 'object',
                'additionalProperties' => $schema,
            ],
        ],
        'additionalProperties' => true,
    ];
}

function resolveTestSuiteRemote(string $uri, string $baseUri, string $rawRef) {
    global $remotesDir;

    $documentUri = explode('#', $uri, 2)[0];
    if (preg_match('#^http://json-schema\.org/draft-(04|06|07)/schema$#', $documentUri, $matches)) {
        return metaSchemaForDraft('draft' . ltrim($matches[1], '0'));
    }

    $prefix = 'http://localhost:1234/';
    if (!str_starts_with($documentUri, $prefix)) {
        return null;
    }

    $relativePath = substr($documentUri, strlen($prefix));
    $file = $remotesDir . '/' . $relativePath;
    if (!is_file($file)) {
        return null;
    }

    return schemaToArray(json_decode(file_get_contents($file)));
}


foreach ($drafts as $draft) {
    $draftDir = "$testSuiteDir/$draft";
    if (!is_dir($draftDir)) {
        $missingDrafts[] = $draft;
        echo "Missing $draft (directory not found)\n";
        continue;
    }

    echo "\n=== Testing $draft ===\n";

    $files = glob("$draftDir/*.json") ?: [];
    foreach ($files as $file) {
        $filename = basename($file);

        if (in_array($filename, $skipFiles)) {
            echo "  Skipping $filename\n";
            continue;
        }

        $content = file_get_contents($file);
        $testGroups = json_decode($content);

        if (!is_array($testGroups)) {
            echo "  Error reading $filename\n";
            continue;
        }

        foreach ($testGroups as $group) {
            $groupDesc = $group->description ?? 'Unknown';

            // Check if this test group should be skipped
            if (isset($skipTests[$filename])) {
                $skip = false;
                foreach ($skipTests[$filename] as $skipPattern) {
                    if (stripos($groupDesc, $skipPattern) !== false) {
                        $skip = true;
                        break;
                    }
                }
                if ($skip) {
                    $skippedTests += count($group->tests ?? []);
                    continue;
                }
            }

            // Convert schema to array for our extension
            $schema = schemaToArray($group->schema);
            $tests = $group->tests ?? [];

            foreach ($tests as $test) {
                $testDesc = $test->description ?? 'Unknown';

                // Check if individual test should be skipped
                if (isset($skipTests[$filename])) {
                    $skipTest = false;
                    foreach ($skipTests[$filename] as $skipPattern) {
                        if (stripos($testDesc, $skipPattern) !== false) {
                            $skipTest = true;
                            break;
                        }
                    }
                    if ($skipTest) {
                        $skippedTests++;
                        continue;
                    }
                }

                $totalTests++;
                // Keep data as-is (stdClass objects) to preserve object/array distinction
                $data = $test->data;
                $expectedValid = $test->valid;

                $result = json_schema_validate($data, $schema, 1, [
                    'resolver' => 'resolveTestSuiteRemote',
                    'draft' => $draft,
                ]);

                if ($result === $expectedValid) {
                    $passedTests++;
                } else {
                    $failedTests++;
                    $failures[] = [
                        'draft' => $draft,
                        'file' => $filename,
                        'group' => $groupDesc,
                        'test' => $testDesc,
                        'expected' => $expectedValid ? 'valid' : 'invalid',
                        'got' => $result ? 'valid' : 'invalid',
                        'data' => $data,
                        'schema' => $schema,
                    ];
                }
            }
        }
    }
}

echo "\n\n=== Summary ===\n";
echo "Total: $totalTests\n";
echo "Passed: $passedTests\n";
echo "Failed: $failedTests\n";
echo "Skipped: $skippedTests\n";

if ($missingDrafts !== []) {
    echo "Missing drafts: " . implode(', ', $missingDrafts) . "\n";
}

if ($failedTests > 0) {
    $passRate = round(($passedTests / ($passedTests + $failedTests)) * 100, 1);
    echo "Pass rate: $passRate%\n";

    echo "\n=== Failed Tests ===\n";

    // Group failures by file
    $byFile = [];
    foreach ($failures as $f) {
        $key = "{$f['draft']}/{$f['file']}";
        if (!isset($byFile[$key])) {
            $byFile[$key] = [];
        }
        $byFile[$key][] = $f;
    }

    foreach ($byFile as $file => $fileFailures) {
        echo "\n$file (" . count($fileFailures) . " failures):\n";
        foreach (array_slice($fileFailures, 0, 5) as $f) {
            echo "  - {$f['group']}: {$f['test']}\n";
            echo "    Expected: {$f['expected']}, Got: {$f['got']}\n";
        }
        if (count($fileFailures) > 5) {
            echo "  ... and " . (count($fileFailures) - 5) . " more\n";
        }
    }
}

echo "\n";
if ($missingDrafts !== [] || $totalTests === 0) {
    if ($totalTests === 0) {
        fwrite(STDERR, "No JSON Schema Test Suite tests were executed.\n");
        fwrite(STDERR, "Run: git submodule update --init --recursive\n");
    }
    exit(1);
}

exit($failedTests > 0 ? 1 : 0);
