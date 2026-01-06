/*
 * JSON Schema Validator for PHP
 *
 * A PECL extension that provides JSON Schema validation functionality.
 * Based on the API design of jsonrainbow/json-schema.
 *
 * Supports JSON Schema Draft-04, Draft-06, and Draft-07.
 */

#ifndef PHP_JSON_SCHEMA_H
#define PHP_JSON_SCHEMA_H

extern zend_module_entry json_schema_module_entry;
#define phpext_json_schema_ptr &json_schema_module_entry

#define PHP_JSON_SCHEMA_VERSION "1.0.0"
#define PHP_JSON_SCHEMA_EXTNAME "json_schema"

#ifdef PHP_WIN32
#define PHP_JSON_SCHEMA_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define PHP_JSON_SCHEMA_API __attribute__ ((visibility("default")))
#else
#define PHP_JSON_SCHEMA_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

/* Check mode flags (matching jsonrainbow/json-schema) */
#define JSON_SCHEMA_CHECK_MODE_NONE              0x00000000
#define JSON_SCHEMA_CHECK_MODE_NORMAL            0x00000001
#define JSON_SCHEMA_CHECK_MODE_TYPE_CAST         0x00000002
#define JSON_SCHEMA_CHECK_MODE_COERCE_TYPES      0x00000004
#define JSON_SCHEMA_CHECK_MODE_APPLY_DEFAULTS    0x00000008
#define JSON_SCHEMA_CHECK_MODE_EXCEPTIONS        0x00000010
#define JSON_SCHEMA_CHECK_MODE_DISABLE_FORMAT    0x00000020
#define JSON_SCHEMA_CHECK_MODE_EARLY_COERCE      0x00000040
#define JSON_SCHEMA_CHECK_MODE_ONLY_REQUIRED_DEFAULTS 0x00000080
#define JSON_SCHEMA_CHECK_MODE_VALIDATE_SCHEMA   0x00000100

/* Error codes */
#define JSON_SCHEMA_ERROR_NONE                   0
#define JSON_SCHEMA_ERROR_TYPE_MISMATCH          1
#define JSON_SCHEMA_ERROR_MIN_LENGTH             2
#define JSON_SCHEMA_ERROR_MAX_LENGTH             3
#define JSON_SCHEMA_ERROR_PATTERN_MISMATCH       4
#define JSON_SCHEMA_ERROR_MINIMUM                5
#define JSON_SCHEMA_ERROR_MAXIMUM                6
#define JSON_SCHEMA_ERROR_MIN_ITEMS              7
#define JSON_SCHEMA_ERROR_MAX_ITEMS              8
#define JSON_SCHEMA_ERROR_UNIQUE_ITEMS           9
#define JSON_SCHEMA_ERROR_ENUM_MISMATCH          10
#define JSON_SCHEMA_ERROR_REQUIRED_PROPERTY      11
#define JSON_SCHEMA_ERROR_ADDITIONAL_PROPERTIES  12
#define JSON_SCHEMA_ERROR_FORMAT                 13
#define JSON_SCHEMA_ERROR_CONST_MISMATCH         14
#define JSON_SCHEMA_ERROR_MULTIPLE_OF            15
#define JSON_SCHEMA_ERROR_EXCLUSIVE_MINIMUM      16
#define JSON_SCHEMA_ERROR_EXCLUSIVE_MAXIMUM      17
#define JSON_SCHEMA_ERROR_MIN_PROPERTIES         18
#define JSON_SCHEMA_ERROR_MAX_PROPERTIES         19
#define JSON_SCHEMA_ERROR_PROPERTY_NAMES         20
#define JSON_SCHEMA_ERROR_CONTAINS               21
#define JSON_SCHEMA_ERROR_IF_THEN_ELSE           22
#define JSON_SCHEMA_ERROR_ALL_OF                 23
#define JSON_SCHEMA_ERROR_ANY_OF                 24
#define JSON_SCHEMA_ERROR_ONE_OF                 25
#define JSON_SCHEMA_ERROR_NOT                    26
#define JSON_SCHEMA_ERROR_REF                    27

/* Module functions */
PHP_MINIT_FUNCTION(json_schema);
PHP_MSHUTDOWN_FUNCTION(json_schema);
PHP_MINFO_FUNCTION(json_schema);

/* Class entry declarations */
extern zend_class_entry *json_schema_validator_ce;
extern zend_class_entry *json_schema_constraint_ce;
extern zend_class_entry *json_schema_exception_ce;

#ifdef ZTS
#define JSON_SCHEMA_G(v) TSRMG(json_schema_globals_id, zend_json_schema_globals *, v)
#else
#define JSON_SCHEMA_G(v) (json_schema_globals.v)
#endif

#endif /* PHP_JSON_SCHEMA_H */
