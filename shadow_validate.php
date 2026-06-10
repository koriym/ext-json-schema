<?php

declare(strict_types=1);

/**
 * Shadow validation runner.
 *
 * Compares ext-json-schema against the pure PHP justinrainbow/json-schema validator
 * for representative schema/data fixtures. The runner executes each engine in a
 * separate PHP process so the PECL JsonSchema\Validator class does not shadow the
 * pure PHP JsonSchema\Validator class.
 *
 * Usage:
 *   php shadow_validate.php [fixture-file-or-directory ...]
 *   php shadow_validate.php --extension=./modules/json_schema.so tests/fixtures/shadow-validation
 */

const DEFAULT_FIXTURE_DIR = __DIR__ . '/tests/fixtures/shadow-validation';
const DEFAULT_EXTENSION = __DIR__ . '/modules/json_schema.so';

function usage(): string
{
    return "Usage: php shadow_validate.php [--extension=/path/json_schema.so] [fixture-file-or-directory ...]\n";
}

/** @return array{engine:?string, extension:string, paths:list<string>} */
function parse_args(array $argv): array
{
    $engine = null;
    $extension = getenv('JSON_SCHEMA_EXTENSION') ?: DEFAULT_EXTENSION;
    $paths = [];

    for ($i = 1; $i < count($argv); $i++) {
        $arg = $argv[$i];
        if ($arg === '--help' || $arg === '-h') {
            echo usage();
            exit(0);
        }
        if (str_starts_with($arg, '--engine=')) {
            $engine = substr($arg, strlen('--engine='));
            continue;
        }
        if ($arg === '--engine') {
            $engine = $argv[++$i] ?? null;
            continue;
        }
        if (str_starts_with($arg, '--extension=')) {
            $extension = substr($arg, strlen('--extension='));
            continue;
        }
        if ($arg === '--extension') {
            $extension = $argv[++$i] ?? $extension;
            continue;
        }
        $paths[] = $arg;
    }

    if ($paths === []) {
        $paths[] = DEFAULT_FIXTURE_DIR;
    }

    if ($engine !== null && !in_array($engine, ['ext', 'php'], true)) {
        fwrite(STDERR, "Unknown engine: {$engine}\n" . usage());
        exit(2);
    }

    return ['engine' => $engine, 'extension' => $extension, 'paths' => $paths];
}

/** @return list<string> */
function fixture_files(array $paths): array
{
    $files = [];
    foreach ($paths as $path) {
        if (is_dir($path)) {
            $matches = glob(rtrim($path, DIRECTORY_SEPARATOR) . DIRECTORY_SEPARATOR . '*.json') ?: [];
            array_push($files, ...$matches);
            continue;
        }
        if (is_file($path)) {
            $files[] = $path;
            continue;
        }
        throw new RuntimeException("Fixture path not found: {$path}");
    }

    sort($files);
    return array_values(array_unique($files));
}

function json_decode_file(string $file): mixed
{
    $json = file_get_contents($file);
    if ($json === false) {
        throw new RuntimeException("Cannot read fixture: {$file}");
    }

    return json_decode($json, false, 512, JSON_THROW_ON_ERROR);
}

/** @return list<object> */
function load_cases(array $paths): array
{
    $cases = [];
    foreach (fixture_files($paths) as $file) {
        $decoded = json_decode_file($file);
        $fileCases = is_object($decoded) && property_exists($decoded, 'cases') ? $decoded->cases : $decoded;
        if (!is_array($fileCases)) {
            throw new RuntimeException("Fixture must be an array or an object with a cases array: {$file}");
        }
        foreach ($fileCases as $offset => $case) {
            if (!is_object($case)) {
                throw new RuntimeException("Fixture case must be an object: {$file}#{$offset}");
            }
            if (!property_exists($case, 'schema')) {
                throw new RuntimeException("Fixture case is missing schema: {$file}#{$offset}");
            }
            if (!property_exists($case, 'data')) {
                throw new RuntimeException("Fixture case is missing data: {$file}#{$offset}");
            }
            $case->_fixtureFile = $file;
            $case->_fixtureOffset = $offset;
            $cases[] = $case;
        }
    }

    return $cases;
}

function json_clone(mixed $value): mixed
{
    return json_decode(json_encode($value, JSON_THROW_ON_ERROR), false, 512, JSON_THROW_ON_ERROR);
}

function to_array(mixed $value): mixed
{
    if (is_object($value)) {
        $result = [];
        foreach ($value as $key => $child) {
            $result[$key] = to_array($child);
        }
        return $result;
    }
    if (is_array($value)) {
        return array_map('to_array', $value);
    }
    return $value;
}

/** @return array<string,mixed> */
function object_options(?object $options): array
{
    return $options ? to_array($options) : [];
}

function case_name(object $case): string
{
    return isset($case->name) && is_string($case->name)
        ? $case->name
        : basename((string) $case->_fixtureFile) . '#' . (string) $case->_fixtureOffset;
}

/** @return array<string,mixed> */
function run_ext_case(object $case): array
{
    if (!extension_loaded('json_schema')) {
        throw new RuntimeException('json_schema extension is not loaded for ext engine');
    }

    $options = object_options($case->options ?? null);
    $remotes = object_options($case->remotes ?? null);
    if ($remotes !== []) {
        $options['resolver'] = static fn(string $uri, string $baseUri, string $rawRef): mixed => $remotes[$uri] ?? null;
    }

    $valid = json_schema_validate(
        json_clone($case->data),
        to_array($case->schema),
        (int) ($case->checkMode ?? JsonSchema\Constraint::CHECK_MODE_NORMAL),
        $options
    );

    return ['name' => case_name($case), 'valid' => $valid, 'error' => null];
}

/** @return array<string,mixed> */
function run_php_case(object $case): array
{
    if (extension_loaded('json_schema')) {
        throw new RuntimeException('pure PHP engine must run without json_schema extension loaded');
    }

    require_once __DIR__ . '/vendor/autoload.php';

    $storage = new JsonSchema\SchemaStorage();
    $remotes = $case->remotes ?? null;
    if (is_object($remotes)) {
        foreach ($remotes as $uri => $schema) {
            $storage->addSchema((string) $uri, json_clone($schema));
        }
    }

    $factory = new JsonSchema\Constraints\Factory($storage);
    $validator = new JsonSchema\Validator($factory);
    $schema = json_clone($case->schema);
    $data = json_clone($case->data);
    $options = object_options($case->options ?? null);
    $checkMode = (int) ($case->checkMode ?? JsonSchema\Constraints\Constraint::CHECK_MODE_NORMAL);

    if (isset($options['baseUri']) && is_string($options['baseUri'])) {
        $storage->addSchema($options['baseUri'], $schema);
        $schema = $storage->getSchema($options['baseUri']);
    }

    $validator->validate($data, $schema, $checkMode);

    return ['name' => case_name($case), 'valid' => $validator->isValid(), 'error' => null];
}

/** @return array{engine:string, results:list<array<string,mixed>>} */
function run_engine(string $engine, array $paths): array
{
    $results = [];
    foreach (load_cases($paths) as $case) {
        try {
            $results[] = $engine === 'ext' ? run_ext_case($case) : run_php_case($case);
        } catch (Throwable $e) {
            $results[] = ['name' => case_name($case), 'valid' => null, 'error' => $e->getMessage()];
        }
    }

    return ['engine' => $engine, 'results' => $results];
}

/** @return array{exit:int, stdout:string, stderr:string} */
function run_engine_process(string $engine, string $extension, array $paths): array
{
    $command = [PHP_BINARY];
    if ($engine === 'ext') {
        $command[] = '-d';
        $command[] = 'extension=' . $extension;
    } else {
        $command[] = '-n';
    }
    $command[] = __FILE__;
    $command[] = '--engine=' . $engine;
    array_push($command, ...$paths);

    $descriptors = [
        0 => ['pipe', 'r'],
        1 => ['pipe', 'w'],
        2 => ['pipe', 'w'],
    ];
    $process = proc_open($command, $descriptors, $pipes);
    if (!is_resource($process)) {
        throw new RuntimeException('Failed to start ' . $engine . ' engine process');
    }

    fclose($pipes[0]);
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $exit = proc_close($process);

    return ['exit' => $exit, 'stdout' => $stdout === false ? '' : $stdout, 'stderr' => $stderr === false ? '' : $stderr];
}

/** @return array{engine:string, results:list<array<string,mixed>>} */
function decode_engine_output(array $processResult, string $engine): array
{
    if ($processResult['exit'] !== 0) {
        throw new RuntimeException("{$engine} engine failed:\n" . $processResult['stderr'] . $processResult['stdout']);
    }

    $decoded = json_decode($processResult['stdout'], true, 512, JSON_THROW_ON_ERROR);
    if (!is_array($decoded) || !isset($decoded['results']) || !is_array($decoded['results'])) {
        throw new RuntimeException("{$engine} engine produced invalid output");
    }

    return $decoded;
}

function run_comparison(string $extension, array $paths): int
{
    if (!is_file($extension)) {
        fwrite(STDERR, "json_schema extension module not found: {$extension}\n");
        return 2;
    }

    $ext = decode_engine_output(run_engine_process('ext', $extension, $paths), 'ext');
    $php = decode_engine_output(run_engine_process('php', $extension, $paths), 'php');
    $phpByName = [];
    foreach ($php['results'] as $result) {
        $phpByName[$result['name']] = $result;
    }

    $diffs = [];
    foreach ($ext['results'] as $extResult) {
        $name = $extResult['name'];
        $phpResult = $phpByName[$name] ?? null;
        if (!$phpResult || $extResult['valid'] !== $phpResult['valid'] || $extResult['error'] !== $phpResult['error']) {
            $diffs[] = ['case' => $name, 'ext' => $extResult, 'php' => $phpResult];
        }
    }

    $count = count($ext['results']);
    if ($diffs === []) {
        echo "Shadow validation: {$count} cases checked, 0 differences\n";
        return 0;
    }

    fwrite(STDERR, "Shadow validation differences: " . count($diffs) . " / {$count}\n");
    foreach ($diffs as $diff) {
        fwrite(STDERR, json_encode($diff, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . "\n");
    }

    return 1;
}

try {
    $args = parse_args($argv);
    if ($args['engine'] !== null) {
        echo json_encode(run_engine($args['engine'], $args['paths']), JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES);
        echo "\n";
        exit(0);
    }

    exit(run_comparison($args['extension'], $args['paths']));
} catch (Throwable $e) {
    fwrite(STDERR, $e->getMessage() . "\n");
    exit(2);
}
