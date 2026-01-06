/*
 * JSON Schema Validator - Core validation logic
 */

#ifndef JSON_SCHEMA_VALIDATOR_H
#define JSON_SCHEMA_VALIDATOR_H

#include "php.h"
#include "zend_exceptions.h"

/* Validation error structure */
typedef struct _json_schema_error {
    zend_string *message;
    zend_string *property;
    zend_string *pointer;
    int constraint;
    struct _json_schema_error *next;
} json_schema_error;

/* Validation context structure */
typedef struct _json_schema_context {
    int check_mode;
    json_schema_error *errors;
    json_schema_error *errors_tail;
    int error_count;
    HashTable *definitions;    /* For $ref resolution */
    zval *root_schema;         /* Root schema for $ref resolution */
    zend_string *current_path; /* Current JSON pointer path */
} json_schema_context;

/* Core validation functions */
int json_schema_validate(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_type(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_string(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_number(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_integer(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_array(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_object(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_boolean(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_null(zval *data, zval *schema, json_schema_context *ctx);

/* Constraint validation functions */
int json_schema_validate_enum(zval *data, zval *enum_values, json_schema_context *ctx);
int json_schema_validate_const(zval *data, zval *const_value, json_schema_context *ctx);
int json_schema_validate_format(zval *data, zend_string *format, json_schema_context *ctx);
int json_schema_validate_pattern(zval *data, zend_string *pattern, json_schema_context *ctx);
int json_schema_validate_min_length(zval *data, zend_long min_length, json_schema_context *ctx);
int json_schema_validate_max_length(zval *data, zend_long max_length, json_schema_context *ctx);
int json_schema_validate_minimum(zval *data, zval *minimum, zend_bool exclusive, json_schema_context *ctx);
int json_schema_validate_maximum(zval *data, zval *maximum, zend_bool exclusive, json_schema_context *ctx);
int json_schema_validate_multiple_of(zval *data, zval *multiple_of, json_schema_context *ctx);
int json_schema_validate_min_items(zval *data, zend_long min_items, json_schema_context *ctx);
int json_schema_validate_max_items(zval *data, zend_long max_items, json_schema_context *ctx);
int json_schema_validate_unique_items(zval *data, json_schema_context *ctx);
int json_schema_validate_min_properties(zval *data, zend_long min_props, json_schema_context *ctx);
int json_schema_validate_max_properties(zval *data, zend_long max_props, json_schema_context *ctx);
int json_schema_validate_required(zval *data, zval *required, json_schema_context *ctx);
int json_schema_validate_additional_properties(zval *data, zval *schema, zval *additional, json_schema_context *ctx);
int json_schema_validate_property_names(zval *data, zval *property_names_schema, json_schema_context *ctx);
int json_schema_validate_contains(zval *data, zval *contains_schema, json_schema_context *ctx);
int json_schema_validate_dependencies(zval *data, zval *dependencies, json_schema_context *ctx);

/* Combinators */
int json_schema_validate_all_of(zval *data, zval *schemas, json_schema_context *ctx);
int json_schema_validate_any_of(zval *data, zval *schemas, json_schema_context *ctx);
int json_schema_validate_one_of(zval *data, zval *schemas, json_schema_context *ctx);
int json_schema_validate_not(zval *data, zval *schema, json_schema_context *ctx);
int json_schema_validate_if_then_else(zval *data, zval *if_schema, zval *then_schema, zval *else_schema, json_schema_context *ctx);

/* $ref resolution */
int json_schema_validate_ref(zval *data, zend_string *ref, json_schema_context *ctx);
zval *json_schema_resolve_ref(zend_string *ref, json_schema_context *ctx);

/* Context management */
json_schema_context *json_schema_context_create(int check_mode);
void json_schema_context_free(json_schema_context *ctx);
void json_schema_context_add_error(json_schema_context *ctx, int constraint, const char *message, const char *property);
void json_schema_context_push_path(json_schema_context *ctx, const char *segment);
void json_schema_context_push_path_index(json_schema_context *ctx, zend_long index);
void json_schema_context_pop_path(json_schema_context *ctx);

/* Utility functions */
int json_schema_is_type(zval *data, const char *type);
int json_schema_values_equal(zval *a, zval *b);
zend_string *json_schema_get_type_name(zval *data);
int json_schema_coerce_type(zval *data, const char *target_type);

#endif /* JSON_SCHEMA_VALIDATOR_H */
