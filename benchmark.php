<?php
/**
 * Benchmark: PECL json_schema vs jsonrainbow/json-schema
 */

require_once __DIR__ . '/vendor/autoload.php';

$testSuiteDir = __DIR__ . '/JSON-Schema-Test-Suite/tests';
$drafts = ['draft4', 'draft6', 'draft7'];

// Collect all test cases
$testCases = [];
foreach ($drafts as $draft) {
    $draftDir = $testSuiteDir . '/' . $draft;
    if (!is_dir($draftDir)) continue;

    foreach (glob($draftDir . '/*.json') as $file) {
        $content = json_decode(file_get_contents($file), true);
        foreach ($content as $group) {
            $schema = $group['schema'];
            foreach ($group['tests'] as $test) {
                $testCases[] = [
                    'schema' => $schema,
                    'data' => $test['data'],
                    'valid' => $test['valid'],
                ];
            }
        }
    }
}

$totalTests = count($testCases);
echo "Total test cases: $totalTests\n\n";

// Benchmark PECL extension
echo "=== PECL ext-json-schema ===\n";
$start = microtime(true);
$peclPassed = 0;
foreach ($testCases as $case) {
    $result = json_schema_validate($case['data'], $case['schema']);
    if ($result === $case['valid']) {
        $peclPassed++;
    }
}
$peclTime = microtime(true) - $start;
echo sprintf("Time: %.4f sec\n", $peclTime);
echo sprintf("Passed: %d/%d\n\n", $peclPassed, $totalTests);

// Benchmark jsonrainbow/json-schema
echo "=== jsonrainbow/json-schema (PHP) ===\n";
$start = microtime(true);
$phpPassed = 0;
foreach ($testCases as $case) {
    $validator = new JsonSchema\Validator();
    $data = json_decode(json_encode($case['data']));
    $schema = json_decode(json_encode($case['schema']));
    $validator->validate($data, $schema);
    $result = $validator->isValid();
    if ($result === $case['valid']) {
        $phpPassed++;
    }
}
$phpTime = microtime(true) - $start;
echo sprintf("Time: %.4f sec\n", $phpTime);
echo sprintf("Passed: %d/%d\n\n", $phpPassed, $totalTests);

// Comparison
echo "=== Comparison ===\n";
$speedup = $phpTime / $peclTime;
echo sprintf("PECL is %.1fx faster than PHP\n", $speedup);
echo sprintf("Time saved: %.4f sec (%.1f%%)\n", $phpTime - $peclTime, (1 - $peclTime / $phpTime) * 100);
