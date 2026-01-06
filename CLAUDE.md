# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ext-json-schema is a PECL extension providing high-performance JSON Schema validation for PHP. It includes:
- Native C implementation (`json_schema.c`, `validator.c`)
- PHP compatibility layer for jsonrainbow/json-schema API (`src/JsonSchema/`)

## Build Commands

```bash
# Full build cycle (clean, prepare, build, run smoke test)
./build.sh all

# Individual steps
./build.sh clean     # Clean build artifacts
./build.sh prepare   # Run phpize && ./configure
./build.sh build     # Run make
./build.sh install   # Run make install
./build.sh test      # Run PHPT tests
./build.sh run       # Run smoke.php with extension
```

## Test Commands

```bash
# PHPT extension tests (C code)
make test TESTS=tests/

# Single PHPT test
make test TESTS=tests/001-basic.phpt

# PHPUnit tests (PHP adapter layer)
composer test

# JSON Schema Test Suite (2178 tests)
composer test-suite
# or
php run_test_suite.php

# Memory leak detection with Valgrind
USE_ZEND_ALLOC=0 ZEND_DONT_UNLOAD_MODULES=1 make test TESTS=tests/*.phpt TEST_PHP_ARGS="-m"
```

## Architecture

### C Extension Layer
- `json_schema.c` - PHP extension entry point, class definitions (`JsonSchema\Validator`), procedural API (`json_schema_validate()`, `json_schema_get_errors()`)
- `validator.c` / `validator.h` - Core validation logic for all JSON Schema keywords
- `php_json_schema.h` - Extension header with constants and error codes

### PHP Compatibility Layer
- `src/JsonSchema/ValidatorAdapter.php` - Extends jsonrainbow's `BaseConstraint`, uses PECL for validation
- `src/JsonSchema/Constraints/Constraint.php` - Check mode constants matching jsonrainbow API
- `src/JsonSchema/Validator.php` - Bridge file for autoloading

### Two APIs
1. **Procedural**: `json_schema_validate($data, $schema)` - returns bool
2. **OOP**: `new \JsonSchema\Validator()` with `validate()`, `isValid()`, `getErrors()`
3. **Adapter**: `new \JsonSchema\ValidatorAdapter()` - drop-in replacement for jsonrainbow/json-schema

## Test Structure

- `tests/*.phpt` - PHPT tests for C extension features
- `tests/ValidatorAdapterTest.php` - PHPUnit tests for jsonrainbow API compatibility
- `JSON-Schema-Test-Suite/` - Git submodule with official JSON Schema tests
