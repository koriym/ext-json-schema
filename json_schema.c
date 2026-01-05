/*
 * JSON Schema Validator for PHP
 *
 * A PECL extension that provides JSON Schema validation functionality.
 * Based on the API design of jsonrainbow/json-schema.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include "ext/json/php_json.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "php_json_schema.h"
#include "validator.h"

/* Class entries */
zend_class_entry *json_schema_validator_ce;
zend_class_entry *json_schema_constraint_ce;
zend_class_entry *json_schema_exception_ce;

/* Object handlers */
static zend_object_handlers json_schema_validator_handlers;

/* Validator object structure */
typedef struct _json_schema_validator_object {
    int check_mode;
    zval errors;        /* Array of error objects */
    zend_object std;
} json_schema_validator_object;

static inline json_schema_validator_object *json_schema_validator_from_obj(zend_object *obj) {
    return (json_schema_validator_object *)((char *)(obj) - XtOffsetOf(json_schema_validator_object, std));
}

#define Z_JSON_SCHEMA_VALIDATOR_P(zv) json_schema_validator_from_obj(Z_OBJ_P(zv))

/* ============================================================================
 * Validator Class Methods
 * ========================================================================== */

static zend_object *json_schema_validator_create_object(zend_class_entry *ce)
{
    json_schema_validator_object *intern = zend_object_alloc(sizeof(json_schema_validator_object), ce);

    intern->check_mode = JSON_SCHEMA_CHECK_MODE_NORMAL;
    array_init(&intern->errors);

    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);

    intern->std.handlers = &json_schema_validator_handlers;

    return &intern->std;
}

static void json_schema_validator_free_object(zend_object *obj)
{
    json_schema_validator_object *intern = json_schema_validator_from_obj(obj);

    zval_ptr_dtor(&intern->errors);
    zend_object_std_dtor(&intern->std);
}

/* Convert validation errors to PHP array */
static void errors_to_array(json_schema_context *ctx, zval *errors_array)
{
    json_schema_error *error = ctx->errors;

    while (error) {
        zval error_obj;
        array_init(&error_obj);

        add_assoc_str(&error_obj, "message", zend_string_copy(error->message));

        if (error->property) {
            add_assoc_str(&error_obj, "property", zend_string_copy(error->property));
        } else {
            add_assoc_null(&error_obj, "property");
        }

        if (error->pointer) {
            add_assoc_str(&error_obj, "pointer", zend_string_copy(error->pointer));
        } else {
            add_assoc_string(&error_obj, "pointer", "");
        }

        add_assoc_long(&error_obj, "constraint", error->constraint);

        add_next_index_zval(errors_array, &error_obj);
        error = error->next;
    }
}

/* {{{ proto void JsonSchema\Validator::__construct(int $checkMode = Constraint::CHECK_MODE_NORMAL) */
PHP_METHOD(JsonSchema_Validator, __construct)
{
    zend_long check_mode = JSON_SCHEMA_CHECK_MODE_NORMAL;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(check_mode)
    ZEND_PARSE_PARAMETERS_END();

    json_schema_validator_object *intern = Z_JSON_SCHEMA_VALIDATOR_P(ZEND_THIS);
    intern->check_mode = (int)check_mode;
}
/* }}} */

/* {{{ proto bool JsonSchema\Validator::validate(mixed $data, mixed $schema, int $checkMode = null)
   Validates data against a JSON Schema */
PHP_METHOD(JsonSchema_Validator, validate)
{
    zval *data;
    zval *schema;
    zend_long check_mode = -1;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_ZVAL(data)
        Z_PARAM_ZVAL(schema)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(check_mode)
    ZEND_PARSE_PARAMETERS_END();

    json_schema_validator_object *intern = Z_JSON_SCHEMA_VALIDATOR_P(ZEND_THIS);

    /* Use instance check_mode if not specified */
    if (check_mode < 0) {
        check_mode = intern->check_mode;
    }

    /* Clear previous errors */
    zval_ptr_dtor(&intern->errors);
    array_init(&intern->errors);

    /* Create validation context */
    json_schema_context *ctx = json_schema_context_create((int)check_mode);

    /* Perform validation */
    int result = json_schema_validate(data, schema, ctx);

    /* Convert errors to PHP array */
    errors_to_array(ctx, &intern->errors);

    /* Check for exception mode */
    if (!result && (check_mode & JSON_SCHEMA_CHECK_MODE_EXCEPTIONS)) {
        json_schema_context_free(ctx);

        zend_throw_exception(json_schema_exception_ce,
            "JSON Schema validation failed", 0);
        RETURN_THROWS();
    }

    json_schema_context_free(ctx);

    RETURN_BOOL(result);
}
/* }}} */

/* {{{ proto bool JsonSchema\Validator::isValid()
   Returns whether the last validation was valid */
PHP_METHOD(JsonSchema_Validator, isValid)
{
    ZEND_PARSE_PARAMETERS_NONE();

    json_schema_validator_object *intern = Z_JSON_SCHEMA_VALIDATOR_P(ZEND_THIS);

    RETURN_BOOL(zend_hash_num_elements(Z_ARRVAL(intern->errors)) == 0);
}
/* }}} */

/* {{{ proto array JsonSchema\Validator::getErrors()
   Returns the validation errors from the last validation */
PHP_METHOD(JsonSchema_Validator, getErrors)
{
    ZEND_PARSE_PARAMETERS_NONE();

    json_schema_validator_object *intern = Z_JSON_SCHEMA_VALIDATOR_P(ZEND_THIS);

    RETURN_ZVAL(&intern->errors, 1, 0);
}
/* }}} */

/* {{{ proto void JsonSchema\Validator::reset()
   Resets the validator state */
PHP_METHOD(JsonSchema_Validator, reset)
{
    ZEND_PARSE_PARAMETERS_NONE();

    json_schema_validator_object *intern = Z_JSON_SCHEMA_VALIDATOR_P(ZEND_THIS);

    zval_ptr_dtor(&intern->errors);
    array_init(&intern->errors);
}
/* }}} */

/* {{{ proto int JsonSchema\Validator::getCheckMode()
   Returns the current check mode */
PHP_METHOD(JsonSchema_Validator, getCheckMode)
{
    ZEND_PARSE_PARAMETERS_NONE();

    json_schema_validator_object *intern = Z_JSON_SCHEMA_VALIDATOR_P(ZEND_THIS);

    RETURN_LONG(intern->check_mode);
}
/* }}} */

/* {{{ proto void JsonSchema\Validator::setCheckMode(int $checkMode)
   Sets the check mode */
PHP_METHOD(JsonSchema_Validator, setCheckMode)
{
    zend_long check_mode;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(check_mode)
    ZEND_PARSE_PARAMETERS_END();

    json_schema_validator_object *intern = Z_JSON_SCHEMA_VALIDATOR_P(ZEND_THIS);
    intern->check_mode = (int)check_mode;
}
/* }}} */

/* ============================================================================
 * Procedural API Functions
 * ========================================================================== */

/* {{{ proto bool json_schema_validate(mixed $data, mixed $schema, int $checkMode = 0)
   Validates data against a JSON Schema */
PHP_FUNCTION(json_schema_validate)
{
    zval *data;
    zval *schema;
    zend_long check_mode = JSON_SCHEMA_CHECK_MODE_NORMAL;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_ZVAL(data)
        Z_PARAM_ZVAL(schema)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(check_mode)
    ZEND_PARSE_PARAMETERS_END();

    json_schema_context *ctx = json_schema_context_create((int)check_mode);
    int result = json_schema_validate(data, schema, ctx);
    json_schema_context_free(ctx);

    RETURN_BOOL(result);
}
/* }}} */

/* {{{ proto array json_schema_validate_with_errors(mixed $data, mixed $schema, int $checkMode = 0)
   Validates data against a JSON Schema and returns errors */
PHP_FUNCTION(json_schema_validate_with_errors)
{
    zval *data;
    zval *schema;
    zend_long check_mode = JSON_SCHEMA_CHECK_MODE_NORMAL;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_ZVAL(data)
        Z_PARAM_ZVAL(schema)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(check_mode)
    ZEND_PARSE_PARAMETERS_END();

    json_schema_context *ctx = json_schema_context_create((int)check_mode);
    int result = json_schema_validate(data, schema, ctx);

    array_init(return_value);
    add_assoc_bool(return_value, "valid", result);

    zval errors;
    array_init(&errors);
    errors_to_array(ctx, &errors);
    add_assoc_zval(return_value, "errors", &errors);

    json_schema_context_free(ctx);
}
/* }}} */

/* {{{ proto array json_schema_get_errors(mixed $data, mixed $schema, int $checkMode = 0)
   Validates data against a JSON Schema and returns only the errors array */
PHP_FUNCTION(json_schema_get_errors)
{
    zval *data;
    zval *schema;
    zend_long check_mode = JSON_SCHEMA_CHECK_MODE_NORMAL;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_ZVAL(data)
        Z_PARAM_ZVAL(schema)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(check_mode)
    ZEND_PARSE_PARAMETERS_END();

    json_schema_context *ctx = json_schema_context_create((int)check_mode);
    json_schema_validate(data, schema, ctx);

    array_init(return_value);
    errors_to_array(ctx, return_value);

    json_schema_context_free(ctx);
}
/* }}} */

/* ============================================================================
 * Argument Info Definitions
 * ========================================================================== */

ZEND_BEGIN_ARG_INFO_EX(arginfo_json_schema_validator_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, checkMode, IS_LONG, 0, "JsonSchema\\Constraint::CHECK_MODE_NORMAL")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validator_validate, 0, 2, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, data, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO(0, schema, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, checkMode, IS_LONG, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validator_is_valid, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validator_get_errors, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validator_reset, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validator_get_check_mode, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validator_set_check_mode, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, checkMode, IS_LONG, 0)
ZEND_END_ARG_INFO()

/* Procedural API argument info */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validate, 0, 2, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, data, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO(0, schema, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, checkMode, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_validate_with_errors, 0, 2, IS_ARRAY, 0)
    ZEND_ARG_TYPE_INFO(0, data, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO(0, schema, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, checkMode, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_json_schema_get_errors, 0, 2, IS_ARRAY, 0)
    ZEND_ARG_TYPE_INFO(0, data, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO(0, schema, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, checkMode, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

/* ============================================================================
 * Method Entries
 * ========================================================================== */

static const zend_function_entry json_schema_validator_methods[] = {
    PHP_ME(JsonSchema_Validator, __construct, arginfo_json_schema_validator_construct, ZEND_ACC_PUBLIC)
    PHP_ME(JsonSchema_Validator, validate, arginfo_json_schema_validator_validate, ZEND_ACC_PUBLIC)
    PHP_ME(JsonSchema_Validator, isValid, arginfo_json_schema_validator_is_valid, ZEND_ACC_PUBLIC)
    PHP_ME(JsonSchema_Validator, getErrors, arginfo_json_schema_validator_get_errors, ZEND_ACC_PUBLIC)
    PHP_ME(JsonSchema_Validator, reset, arginfo_json_schema_validator_reset, ZEND_ACC_PUBLIC)
    PHP_ME(JsonSchema_Validator, getCheckMode, arginfo_json_schema_validator_get_check_mode, ZEND_ACC_PUBLIC)
    PHP_ME(JsonSchema_Validator, setCheckMode, arginfo_json_schema_validator_set_check_mode, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ============================================================================
 * Module Functions
 * ========================================================================== */

static const zend_function_entry json_schema_functions[] = {
    PHP_FE(json_schema_validate, arginfo_json_schema_validate)
    PHP_FE(json_schema_validate_with_errors, arginfo_json_schema_validate_with_errors)
    PHP_FE(json_schema_get_errors, arginfo_json_schema_get_errors)
    PHP_FE_END
};

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(json_schema)
{
    zend_class_entry ce;

    /* Register JsonSchema\Validator class */
    INIT_NS_CLASS_ENTRY(ce, "JsonSchema", "Validator", json_schema_validator_methods);
    json_schema_validator_ce = zend_register_internal_class(&ce);
    json_schema_validator_ce->create_object = json_schema_validator_create_object;

    memcpy(&json_schema_validator_handlers, &std_object_handlers, sizeof(zend_object_handlers));
    json_schema_validator_handlers.offset = XtOffsetOf(json_schema_validator_object, std);
    json_schema_validator_handlers.free_obj = json_schema_validator_free_object;

    /* Register JsonSchema\Constraint class (constants only) */
    INIT_NS_CLASS_ENTRY(ce, "JsonSchema", "Constraint", NULL);
    json_schema_constraint_ce = zend_register_internal_class(&ce);

    /* Register check mode constants */
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_NONE", sizeof("CHECK_MODE_NONE") - 1, JSON_SCHEMA_CHECK_MODE_NONE);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_NORMAL", sizeof("CHECK_MODE_NORMAL") - 1, JSON_SCHEMA_CHECK_MODE_NORMAL);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_TYPE_CAST", sizeof("CHECK_MODE_TYPE_CAST") - 1, JSON_SCHEMA_CHECK_MODE_TYPE_CAST);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_COERCE_TYPES", sizeof("CHECK_MODE_COERCE_TYPES") - 1, JSON_SCHEMA_CHECK_MODE_COERCE_TYPES);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_APPLY_DEFAULTS", sizeof("CHECK_MODE_APPLY_DEFAULTS") - 1, JSON_SCHEMA_CHECK_MODE_APPLY_DEFAULTS);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_EXCEPTIONS", sizeof("CHECK_MODE_EXCEPTIONS") - 1, JSON_SCHEMA_CHECK_MODE_EXCEPTIONS);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_DISABLE_FORMAT", sizeof("CHECK_MODE_DISABLE_FORMAT") - 1, JSON_SCHEMA_CHECK_MODE_DISABLE_FORMAT);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_EARLY_COERCE", sizeof("CHECK_MODE_EARLY_COERCE") - 1, JSON_SCHEMA_CHECK_MODE_EARLY_COERCE);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_ONLY_REQUIRED_DEFAULTS", sizeof("CHECK_MODE_ONLY_REQUIRED_DEFAULTS") - 1, JSON_SCHEMA_CHECK_MODE_ONLY_REQUIRED_DEFAULTS);
    zend_declare_class_constant_long(json_schema_constraint_ce, "CHECK_MODE_VALIDATE_SCHEMA", sizeof("CHECK_MODE_VALIDATE_SCHEMA") - 1, JSON_SCHEMA_CHECK_MODE_VALIDATE_SCHEMA);

    /* Register JsonSchema\Exception class */
    INIT_NS_CLASS_ENTRY(ce, "JsonSchema", "ValidationException", NULL);
    json_schema_exception_ce = zend_register_internal_class_ex(&ce, zend_ce_exception);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(json_schema)
{
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(json_schema)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "json_schema support", "enabled");
    php_info_print_table_row(2, "Version", PHP_JSON_SCHEMA_VERSION);
    php_info_print_table_row(2, "JSON Schema Draft Support", "Draft-04, Draft-06, Draft-07");
    php_info_print_table_end();
}
/* }}} */

/* {{{ json_schema_module_entry */
zend_module_entry json_schema_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_JSON_SCHEMA_EXTNAME,
    json_schema_functions,
    PHP_MINIT(json_schema),
    PHP_MSHUTDOWN(json_schema),
    NULL,  /* RINIT */
    NULL,  /* RSHUTDOWN */
    PHP_MINFO(json_schema),
    PHP_JSON_SCHEMA_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_JSON_SCHEMA
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(json_schema)
#endif
