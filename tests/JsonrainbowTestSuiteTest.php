<?php

declare(strict_types=1);

namespace JsonSchema\Tests;

use JsonSchema\Constraints\Constraint;
use JsonSchema\Constraints\Factory;
use JsonSchema\SchemaStorage;
use JsonSchema\SchemaStorageInterface;
// Note: JsonSchema\Validator comes from justinrainbow/json-schema package
use PHPUnit\Framework\TestCase;

/**
 * Test jsonrainbow/json-schema against JSON Schema Test Suite
 * Same methodology as their JsonSchemaTestSuiteTest
 */
class JsonrainbowTestSuiteTest extends TestCase
{
    private const REMOTES_PREFIX = 'http://localhost:1234';

    /**
     * @dataProvider casesDataProvider
     */
    public function testJsonrainbowValidatesCorrectly(
        string $testCaseDescription,
        string $testDescription,
        $schema,
        $data,
        int $checkMode,
        bool $expectedValidationResult,
        bool $optional
    ): void {
        // Skip if PECL extension is loaded (it overrides JsonSchema\Validator)
        if (extension_loaded('json_schema')) {
            $this->markTestSkipped('Cannot test jsonrainbow when PECL json_schema is loaded - namespace conflict');
        }

        $schemaStorage = new SchemaStorage();
        $id = is_object($schema) && property_exists($schema, 'id')
            ? $schema->id
            : SchemaStorage::INTERNAL_PROVIDED_SCHEMA_URI;
        $schemaStorage->addSchema($id, $schema);
        $this->loadRemotesIntoStorage($schemaStorage);
        $validator = new \JsonSchema\Validator(new Factory($schemaStorage));

        try {
            $validator->validate($data, $schema, $checkMode);
        } catch (\Exception $e) {
            if ($optional) {
                $this->markTestSkipped('Optional test case throws exception: "' . $e->getMessage() . '"');
            }
            throw $e;
        }

        $isValid = count($validator->getErrors()) === 0;

        if ($optional && $expectedValidationResult !== $isValid) {
            $this->markTestSkipped('Optional test case would fail');
        }

        self::assertEquals(
            $expectedValidationResult,
            $isValid,
            $expectedValidationResult
                ? print_r($validator->getErrors(), true)
                : 'Validator returned valid but the testcase indicates it is invalid'
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
        $skipFiles = [
            'refRemote.json',
            'definitions.json',
            'infinite-loop-detection.json',
        ];

        if (in_array($filename, $skipFiles, true)) {
            return true;
        }

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
}
