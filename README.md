# JSON Schema Validator for PHP (PECL Extension)

[![Build and Test PHP Extension](https://github.com/koriym/ext-json-schema/actions/workflows/build.yml/badge.svg)](https://github.com/koriym/ext-json-schema/actions/workflows/build.yml)

A high-performance PHP extension for JSON Schema validation, ported from [jsonrainbow/json-schema](https://github.com/jsonrainbow/json-schema).

## Features

- **JSON Schema Draft Support**: Draft-04, Draft-06, Draft-07
- **High Performance**: Native C implementation for fast validation
- **Compatible API**: Similar API to jsonrainbow/json-schema for easy migration
- **Complete Type Validation**: string, integer, number, boolean, null, array, object
- **All Major Keywords**:
  - String: `minLength`, `maxLength`, `pattern`, `format`
  - Number: `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf`
  - Array: `minItems`, `maxItems`, `uniqueItems`, `items`, `contains`
  - Object: `required`, `properties`, `additionalProperties`, `minProperties`, `maxProperties`, `propertyNames`
  - Combinators: `allOf`, `anyOf`, `oneOf`, `not`
  - Conditional: `if`/`then`/`else` (Draft-07)
  - References: `$ref`, `definitions`, `$defs`
  - Other: `enum`, `const`, `format`
- **Format Validation**: email, uri, date, time, date-time, ipv4, ipv6, hostname, uuid
- **Type Coercion**: Optional automatic type conversion

## Requirements

- PHP 8.0 or later
- php-json extension (usually built-in)
- php-pcre extension (usually built-in)

## Installation

### From Source

```bash
git clone https://github.com/koriym/ext-json-schema.git
cd ext-json-schema
phpize
./configure --enable-json_schema
make
make install
```

Add to your php.ini:
```ini
extension=json_schema.so
```

## Usage

### Object-Oriented API (Recommended)

```php
use JsonSchema\Validator;
use JsonSchema\Constraint;

$schema = [
    'type' => 'object',
    'properties' => [
        'name' => ['type' => 'string', 'minLength' => 1],
        'age' => ['type' => 'integer', 'minimum' => 0]
    ],
    'required' => ['name']
];

$data = ['name' => 'John', 'age' => 30];

$validator = new Validator();
if ($validator->validate($data, $schema)) {
    echo "Valid!\n";
} else {
    foreach ($validator->getErrors() as $error) {
        echo "Error: " . $error['message'] . "\n";
    }
}
```

### Procedural API

```php
// Simple validation
$isValid = json_schema_validate($data, $schema);

// Validation with error details
$result = json_schema_validate_with_errors($data, $schema);
if (!$result['valid']) {
    foreach ($result['errors'] as $error) {
        echo $error['message'] . "\n";
    }
}
```

### Check Modes

```php
use JsonSchema\Constraint;

// Normal validation
$validator = new Validator(Constraint::CHECK_MODE_NORMAL);

// Type coercion (convert "123" to 123 if schema expects integer)
$validator = new Validator(Constraint::CHECK_MODE_COERCE_TYPES);

// Disable format validation
$validator = new Validator(Constraint::CHECK_MODE_DISABLE_FORMAT);

// Throw exception on validation failure
$validator = new Validator(Constraint::CHECK_MODE_EXCEPTIONS);

// Combine multiple modes
$validator = new Validator(
    Constraint::CHECK_MODE_COERCE_TYPES |
    Constraint::CHECK_MODE_APPLY_DEFAULTS
);
```

### Available Check Mode Constants

| Constant | Description |
|----------|-------------|
| `CHECK_MODE_NONE` | No special processing |
| `CHECK_MODE_NORMAL` | Normal validation (default) |
| `CHECK_MODE_TYPE_CAST` | Enable type casting |
| `CHECK_MODE_COERCE_TYPES` | Coerce string values to expected types |
| `CHECK_MODE_APPLY_DEFAULTS` | Apply default values from schema |
| `CHECK_MODE_EXCEPTIONS` | Throw exception on validation failure |
| `CHECK_MODE_DISABLE_FORMAT` | Skip format validation |
| `CHECK_MODE_EARLY_COERCE` | Coerce types before validation |
| `CHECK_MODE_ONLY_REQUIRED_DEFAULTS` | Only apply defaults for required properties |
| `CHECK_MODE_VALIDATE_SCHEMA` | Validate the schema itself |

## Schema Examples

### Basic Types

```php
// String with constraints
$schema = [
    'type' => 'string',
    'minLength' => 1,
    'maxLength' => 100,
    'pattern' => '^[a-z]+$'
];

// Number with range
$schema = [
    'type' => 'number',
    'minimum' => 0,
    'maximum' => 100,
    'multipleOf' => 0.5
];

// Array with items validation
$schema = [
    'type' => 'array',
    'items' => ['type' => 'integer'],
    'minItems' => 1,
    'uniqueItems' => true
];
```

### Using $ref

```php
$schema = [
    'definitions' => [
        'address' => [
            'type' => 'object',
            'properties' => [
                'street' => ['type' => 'string'],
                'city' => ['type' => 'string']
            ],
            'required' => ['street', 'city']
        ]
    ],
    'type' => 'object',
    'properties' => [
        'home' => ['$ref' => '#/definitions/address'],
        'work' => ['$ref' => '#/definitions/address']
    ]
];
```

### Conditional Validation (Draft-07)

```php
$schema = [
    'type' => 'object',
    'if' => [
        'properties' => ['type' => ['const' => 'premium']]
    ],
    'then' => [
        'required' => ['discount']
    ],
    'else' => [
        'required' => ['standard']
    ]
];
```

## API Reference

### JsonSchema\Validator Class

| Method | Description |
|--------|-------------|
| `__construct(int $checkMode = CHECK_MODE_NORMAL)` | Create a new validator |
| `validate(mixed $data, mixed $schema, ?int $checkMode = null): bool` | Validate data against schema |
| `isValid(): bool` | Check if last validation was successful |
| `getErrors(): array` | Get validation errors from last validation |
| `reset(): void` | Clear validation state |
| `getCheckMode(): int` | Get current check mode |
| `setCheckMode(int $mode): void` | Set check mode |

### Procedural Functions

| Function | Description |
|----------|-------------|
| `json_schema_validate(mixed $data, mixed $schema, int $checkMode = 0): bool` | Validate data against schema |
| `json_schema_validate_with_errors(mixed $data, mixed $schema, int $checkMode = 0): array` | Validate and return errors |

## Error Format

Each error in the errors array contains:

```php
[
    'message' => 'Error description',
    'property' => 'propertyName',  // or null
    'pointer' => '/path/to/error', // JSON Pointer
    'constraint' => 1              // Error code
]
```

## Development

### Build and Test

```bash
# Clean, build, and run smoke test
./build.sh all

# Run unit tests
./build.sh test

# Individual steps
./build.sh clean
./build.sh prepare
./build.sh build
./build.sh run
```

### Continuous Integration

This project includes GitHub Actions workflows for automated testing across multiple PHP versions.

### IDE Support

This repository contains CMakeLists.txt for CLion integration. Refer to [Developing a PHP extension in CLion](https://dev.to/jasny/developing-a-php-extension-in-clion-3oo1) for more information.

## License

MIT License

## Credits

- Based on [jsonrainbow/json-schema](https://github.com/jsonrainbow/json-schema) by Justin Rainbow
- PECL port by [Akihito Koriyama](https://github.com/koriym)
