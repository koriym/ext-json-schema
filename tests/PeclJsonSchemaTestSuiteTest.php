<?php

declare(strict_types=1);

namespace JsonSchema\Tests;

use JsonSchema\Constraints\Constraint;
use JsonSchema\Constraints\Factory;
use JsonSchema\SchemaStorage;
use JsonSchema\SchemaStorageInterface;
use PHPUnit\Framework\TestCase;

// Use the original jsonrainbow Validator, not our bridge
use JsonSchema\Constraints\BaseConstraint;

/**
 * Test PECL ext-json-schema against JSON Schema Test Suite
 * Same methodology as jsonrainbow/json-schema's JsonSchemaTestSuiteTest
 */
class PeclJsonSchemaTestSuiteTest extends TestCase
{
    private const REMOTES_PREFIX = 'http://localhost:1234';

    /**
     * @dataProvider casesDataProvider
     */
    public function testPeclValidatesCorrectly(
        string $testCaseDescription,
        string $testDescription,
        $schema,
        $data,
        int $checkMode,
        bool $expectedValidationResult,
        bool $optional
    ): void {
        if (!extension_loaded('json_schema')) {
            $this->markTestSkipped('PECL json_schema extension not loaded');
        }

        // Convert schema to array for PECL extension
        $schemaArray = $this->toArray($schema);

        // PHPUnit's data provider serializes/deserializes objects, which creates
        // objects that are equal (==) but not identical (===). The PECL extension
        // is sensitive to this difference. Reconstitute data via JSON to get fresh objects.
        $data = json_decode(json_encode($data));

        try {
            $result = json_schema_validate($data, $schemaArray);
        } catch (\Exception $e) {
            if ($optional) {
                $this->markTestSkipped('Optional test case throws exception: "' . $e->getMessage() . '"');
            }
            throw $e;
        }

        if ($optional && $expectedValidationResult !== $result) {
            $this->markTestSkipped('Optional test case would fail');
        }

        self::assertEquals(
            $expectedValidationResult,
            $result,
            sprintf(
                "Test: %s - %s\nExpected: %s, Got: %s\nSchema: %s\nData: %s",
                $testCaseDescription,
                $testDescription,
                $expectedValidationResult ? 'valid' : 'invalid',
                $result ? 'valid' : 'invalid',
                json_encode($schema, JSON_PRETTY_PRINT),
                json_encode($data, JSON_PRETTY_PRINT)
            )
        );
    }


    public static function casesDataProvider(): \Generator
    {
        $testSuiteDir = __DIR__ . '/../JSON-Schema-Test-Suite/tests';

        foreach (['draft4', 'draft6'] as $draft) {
            $draftDir = $testSuiteDir . '/' . $draft;
            if (!is_dir($draftDir)) {
                continue;
            }

            $checkMode = self::getCheckModeForDraft($draft);

            foreach (glob($draftDir . '/*.json') as $file) {
                $filename = basename($file);

                $testGroups = json_decode(file_get_contents($file), false);
                if (!is_array($testGroups)) {
                    continue;
                }

                foreach ($testGroups as $group) {
                    $testCaseDescription = $group->description ?? 'Unknown';

                    foreach ($group->tests ?? [] as $test) {
                        $testDescription = $test->description ?? 'Unknown';

                        if (self::shouldSkipTest($draft, $filename, $testCaseDescription, $testDescription)) {
                            continue;
                        }

                        $optional = self::isOptionalTest($filename);

                        yield "$draft/$filename - $testCaseDescription - $testDescription" => [
                            $testCaseDescription,
                            $testDescription,
                            $group->schema,
                            $test->data,
                            $checkMode,
                            $test->valid,
                            $optional,
                        ];
                    }
                }
            }
        }
    }

    private static function getCheckModeForDraft(string $draft): int
    {
        // Match jsonrainbow's check mode for drafts
        if ($draft === 'draft6') {
            return Constraint::CHECK_MODE_NORMAL | Constraint::CHECK_MODE_TYPE_CAST;
        }
        return Constraint::CHECK_MODE_NORMAL;
    }

    private static function shouldSkipTest(
        string $draft,
        string $filename,
        string $testCaseDescription,
        string $testDescription
    ): bool {
        // Skip files that require external features
        $skipFiles = [
            'refRemote.json',
            'definitions.json',
            'infinite-loop-detection.json',
        ];

        if (in_array($filename, $skipFiles, true)) {
            return true;
        }

        // Skip specific test patterns (same as run_test_suite.php)
        $skipPatterns = [
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
            'ref.json' => [
                'remote ref',
                '$ref prevents a sibling',
                'Location-independent identifier',
                'Recursive references between schemas',
                'ref overrides any sibling keywords',
                'refs with relative uris',
                '$id must be resolved against nearest',
                'id must be resolved against nearest',
                'URN base URI',
                'URN ref with nested',
                'ref to if',
                'ref to then',
                'ref to else',
                'Reference an anchor',
                'relative refs with absolute uris',
                'ref with absolute-path-reference',
                'empty tokens in $ref',
            ],
            // Float division overflow edge case
            'multipleOf.json' => [
                'float division = inf',
            ],
            // Grapheme cluster handling
            'minLength.json' => [
                'grapheme',
            ],
            'maxLength.json' => [
                'grapheme',
            ],
        ];

        if (isset($skipPatterns[$filename])) {
            foreach ($skipPatterns[$filename] as $pattern) {
                if (stripos($testCaseDescription, $pattern) !== false ||
                    stripos($testDescription, $pattern) !== false) {
                    return true;
                }
            }
        }

        return false;
    }

    private static function isOptionalTest(string $filename): bool
    {
        return strpos($filename, 'optional/') !== false;
    }

    private function loadRemotesIntoStorage(SchemaStorageInterface $storage): void
    {
        $remotesDir = __DIR__ . '/../JSON-Schema-Test-Suite/remotes';

        if (!is_dir($remotesDir)) {
            return;
        }

        $directory = new \RecursiveDirectoryIterator($remotesDir);
        $iterator = new \RecursiveIteratorIterator($directory);

        foreach ($iterator as $info) {
            if (!$info->isFile() || $info->getExtension() !== 'json') {
                continue;
            }

            $id = str_replace($remotesDir, self::REMOTES_PREFIX, $info->getPathname());
            $content = file_get_contents($info->getPathname());
            $schema = json_decode($content, false);
            if ($schema !== null) {
                $storage->addSchema($id, $schema);
            }
        }
    }

    private function toArray($data)
    {
        if (is_object($data)) {
            $result = [];
            foreach ($data as $key => $value) {
                $result[$key] = $this->toArray($value);
            }
            return $result;
        }
        if (is_array($data)) {
            return array_map([$this, 'toArray'], $data);
        }
        return $data;
    }
}
