# ext-json-schema

High-performance JSON Schema validator for PHP as a PECL extension.

[![Build and Test PHP Extension](https://github.com/koriym/ext-json-schema/actions/workflows/build.yml/badge.svg?branch=1.x)](https://github.com/koriym/ext-json-schema/actions/workflows/build.yml)

## Features

- **JSON Schema Draft-04/06/07** support
- **2178 tests passed** from [JSON Schema Test Suite](https://github.com/json-schema-org/JSON-Schema-Test-Suite)
- **[jsonrainbow/json-schema](https://github.com/jsonrainbow/json-schema) compatible** API
- Native C implementation for performance

## Requirements

- PHP 8.1+

## Installation

```bash
git clone https://github.com/koriym/ext-json-schema.git
cd ext-json-schema
phpize
./configure
make
make install
```

```ini
extension=json_schema.so
```

## Usage

### Procedural API

```php
$data = ['name' => 'John', 'age' => 30];
$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string'],
        'age' => ['type' => 'integer', 'minimum' => 0]
    ],
    'required' => ['name']
];

if (json_schema_validate($data, $schema)) {
    echo "Valid!";
}
```

### OOP API

```php
$validator = new JsonSchema\Validator();
$validator->validate($data, $schema);

if ($validator->isValid()) {
    echo "Valid!";
} else {
    print_r($validator->getErrors());
}
```

### jsonrainbow/json-schema Compatible Adapter

For projects using [jsonrainbow/json-schema](https://github.com/jsonrainbow/json-schema), use `ValidatorAdapter` for a drop-in replacement:

```bash
composer require justinrainbow/json-schema
```

```php
use JsonSchema\ValidatorAdapter;

$validator = new ValidatorAdapter();
$validator->validate($data, $schema);

// Same API as jsonrainbow/json-schema
$validator->isValid();
$validator->getErrors();    // Compatible error format
$validator->numErrors();
$validator->reset();
```

## Error Format

Errors match jsonrainbow/json-schema format:

```php
[
    'property'   => 'user.email',     // Dot notation path
    'pointer'    => '/user/email',    // JSON Pointer
    'message'    => 'Type mismatch',
    'constraint' => 'type',           // Constraint name
    'context'    => 1                 // ERROR_DOCUMENT_VALIDATION
]
```

## Supported Keywords

| Category | Keywords |
|----------|----------|
| Type | `type` |
| String | `minLength`, `maxLength`, `pattern`, `format` |
| Number | `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf` |
| Array | `items`, `additionalItems`, `minItems`, `maxItems`, `uniqueItems`, `contains` |
| Object | `properties`, `additionalProperties`, `required`, `minProperties`, `maxProperties`, `propertyNames`, `dependencies` |
| Combinators | `allOf`, `anyOf`, `oneOf`, `not` |
| Conditional | `if`, `then`, `else` |
| Reference | `$ref`, `definitions`, `$defs` |
| Other | `enum`, `const` |

## Quality Assurance

This extension was implemented with [Claude Code](https://claude.ai/code) (Opus 4.5) and undergoes rigorous testing:

| Test | Coverage |
|------|----------|
| **JSON Schema Test Suite** | 2178/2178 tests passed (100%) |
| **PHPT Unit Tests** | 15 tests covering all features |
| **API Compatibility Tests** | 30 PHPUnit tests for jsonrainbow compatibility |
| **Memory Leak Detection** | Valgrind + PHP's built-in leak detector |
| **Multi-version Testing** | PHP 8.1, 8.2, 8.3, 8.4, 8.5 |

### Memory Safety

```bash
# Run with Valgrind memory check
USE_ZEND_ALLOC=0 ZEND_DONT_UNLOAD_MODULES=1 \
make test TESTS=tests/*.phpt TEST_PHP_ARGS="-m"

# Result: Tests leaked: 0 (0.0%)
```

### Continuous Integration

All tests run automatically on every push via GitHub Actions, including:
- Compilation on multiple PHP versions
- Full test suite execution
- Memory leak detection with Valgrind

## License

MIT
