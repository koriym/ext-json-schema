<?php

declare(strict_types=1);

namespace JsonSchema\Tests;

use JsonSchema\Validator;
use PHPUnit\Framework\TestCase;
use ReflectionClass;

/**
 * The extension must not take over jsonrainbow's JsonSchema\Validator (issue #18)
 */
class JsonRainbowValidatorTest extends TestCase
{
    protected function setUp(): void
    {
        if (!extension_loaded('json_schema')) {
            $this->markTestSkipped('PECL json_schema extension not loaded');
        }
    }

    public function testJsonRainbowValidatorIsAutoloaded(): void
    {
        $class = new ReflectionClass(Validator::class);

        $this->assertFalse($class->isInternal());
        $this->assertStringContainsString('justinrainbow', (string) $class->getFileName());
    }

    public function testJsonRainbowValidatorValidatesObjectSchema(): void
    {
        $data = json_decode('{"users":[{"id":"x"}]}');
        $schema = json_decode('{"type":"object","properties":{"users":{"type":"array","items":{"type":"object","properties":{"id":{"type":"integer"}}}}}}');

        $validator = new Validator();
        $validator->validate($data, $schema);

        $this->assertFalse($validator->isValid());
        $this->assertSame('users[0].id', $validator->getErrors()[0]['property']);

        $valid = json_decode('{"users":[{"id":1}]}');
        $validator = new Validator();
        $validator->validate($valid, $schema);

        $this->assertTrue($validator->isValid());
    }
}
