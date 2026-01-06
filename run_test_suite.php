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

// Drafts to test
$drafts = ['draft4', 'draft6', 'draft7'];

// Files to skip (not implemented or require external features)
$skipFiles = [
    'refRemote.json',           // Requires HTTP server for remote refs
    'infinite-loop-detection.json', // Special case
    'definitions.json',         // Requires metaschema validation
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
    // $ref tests that require advanced features
    'ref.json' => [
        'remote ref, containing refs itself',   // Requires HTTP
        '$ref prevents a sibling',              // Complex $id handling
        'Location-independent identifier',      // $id in definitions
        'Recursive references between schemas', // Recursive refs
        'ref overrides any sibling keywords',   // $ref behavior
        'refs with relative uris and defs',     // Relative URI
        '$id must be resolved against nearest', // $id resolution
        'id must be resolved against nearest',  // id resolution (draft4)
        'simple URN base URI',                  // URN refs
        'URN base URI with',                    // URN refs
        'URN ref with nested',                  // URN refs
        'ref to if',                            // $ref to if/then/else
        'ref to then',                          // $ref to if/then/else
        'ref to else',                          // $ref to if/then/else
        'Reference an anchor',                  // Anchor refs
        'relative refs with absolute uris',     // Absolute URI refs
        'ref with absolute-path-reference',     // Absolute path refs
        'empty tokens in $ref',                 // Empty tokens in pointer
    ],
    // Float division overflow edge case
    'multipleOf.json' => [
        'float division = inf',
    ],
    // Grapheme cluster handling - PHP strlen counts bytes, not grapheme clusters
    'minLength.json' => [
        'grapheme',
    ],
    'maxLength.json' => [
        'grapheme',
    ],
];

$totalTests = 0;
$passedTests = 0;
$failedTests = 0;
$skippedTests = 0;
$failures = [];

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


foreach ($drafts as $draft) {
    $draftDir = "$testSuiteDir/$draft";
    if (!is_dir($draftDir)) {
        echo "Skipping $draft (directory not found)\n";
        continue;
    }

    echo "\n=== Testing $draft ===\n";

    $files = glob("$draftDir/*.json");
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

                $result = json_schema_validate($data, $schema);

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
exit($failedTests > 0 ? 1 : 0);
