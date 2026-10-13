<?php

declare(strict_types=1);

namespace JsonSchema\Tests;

use PHPUnit\Framework\TestCase;

final class ShadowValidationTest extends TestCase
{
    public function testRepresentativeFixturesMatchPurePhpValidator(): void
    {
        $extension = dirname(__DIR__) . '/modules/json_schema.so';
        if (!is_file($extension)) {
            self::markTestSkipped('json_schema extension module is not built');
        }

        $command = [
            PHP_BINARY,
            dirname(__DIR__) . '/shadow_validate.php',
            '--extension=' . $extension,
            __DIR__ . '/fixtures/shadow-validation',
        ];
        $descriptors = [
            0 => ['pipe', 'r'],
            1 => ['pipe', 'w'],
            2 => ['pipe', 'w'],
        ];
        $process = proc_open($command, $descriptors, $pipes);
        self::assertIsResource($process);

        fclose($pipes[0]);
        $stdout = stream_get_contents($pipes[1]);
        $stderr = stream_get_contents($pipes[2]);
        fclose($pipes[1]);
        fclose($pipes[2]);
        $exitCode = proc_close($process);

        self::assertSame(0, $exitCode, (string) $stderr . (string) $stdout);
        self::assertStringContainsString('0 differences', (string) $stdout);
    }
}
