<?php

declare(strict_types=1);

/**
 * Lightweight benchmark runner for ext-json-schema.
 *
 * The default quick mode is intended for CI: it prints a small ext vs pure PHP
 * comparison but does not fail on performance thresholds. It only fails when a
 * benchmarked validation returns an unexpected result.
 *
 * Usage:
 *   php bench.php --quick --extension=./modules/json_schema.so
 *   php bench.php --iterations=50000 --repeats=5 --extension=./modules/json_schema.so
 */

const DEFAULT_EXTENSION = __DIR__ . '/modules/json_schema.so';

/** @return array{engine:?string, extension:string, iterations:int, repeats:int} */
function parse_args(array $argv): array
{
    $engine = null;
    $extension = getenv('JSON_SCHEMA_EXTENSION') ?: DEFAULT_EXTENSION;
    $iterations = 10000;
    $repeats = 5;

    for ($i = 1; $i < count($argv); $i++) {
        $arg = $argv[$i];
        if ($arg === '--help' || $arg === '-h') {
            echo "Usage: php bench.php [--quick] [--extension=./modules/json_schema.so] [--iterations=10000] [--repeats=5]\n";
            exit(0);
        }
        if ($arg === '--quick') {
            $iterations = 1000;
            $repeats = 2;
            continue;
        }
        if (str_starts_with($arg, '--engine=')) {
            $engine = substr($arg, strlen('--engine='));
            continue;
        }
        if (str_starts_with($arg, '--extension=')) {
            $extension = substr($arg, strlen('--extension='));
            continue;
        }
        if (str_starts_with($arg, '--iterations=')) {
            $iterations = (int) substr($arg, strlen('--iterations='));
            continue;
        }
        if (str_starts_with($arg, '--repeats=')) {
            $repeats = (int) substr($arg, strlen('--repeats='));
            continue;
        }
    }

    if ($engine !== null && !in_array($engine, ['ext', 'php'], true)) {
        fwrite(STDERR, "Unknown engine: {$engine}\n");
        exit(2);
    }
    if ($iterations < 1 || $repeats < 1) {
        fwrite(STDERR, "iterations and repeats must be positive integers\n");
        exit(2);
    }

    return ['engine' => $engine, 'extension' => $extension, 'iterations' => $iterations, 'repeats' => $repeats];
}

function as_object(mixed $value): mixed
{
    return json_decode(json_encode($value, JSON_THROW_ON_ERROR), false, 512, JSON_THROW_ON_ERROR);
}

/** @return array<string,array{schema:array<string,mixed>, data:mixed, options:array<string,mixed>}> */
function benchmark_cases(): array
{
    $simpleSchema = [
        'type' => 'object',
        'properties' => [
            'id' => ['type' => 'integer', 'minimum' => 1],
            'name' => ['type' => 'string', 'minLength' => 1, 'maxLength' => 80],
            'email' => ['type' => 'string', 'format' => 'email'],
            'tags' => ['type' => 'array', 'items' => ['type' => 'string'], 'uniqueItems' => true],
        ],
        'required' => ['id', 'name', 'email', 'tags'],
        'additionalProperties' => false,
    ];

    $refSchema = [
        '$schema' => 'http://json-schema.org/draft-07/schema#',
        'type' => 'object',
        'properties' => [
            'user' => ['$ref' => '#/definitions/user'],
            'address' => ['$ref' => '#/definitions/address'],
        ],
        'required' => ['user', 'address'],
        'definitions' => [
            'user' => [
                'type' => 'object',
                'properties' => [
                    'name' => ['type' => 'string', 'minLength' => 1],
                    'email' => ['type' => 'string', 'format' => 'email'],
                ],
                'required' => ['name', 'email'],
                'additionalProperties' => false,
            ],
            'address' => [
                'type' => 'object',
                'properties' => [
                    'zip' => ['type' => 'string', 'pattern' => '^[0-9]{5}$'],
                    'country' => ['type' => 'string', 'minLength' => 2, 'maxLength' => 2],
                ],
                'required' => ['zip', 'country'],
                'additionalProperties' => false,
            ],
        ],
    ];

    return [
        'simple' => [
            'schema' => $simpleSchema,
            'data' => as_object(['id' => 123, 'name' => 'Akihito', 'email' => 'akihito@example.com', 'tags' => ['php', 'json-schema']]),
            'options' => [],
        ],
        'local-ref' => [
            'schema' => $refSchema,
            'data' => as_object(['user' => ['name' => 'Akihito', 'email' => 'akihito@example.com'], 'address' => ['zip' => '10000', 'country' => 'JP']]),
            'options' => ['draft' => 'draft7'],
        ],
    ];
}

/** @return array<string,mixed> */
function bench(string $name, int $iterations, int $repeats, callable $fn): array
{
    $warmup = min(100, $iterations);
    for ($i = 0; $i < $warmup; $i++) {
        $fn();
    }

    $times = [];
    $validCount = 0;
    for ($r = 0; $r < $repeats; $r++) {
        gc_collect_cycles();
        $start = hrtime(true);
        $localValidCount = 0;
        for ($i = 0; $i < $iterations; $i++) {
            if ($fn()) {
                $localValidCount++;
            }
        }
        $elapsed = (hrtime(true) - $start) / 1e9;
        $times[] = $elapsed;
        $validCount = $localValidCount;
    }

    $min = min($times);
    $mean = array_sum($times) / count($times);

    return [
        'name' => $name,
        'iterations' => $iterations,
        'validCount' => $validCount,
        'minSeconds' => $min,
        'meanSeconds' => $mean,
        'opsPerSecond' => $iterations / $min,
        'microsecondsPerOp' => ($min / $iterations) * 1_000_000,
    ];
}

/** @return array{engine:string, php:string, xdebugLoaded:bool, cases:list<array<string,mixed>>} */
function run_engine(string $engine, int $iterations, int $repeats): array
{
    $results = [];
    $cases = benchmark_cases();

    if ($engine === 'ext') {
        if (!extension_loaded('json_schema')) {
            throw new RuntimeException('json_schema extension is not loaded');
        }
        foreach ($cases as $name => $case) {
            $results[] = bench($name, $iterations, $repeats, static fn(): bool => json_schema_validate(
                $case['data'],
                $case['schema'],
                1,
                $case['options']
            ));
        }
    } else {
        if (extension_loaded('json_schema')) {
            throw new RuntimeException('pure PHP benchmark must run without json_schema extension loaded');
        }
        require_once __DIR__ . '/vendor/autoload.php';
        foreach ($cases as $name => $case) {
            $schema = as_object($case['schema']);
            $data = $case['data'];
            $results[] = bench($name, $iterations, $repeats, static function () use ($schema, $data): bool {
                $validator = new JsonSchema\Validator();
                $value = as_object($data);
                $validator->validate($value, $schema, JsonSchema\Constraints\Constraint::CHECK_MODE_NORMAL);
                return $validator->isValid();
            });
        }
    }

    return ['engine' => $engine, 'php' => PHP_VERSION, 'xdebugLoaded' => extension_loaded('xdebug'), 'cases' => $results];
}

/** @return array{exit:int, stdout:string, stderr:string} */
function run_engine_process(string $engine, string $extension, int $iterations, int $repeats): array
{
    putenv('XDEBUG_MODE=off');

    $command = [PHP_BINARY];
    if ($engine === 'ext') {
        $command[] = '-d';
        $command[] = 'extension=' . $extension;
    } else {
        $command[] = '-n';
    }
    $command[] = __FILE__;
    $command[] = '--engine=' . $engine;
    $command[] = '--iterations=' . (string) $iterations;
    $command[] = '--repeats=' . (string) $repeats;

    $descriptors = [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']];
    $process = proc_open($command, $descriptors, $pipes);
    if (!is_resource($process)) {
        throw new RuntimeException('Failed to start ' . $engine . ' benchmark process');
    }

    fclose($pipes[0]);
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $exit = proc_close($process);

    return ['exit' => $exit, 'stdout' => $stdout === false ? '' : $stdout, 'stderr' => $stderr === false ? '' : $stderr];
}

/** @return array{engine:string, php:string, xdebugLoaded:bool, cases:list<array<string,mixed>>} */
function decode_process(array $processResult, string $engine): array
{
    if ($processResult['exit'] !== 0) {
        throw new RuntimeException("{$engine} benchmark failed:\n" . $processResult['stderr'] . $processResult['stdout']);
    }

    $decoded = json_decode($processResult['stdout'], true, 512, JSON_THROW_ON_ERROR);
    if (!is_array($decoded) || !isset($decoded['cases']) || !is_array($decoded['cases'])) {
        throw new RuntimeException("{$engine} benchmark produced invalid output");
    }

    return $decoded;
}

function print_report(array $ext, array $php): int
{
    $phpByName = [];
    foreach ($php['cases'] as $case) {
        $phpByName[$case['name']] = $case;
    }

    echo "Quick benchmark (informational; no performance threshold)\n";
    echo "case        ext us/op   php us/op   speedup\n";
    echo "----------  ----------  ----------  -------\n";

    $exit = 0;
    foreach ($ext['cases'] as $extCase) {
        $name = $extCase['name'];
        $phpCase = $phpByName[$name] ?? null;
        if (!$phpCase) {
            fwrite(STDERR, "Missing PHP benchmark case: {$name}\n");
            return 1;
        }
        if ($extCase['validCount'] !== $extCase['iterations'] || $phpCase['validCount'] !== $phpCase['iterations']) {
            fwrite(STDERR, "Benchmark case did not validate all iterations: {$name}\n");
            $exit = 1;
        }

        $speedup = $phpCase['microsecondsPerOp'] / max($extCase['microsecondsPerOp'], PHP_FLOAT_EPSILON);
        printf(
            "%-10s  %10.3f  %10.3f  %6.1fx\n",
            $name,
            $extCase['microsecondsPerOp'],
            $phpCase['microsecondsPerOp'],
            $speedup
        );
    }

    return $exit;
}

try {
    $args = parse_args($argv);
    if ($args['engine'] !== null) {
        echo json_encode(run_engine($args['engine'], $args['iterations'], $args['repeats']), JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES);
        echo "\n";
        exit(0);
    }

    if (!is_file($args['extension'])) {
        fwrite(STDERR, "json_schema extension module not found: {$args['extension']}\n");
        exit(2);
    }

    $ext = decode_process(run_engine_process('ext', $args['extension'], $args['iterations'], $args['repeats']), 'ext');
    $php = decode_process(run_engine_process('php', $args['extension'], $args['iterations'], $args['repeats']), 'php');
    exit(print_report($ext, $php));
} catch (Throwable $e) {
    fwrite(STDERR, $e->getMessage() . "\n");
    exit(2);
}
