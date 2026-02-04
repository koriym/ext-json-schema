# Changelog

## 1.0.0

- Initial release
- JSON Schema Draft-04, Draft-06, Draft-07 support
- 2178/2178 official JSON Schema Test Suite tests passed (100%)
- Procedural API: `json_schema_validate()`, `json_schema_get_errors()`
- OOP API: `JsonSchema\Validator` class
- jsonrainbow/json-schema compatible error format
- `ValidatorAdapter` for drop-in replacement of jsonrainbow/json-schema
- Memory leak free (Valgrind verified)
- PHP 8.1+ support
