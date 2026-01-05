/*
 * JSON Schema Validator - Core validation logic implementation
 */

#include "php.h"
#include "ext/standard/info.h"
#include "ext/pcre/php_pcre.h"
#include "ext/json/php_json.h"
#include "zend_smart_str.h"
#include "php_json_schema.h"
#include "validator.h"
#include <math.h>

/* Forward declarations */
static int validate_against_schema(zval *data, zval *schema, json_schema_context *ctx);

/* ============================================================================
 * Context Management
 * ========================================================================== */

json_schema_context *json_schema_context_create(int check_mode)
{
    json_schema_context *ctx = emalloc(sizeof(json_schema_context));
    ctx->check_mode = check_mode;
    ctx->errors = NULL;
    ctx->errors_tail = NULL;
    ctx->error_count = 0;
    ctx->definitions = NULL;
    ctx->root_schema = NULL;
    ctx->current_path = zend_string_init("", 0, 0);
    return ctx;
}

void json_schema_context_free(json_schema_context *ctx)
{
    json_schema_error *error = ctx->errors;
    while (error) {
        json_schema_error *next = error->next;
        if (error->message) zend_string_release(error->message);
        if (error->property) zend_string_release(error->property);
        if (error->pointer) zend_string_release(error->pointer);
        efree(error);
        error = next;
    }
    if (ctx->current_path) {
        zend_string_release(ctx->current_path);
    }
    efree(ctx);
}

void json_schema_context_add_error(json_schema_context *ctx, int constraint, const char *message, const char *property)
{
    json_schema_error *error = emalloc(sizeof(json_schema_error));
    error->message = zend_string_init(message, strlen(message), 0);
    error->property = property ? zend_string_init(property, strlen(property), 0) : NULL;
    error->pointer = zend_string_copy(ctx->current_path);
    error->constraint = constraint;
    error->next = NULL;

    if (ctx->errors_tail) {
        ctx->errors_tail->next = error;
    } else {
        ctx->errors = error;
    }
    ctx->errors_tail = error;
    ctx->error_count++;
}

void json_schema_context_push_path(json_schema_context *ctx, const char *segment)
{
    smart_str path = {0};
    smart_str_append(&path, ctx->current_path);
    smart_str_appendc(&path, '/');
    smart_str_appends(&path, segment);
    smart_str_0(&path);

    zend_string_release(ctx->current_path);
    ctx->current_path = smart_str_extract(&path);
}

void json_schema_context_push_path_index(json_schema_context *ctx, zend_long index)
{
    smart_str path = {0};
    smart_str_append(&path, ctx->current_path);
    smart_str_appendc(&path, '/');
    smart_str_append_long(&path, index);
    smart_str_0(&path);

    zend_string_release(ctx->current_path);
    ctx->current_path = smart_str_extract(&path);
}

void json_schema_context_pop_path(json_schema_context *ctx)
{
    char *last_slash = strrchr(ZSTR_VAL(ctx->current_path), '/');
    if (last_slash) {
        size_t new_len = last_slash - ZSTR_VAL(ctx->current_path);
        zend_string *new_path = zend_string_init(ZSTR_VAL(ctx->current_path), new_len, 0);
        zend_string_release(ctx->current_path);
        ctx->current_path = new_path;
    }
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

zend_string *json_schema_get_type_name(zval *data)
{
    switch (Z_TYPE_P(data)) {
        case IS_NULL:
            return zend_string_init("null", 4, 0);
        case IS_TRUE:
        case IS_FALSE:
            return zend_string_init("boolean", 7, 0);
        case IS_LONG:
            return zend_string_init("integer", 7, 0);
        case IS_DOUBLE:
            return zend_string_init("number", 6, 0);
        case IS_STRING:
            return zend_string_init("string", 6, 0);
        case IS_ARRAY:
            /* Check if it's an object or array */
            if (zend_hash_num_elements(Z_ARRVAL_P(data)) == 0) {
                return zend_string_init("array", 5, 0);
            }
            /* Check for sequential array vs object */
            {
                zend_ulong idx;
                zend_string *key;
                zend_ulong expected_idx = 0;

                ZEND_HASH_FOREACH_KEY(Z_ARRVAL_P(data), idx, key) {
                    if (key != NULL || idx != expected_idx) {
                        return zend_string_init("object", 6, 0);
                    }
                    expected_idx++;
                } ZEND_HASH_FOREACH_END();

                return zend_string_init("array", 5, 0);
            }
        case IS_OBJECT:
            return zend_string_init("object", 6, 0);
        default:
            return zend_string_init("unknown", 7, 0);
    }
}

int json_schema_is_type(zval *data, const char *type)
{
    if (strcmp(type, "null") == 0) {
        return Z_TYPE_P(data) == IS_NULL;
    }
    if (strcmp(type, "boolean") == 0) {
        return Z_TYPE_P(data) == IS_TRUE || Z_TYPE_P(data) == IS_FALSE;
    }
    if (strcmp(type, "integer") == 0) {
        if (Z_TYPE_P(data) == IS_LONG) {
            return 1;
        }
        /* Also accept doubles that are whole numbers */
        if (Z_TYPE_P(data) == IS_DOUBLE) {
            double val = Z_DVAL_P(data);
            return floor(val) == val && !isinf(val) && !isnan(val);
        }
        return 0;
    }
    if (strcmp(type, "number") == 0) {
        return Z_TYPE_P(data) == IS_LONG || Z_TYPE_P(data) == IS_DOUBLE;
    }
    if (strcmp(type, "string") == 0) {
        return Z_TYPE_P(data) == IS_STRING;
    }
    if (strcmp(type, "array") == 0) {
        if (Z_TYPE_P(data) != IS_ARRAY) {
            return 0;
        }
        /* Must be sequential array */
        zend_ulong idx;
        zend_string *key;
        zend_ulong expected_idx = 0;

        if (zend_hash_num_elements(Z_ARRVAL_P(data)) == 0) {
            return 1;
        }

        ZEND_HASH_FOREACH_KEY(Z_ARRVAL_P(data), idx, key) {
            if (key != NULL || idx != expected_idx) {
                return 0;
            }
            expected_idx++;
        } ZEND_HASH_FOREACH_END();

        return 1;
    }
    if (strcmp(type, "object") == 0) {
        if (Z_TYPE_P(data) == IS_OBJECT) {
            return 1;
        }
        if (Z_TYPE_P(data) == IS_ARRAY) {
            if (zend_hash_num_elements(Z_ARRVAL_P(data)) == 0) {
                /* Empty array can be considered object */
                return 1;
            }
            /* Check for associative array */
            zend_ulong idx;
            zend_string *key;
            zend_ulong expected_idx = 0;

            ZEND_HASH_FOREACH_KEY(Z_ARRVAL_P(data), idx, key) {
                if (key != NULL || idx != expected_idx) {
                    return 1;
                }
                expected_idx++;
            } ZEND_HASH_FOREACH_END();

            return 0;
        }
        return 0;
    }

    return 0;
}

int json_schema_values_equal(zval *a, zval *b)
{
    if (Z_TYPE_P(a) != Z_TYPE_P(b)) {
        /* Special case: integer and double can be equal */
        if ((Z_TYPE_P(a) == IS_LONG && Z_TYPE_P(b) == IS_DOUBLE) ||
            (Z_TYPE_P(a) == IS_DOUBLE && Z_TYPE_P(b) == IS_LONG)) {
            double da = (Z_TYPE_P(a) == IS_LONG) ? (double)Z_LVAL_P(a) : Z_DVAL_P(a);
            double db = (Z_TYPE_P(b) == IS_LONG) ? (double)Z_LVAL_P(b) : Z_DVAL_P(b);
            return da == db;
        }
        return 0;
    }

    switch (Z_TYPE_P(a)) {
        case IS_NULL:
            return 1;
        case IS_TRUE:
        case IS_FALSE:
            return Z_TYPE_P(a) == Z_TYPE_P(b);
        case IS_LONG:
            return Z_LVAL_P(a) == Z_LVAL_P(b);
        case IS_DOUBLE:
            return Z_DVAL_P(a) == Z_DVAL_P(b);
        case IS_STRING:
            return zend_string_equals(Z_STR_P(a), Z_STR_P(b));
        case IS_ARRAY: {
            HashTable *arr_a = Z_ARRVAL_P(a);
            HashTable *arr_b = Z_ARRVAL_P(b);

            if (zend_hash_num_elements(arr_a) != zend_hash_num_elements(arr_b)) {
                return 0;
            }

            zend_string *key;
            zend_ulong idx;
            zval *val_a;

            ZEND_HASH_FOREACH_KEY_VAL(arr_a, idx, key, val_a) {
                zval *val_b;
                if (key) {
                    val_b = zend_hash_find(arr_b, key);
                } else {
                    val_b = zend_hash_index_find(arr_b, idx);
                }
                if (!val_b || !json_schema_values_equal(val_a, val_b)) {
                    return 0;
                }
            } ZEND_HASH_FOREACH_END();

            return 1;
        }
        default:
            return 0;
    }
}

/* ============================================================================
 * Type Coercion
 * ========================================================================== */

int json_schema_coerce_type(zval *data, const char *target_type)
{
    if (Z_TYPE_P(data) != IS_STRING) {
        return 0;
    }

    zend_string *str = Z_STR_P(data);

    if (strcmp(target_type, "integer") == 0) {
        char *end;
        zend_long val = ZEND_STRTOL(ZSTR_VAL(str), &end, 10);
        if (end == ZSTR_VAL(str) + ZSTR_LEN(str)) {
            zval_ptr_dtor(data);
            ZVAL_LONG(data, val);
            return 1;
        }
        return 0;
    }

    if (strcmp(target_type, "number") == 0) {
        char *end;
        double val = strtod(ZSTR_VAL(str), &end);
        if (end == ZSTR_VAL(str) + ZSTR_LEN(str)) {
            zval_ptr_dtor(data);
            ZVAL_DOUBLE(data, val);
            return 1;
        }
        return 0;
    }

    if (strcmp(target_type, "boolean") == 0) {
        if (zend_string_equals_literal_ci(str, "true") ||
            zend_string_equals_literal(str, "1")) {
            zval_ptr_dtor(data);
            ZVAL_TRUE(data);
            return 1;
        }
        if (zend_string_equals_literal_ci(str, "false") ||
            zend_string_equals_literal(str, "0")) {
            zval_ptr_dtor(data);
            ZVAL_FALSE(data);
            return 1;
        }
        return 0;
    }

    if (strcmp(target_type, "null") == 0) {
        if (ZSTR_LEN(str) == 0 || zend_string_equals_literal_ci(str, "null")) {
            zval_ptr_dtor(data);
            ZVAL_NULL(data);
            return 1;
        }
        return 0;
    }

    return 0;
}

/* ============================================================================
 * String Validation
 * ========================================================================== */

int json_schema_validate_min_length(zval *data, zend_long min_length, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_STRING) {
        return 1; /* Not applicable */
    }

    size_t len = ZSTR_LEN(Z_STR_P(data));
    /* Use multibyte length if available */
#ifdef HAVE_MBSTRING
    len = php_mb_strlen(ZSTR_VAL(Z_STR_P(data)), ZSTR_LEN(Z_STR_P(data)));
#endif

    if ((zend_long)len < min_length) {
        char msg[256];
        snprintf(msg, sizeof(msg), "String is too short (%zu < %ld)", len, min_length);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_MIN_LENGTH, msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_max_length(zval *data, zend_long max_length, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_STRING) {
        return 1;
    }

    size_t len = ZSTR_LEN(Z_STR_P(data));
#ifdef HAVE_MBSTRING
    len = php_mb_strlen(ZSTR_VAL(Z_STR_P(data)), ZSTR_LEN(Z_STR_P(data)));
#endif

    if ((zend_long)len > max_length) {
        char msg[256];
        snprintf(msg, sizeof(msg), "String is too long (%zu > %ld)", len, max_length);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_MAX_LENGTH, msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_pattern(zval *data, zend_string *pattern, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_STRING) {
        return 1;
    }

    /* Convert pattern to PCRE format */
    smart_str regex = {0};
    smart_str_appendc(&regex, '/');
    smart_str_append(&regex, pattern);
    smart_str_appendc(&regex, '/');
    smart_str_appendc(&regex, 'u'); /* UTF-8 support */
    smart_str_0(&regex);

    zend_string *regex_str = smart_str_extract(&regex);
    pcre_cache_entry *pce = pcre_get_compiled_regex_cache(regex_str);
    zend_string_release(regex_str);

    if (!pce) {
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_PATTERN_MISMATCH,
            "Invalid regular expression pattern", NULL);
        return 0;
    }

    zval matches;
    ZVAL_UNDEF(&matches);

    zval retval;
#if PHP_VERSION_ID >= 80400
    php_pcre_match_impl(pce, Z_STR_P(data), &retval, &matches, 0, 0, 0);
#else
    php_pcre_match_impl(pce, Z_STR_P(data), &retval, &matches, 0, 0, 0, 0);
#endif

    int result = Z_LVAL(retval) > 0;
    zval_ptr_dtor(&matches);

    if (!result) {
        char msg[512];
        snprintf(msg, sizeof(msg), "String does not match pattern '%s'", ZSTR_VAL(pattern));
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_PATTERN_MISMATCH, msg, NULL);
        return 0;
    }

    return 1;
}

/* ============================================================================
 * Number Validation
 * ========================================================================== */

static double get_number_value(zval *data)
{
    if (Z_TYPE_P(data) == IS_LONG) {
        return (double)Z_LVAL_P(data);
    }
    return Z_DVAL_P(data);
}

int json_schema_validate_minimum(zval *data, zval *minimum, zend_bool exclusive, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_LONG && Z_TYPE_P(data) != IS_DOUBLE) {
        return 1;
    }

    double value = get_number_value(data);
    double min_val = get_number_value(minimum);

    if (exclusive ? (value <= min_val) : (value < min_val)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Value %g is %s minimum %g",
            value, exclusive ? "not greater than" : "less than", min_val);
        json_schema_context_add_error(ctx,
            exclusive ? JSON_SCHEMA_ERROR_EXCLUSIVE_MINIMUM : JSON_SCHEMA_ERROR_MINIMUM,
            msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_maximum(zval *data, zval *maximum, zend_bool exclusive, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_LONG && Z_TYPE_P(data) != IS_DOUBLE) {
        return 1;
    }

    double value = get_number_value(data);
    double max_val = get_number_value(maximum);

    if (exclusive ? (value >= max_val) : (value > max_val)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Value %g is %s maximum %g",
            value, exclusive ? "not less than" : "greater than", max_val);
        json_schema_context_add_error(ctx,
            exclusive ? JSON_SCHEMA_ERROR_EXCLUSIVE_MAXIMUM : JSON_SCHEMA_ERROR_MAXIMUM,
            msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_multiple_of(zval *data, zval *multiple_of, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_LONG && Z_TYPE_P(data) != IS_DOUBLE) {
        return 1;
    }

    double value = get_number_value(data);
    double divisor = get_number_value(multiple_of);

    if (divisor == 0) {
        return 1;
    }

    double quotient = value / divisor;
    double rounded = round(quotient);

    if (fabs(quotient - rounded) > 1e-10) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Value %g is not a multiple of %g", value, divisor);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_MULTIPLE_OF, msg, NULL);
        return 0;
    }
    return 1;
}

/* ============================================================================
 * Array Validation
 * ========================================================================== */

int json_schema_validate_min_items(zval *data, zend_long min_items, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY) {
        return 1;
    }

    zend_long count = zend_hash_num_elements(Z_ARRVAL_P(data));
    if (count < min_items) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Array has too few items (%ld < %ld)", count, min_items);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_MIN_ITEMS, msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_max_items(zval *data, zend_long max_items, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY) {
        return 1;
    }

    zend_long count = zend_hash_num_elements(Z_ARRVAL_P(data));
    if (count > max_items) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Array has too many items (%ld > %ld)", count, max_items);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_MAX_ITEMS, msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_unique_items(zval *data, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY) {
        return 1;
    }

    HashTable *arr = Z_ARRVAL_P(data);
    zend_ulong count = zend_hash_num_elements(arr);

    if (count <= 1) {
        return 1;
    }

    zval *items[count];
    zend_ulong i = 0;
    zval *item;

    ZEND_HASH_FOREACH_VAL(arr, item) {
        items[i++] = item;
    } ZEND_HASH_FOREACH_END();

    for (i = 0; i < count - 1; i++) {
        for (zend_ulong j = i + 1; j < count; j++) {
            if (json_schema_values_equal(items[i], items[j])) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Array contains duplicate items at indices %lu and %lu", i, j);
                json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_UNIQUE_ITEMS, msg, NULL);
                return 0;
            }
        }
    }

    return 1;
}

int json_schema_validate_contains(zval *data, zval *contains_schema, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY) {
        return 1;
    }

    zval *item;
    int original_error_count = ctx->error_count;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(data), item) {
        int errors_before = ctx->error_count;
        validate_against_schema(item, contains_schema, ctx);

        if (ctx->error_count == errors_before) {
            /* Reset to original error count since we found a match */
            ctx->error_count = original_error_count;
            return 1;
        }
    } ZEND_HASH_FOREACH_END();

    /* Reset errors and add contains error */
    ctx->error_count = original_error_count;
    json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_CONTAINS,
        "Array does not contain any item matching the schema", NULL);
    return 0;
}

/* ============================================================================
 * Object Validation
 * ========================================================================== */

int json_schema_validate_min_properties(zval *data, zend_long min_props, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY && Z_TYPE_P(data) != IS_OBJECT) {
        return 1;
    }

    zend_long count;
    if (Z_TYPE_P(data) == IS_ARRAY) {
        count = zend_hash_num_elements(Z_ARRVAL_P(data));
    } else {
        count = zend_hash_num_elements(Z_OBJPROP_P(data));
    }

    if (count < min_props) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Object has too few properties (%ld < %ld)", count, min_props);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_MIN_PROPERTIES, msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_max_properties(zval *data, zend_long max_props, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY && Z_TYPE_P(data) != IS_OBJECT) {
        return 1;
    }

    zend_long count;
    if (Z_TYPE_P(data) == IS_ARRAY) {
        count = zend_hash_num_elements(Z_ARRVAL_P(data));
    } else {
        count = zend_hash_num_elements(Z_OBJPROP_P(data));
    }

    if (count > max_props) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Object has too many properties (%ld > %ld)", count, max_props);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_MAX_PROPERTIES, msg, NULL);
        return 0;
    }
    return 1;
}

int json_schema_validate_required(zval *data, zval *required, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY && Z_TYPE_P(data) != IS_OBJECT) {
        return 1;
    }
    if (Z_TYPE_P(required) != IS_ARRAY) {
        return 1;
    }

    HashTable *obj = (Z_TYPE_P(data) == IS_ARRAY) ? Z_ARRVAL_P(data) : Z_OBJPROP_P(data);
    zval *prop_name;
    int valid = 1;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(required), prop_name) {
        if (Z_TYPE_P(prop_name) != IS_STRING) {
            continue;
        }

        if (!zend_hash_exists(obj, Z_STR_P(prop_name))) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Required property '%s' is missing", Z_STRVAL_P(prop_name));
            json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_REQUIRED_PROPERTY, msg, Z_STRVAL_P(prop_name));
            valid = 0;
        }
    } ZEND_HASH_FOREACH_END();

    return valid;
}

int json_schema_validate_property_names(zval *data, zval *property_names_schema, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_ARRAY && Z_TYPE_P(data) != IS_OBJECT) {
        return 1;
    }

    HashTable *obj = (Z_TYPE_P(data) == IS_ARRAY) ? Z_ARRVAL_P(data) : Z_OBJPROP_P(data);
    zend_string *key;
    int valid = 1;

    ZEND_HASH_FOREACH_STR_KEY(obj, key) {
        if (!key) continue;

        zval key_val;
        ZVAL_STR_COPY(&key_val, key);

        int errors_before = ctx->error_count;
        validate_against_schema(&key_val, property_names_schema, ctx);

        if (ctx->error_count > errors_before) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Property name '%s' does not match schema", ZSTR_VAL(key));
            json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_PROPERTY_NAMES, msg, ZSTR_VAL(key));
            valid = 0;
        }

        zval_ptr_dtor(&key_val);
    } ZEND_HASH_FOREACH_END();

    return valid;
}

/* ============================================================================
 * Enum and Const Validation
 * ========================================================================== */

int json_schema_validate_enum(zval *data, zval *enum_values, json_schema_context *ctx)
{
    if (Z_TYPE_P(enum_values) != IS_ARRAY) {
        return 1;
    }

    zval *enum_val;
    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(enum_values), enum_val) {
        if (json_schema_values_equal(data, enum_val)) {
            return 1;
        }
    } ZEND_HASH_FOREACH_END();

    json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_ENUM_MISMATCH,
        "Value is not one of the allowed enum values", NULL);
    return 0;
}

int json_schema_validate_const(zval *data, zval *const_value, json_schema_context *ctx)
{
    if (!json_schema_values_equal(data, const_value)) {
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_CONST_MISMATCH,
            "Value does not match const", NULL);
        return 0;
    }
    return 1;
}

/* ============================================================================
 * Format Validation (Draft-04, Draft-06, Draft-07)
 * ========================================================================== */

static int validate_format_email(const char *str, size_t len)
{
    /* Simple email validation */
    const char *at = strchr(str, '@');
    if (!at || at == str || at == str + len - 1) {
        return 0;
    }
    /* Check for multiple @ signs */
    if (strchr(at + 1, '@')) {
        return 0;
    }
    return 1;
}

static int validate_format_uri(const char *str, size_t len)
{
    /* Check for scheme:// pattern */
    const char *colon = strchr(str, ':');
    return colon && colon > str && strncmp(colon, "://", 3) == 0;
}

static int validate_format_date(const char *str, size_t len)
{
    /* YYYY-MM-DD format */
    if (len != 10) return 0;
    if (str[4] != '-' || str[7] != '-') return 0;

    int year, month, day;
    if (sscanf(str, "%4d-%2d-%2d", &year, &month, &day) != 3) return 0;

    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

static int validate_format_time(const char *str, size_t len)
{
    /* HH:MM:SS format (with optional timezone) */
    if (len < 8) return 0;
    if (str[2] != ':' || str[5] != ':') return 0;

    int hour, min, sec;
    if (sscanf(str, "%2d:%2d:%2d", &hour, &min, &sec) != 3) return 0;

    return hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 60;
}

static int validate_format_datetime(const char *str, size_t len)
{
    /* ISO 8601 date-time format */
    if (len < 19) return 0;

    const char *t = strchr(str, 'T');
    if (!t) t = strchr(str, 't');
    if (!t) return 0;

    return validate_format_date(str, t - str) && validate_format_time(t + 1, len - (t - str) - 1);
}

static int validate_format_ipv4(const char *str, size_t len)
{
    int a, b, c, d;
    char extra;

    if (sscanf(str, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) {
        return 0;
    }

    return a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
           c >= 0 && c <= 255 && d >= 0 && d <= 255;
}

static int validate_format_ipv6(const char *str, size_t len)
{
    /* Simplified IPv6 validation */
    int colons = 0;
    int double_colon = 0;

    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c == ':') {
            colons++;
            if (i + 1 < len && str[i + 1] == ':') {
                if (double_colon) return 0; /* Only one :: allowed */
                double_colon = 1;
            }
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }

    return colons >= 2 && colons <= 7;
}

static int validate_format_hostname(const char *str, size_t len)
{
    if (len == 0 || len > 253) return 0;

    /* Simple hostname validation */
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) {
            return 0;
        }
    }

    return 1;
}

static int validate_format_uuid(const char *str, size_t len)
{
    /* UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
    if (len != 36) return 0;
    if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-') return 0;

    for (size_t i = 0; i < len; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }

    return 1;
}

int json_schema_validate_format(zval *data, zend_string *format, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_STRING) {
        return 1;
    }

    const char *str = Z_STRVAL_P(data);
    size_t len = Z_STRLEN_P(data);
    int valid = 1;

    if (zend_string_equals_literal(format, "email")) {
        valid = validate_format_email(str, len);
    } else if (zend_string_equals_literal(format, "uri") ||
               zend_string_equals_literal(format, "uri-reference")) {
        valid = validate_format_uri(str, len);
    } else if (zend_string_equals_literal(format, "date")) {
        valid = validate_format_date(str, len);
    } else if (zend_string_equals_literal(format, "time")) {
        valid = validate_format_time(str, len);
    } else if (zend_string_equals_literal(format, "date-time")) {
        valid = validate_format_datetime(str, len);
    } else if (zend_string_equals_literal(format, "ipv4")) {
        valid = validate_format_ipv4(str, len);
    } else if (zend_string_equals_literal(format, "ipv6")) {
        valid = validate_format_ipv6(str, len);
    } else if (zend_string_equals_literal(format, "hostname")) {
        valid = validate_format_hostname(str, len);
    } else if (zend_string_equals_literal(format, "uuid")) {
        valid = validate_format_uuid(str, len);
    }
    /* Unknown formats are considered valid */

    if (!valid) {
        char msg[256];
        snprintf(msg, sizeof(msg), "String does not match format '%s'", ZSTR_VAL(format));
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_FORMAT, msg, NULL);
        return 0;
    }

    return 1;
}

/* ============================================================================
 * Combinators (allOf, anyOf, oneOf, not)
 * ========================================================================== */

int json_schema_validate_all_of(zval *data, zval *schemas, json_schema_context *ctx)
{
    if (Z_TYPE_P(schemas) != IS_ARRAY) {
        return 1;
    }

    zval *schema;
    int valid = 1;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(schemas), schema) {
        if (!validate_against_schema(data, schema, ctx)) {
            valid = 0;
        }
    } ZEND_HASH_FOREACH_END();

    if (!valid) {
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_ALL_OF,
            "Value does not match all schemas in allOf", NULL);
    }

    return valid;
}

int json_schema_validate_any_of(zval *data, zval *schemas, json_schema_context *ctx)
{
    if (Z_TYPE_P(schemas) != IS_ARRAY) {
        return 1;
    }

    zval *schema;
    int original_error_count = ctx->error_count;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(schemas), schema) {
        int errors_before = ctx->error_count;
        validate_against_schema(data, schema, ctx);

        if (ctx->error_count == errors_before) {
            /* Reset to original error count since we found a match */
            ctx->error_count = original_error_count;
            return 1;
        }
    } ZEND_HASH_FOREACH_END();

    /* Reset errors and add anyOf error */
    ctx->error_count = original_error_count;
    json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_ANY_OF,
        "Value does not match any schema in anyOf", NULL);
    return 0;
}

int json_schema_validate_one_of(zval *data, zval *schemas, json_schema_context *ctx)
{
    if (Z_TYPE_P(schemas) != IS_ARRAY) {
        return 1;
    }

    zval *schema;
    int match_count = 0;
    int original_error_count = ctx->error_count;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(schemas), schema) {
        int errors_before = ctx->error_count;
        validate_against_schema(data, schema, ctx);

        if (ctx->error_count == errors_before) {
            match_count++;
        }
    } ZEND_HASH_FOREACH_END();

    /* Reset error count */
    ctx->error_count = original_error_count;

    if (match_count != 1) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Value matches %d schemas but should match exactly 1 in oneOf", match_count);
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_ONE_OF, msg, NULL);
        return 0;
    }

    return 1;
}

int json_schema_validate_not(zval *data, zval *schema, json_schema_context *ctx)
{
    int original_error_count = ctx->error_count;
    validate_against_schema(data, schema, ctx);

    if (ctx->error_count == original_error_count) {
        /* Validation passed, which means NOT failed */
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_NOT,
            "Value matches schema in 'not'", NULL);
        return 0;
    }

    /* Reset errors since validation should fail */
    ctx->error_count = original_error_count;
    return 1;
}

int json_schema_validate_if_then_else(zval *data, zval *if_schema, zval *then_schema, zval *else_schema, json_schema_context *ctx)
{
    int original_error_count = ctx->error_count;
    validate_against_schema(data, if_schema, ctx);

    int if_passed = (ctx->error_count == original_error_count);
    ctx->error_count = original_error_count;

    if (if_passed) {
        if (then_schema && Z_TYPE_P(then_schema) != IS_NULL) {
            if (!validate_against_schema(data, then_schema, ctx)) {
                json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_IF_THEN_ELSE,
                    "Value matches 'if' but does not match 'then'", NULL);
                return 0;
            }
        }
    } else {
        if (else_schema && Z_TYPE_P(else_schema) != IS_NULL) {
            if (!validate_against_schema(data, else_schema, ctx)) {
                json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_IF_THEN_ELSE,
                    "Value does not match 'if' and does not match 'else'", NULL);
                return 0;
            }
        }
    }

    return 1;
}

/* ============================================================================
 * $ref Resolution
 * ========================================================================== */

zval *json_schema_resolve_ref(zend_string *ref, json_schema_context *ctx)
{
    if (!ctx->root_schema) {
        return NULL;
    }

    const char *ref_str = ZSTR_VAL(ref);

    /* Handle local references (#/...) */
    if (ref_str[0] == '#') {
        if (ref_str[1] == '\0') {
            return ctx->root_schema;
        }

        if (ref_str[1] != '/') {
            return NULL;
        }

        /* Parse JSON Pointer */
        zval *current = ctx->root_schema;
        const char *ptr = ref_str + 2;

        while (*ptr) {
            const char *slash = strchr(ptr, '/');
            size_t segment_len = slash ? (size_t)(slash - ptr) : strlen(ptr);

            char segment[256];
            if (segment_len >= sizeof(segment)) {
                return NULL;
            }
            strncpy(segment, ptr, segment_len);
            segment[segment_len] = '\0';

            /* Decode JSON Pointer escapes */
            char *src = segment;
            char *dst = segment;
            while (*src) {
                if (*src == '~') {
                    if (src[1] == '0') {
                        *dst++ = '~';
                        src += 2;
                    } else if (src[1] == '1') {
                        *dst++ = '/';
                        src += 2;
                    } else {
                        *dst++ = *src++;
                    }
                } else {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';

            if (Z_TYPE_P(current) == IS_ARRAY) {
                zval *next = zend_hash_str_find(Z_ARRVAL_P(current), segment, strlen(segment));
                if (!next) {
                    /* Try numeric index */
                    char *end;
                    zend_long idx = strtol(segment, &end, 10);
                    if (*end == '\0') {
                        next = zend_hash_index_find(Z_ARRVAL_P(current), idx);
                    }
                }
                if (!next) {
                    return NULL;
                }
                current = next;
            } else {
                return NULL;
            }

            if (slash) {
                ptr = slash + 1;
            } else {
                break;
            }
        }

        return current;
    }

    /* External references not supported in this implementation */
    return NULL;
}

int json_schema_validate_ref(zval *data, zend_string *ref, json_schema_context *ctx)
{
    zval *resolved = json_schema_resolve_ref(ref, ctx);
    if (!resolved) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Cannot resolve $ref '%s'", ZSTR_VAL(ref));
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_REF, msg, NULL);
        return 0;
    }

    return validate_against_schema(data, resolved, ctx);
}

/* ============================================================================
 * Type Validation
 * ========================================================================== */

int json_schema_validate_type(zval *data, zval *schema, json_schema_context *ctx)
{
    zval *type = NULL;

    if (Z_TYPE_P(schema) == IS_ARRAY) {
        type = zend_hash_str_find(Z_ARRVAL_P(schema), "type", 4);
    }

    if (!type) {
        return 1; /* No type constraint */
    }

    /* Handle array of types */
    if (Z_TYPE_P(type) == IS_ARRAY) {
        zval *type_val;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(type), type_val) {
            if (Z_TYPE_P(type_val) == IS_STRING) {
                if (json_schema_is_type(data, Z_STRVAL_P(type_val))) {
                    return 1;
                }
            }
        } ZEND_HASH_FOREACH_END();

        zend_string *actual_type = json_schema_get_type_name(data);
        char msg[256];
        snprintf(msg, sizeof(msg), "Type '%s' is not allowed", ZSTR_VAL(actual_type));
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_TYPE_MISMATCH, msg, NULL);
        zend_string_release(actual_type);
        return 0;
    }

    /* Single type */
    if (Z_TYPE_P(type) == IS_STRING) {
        /* Type coercion if enabled */
        if (ctx->check_mode & JSON_SCHEMA_CHECK_MODE_COERCE_TYPES) {
            if (!json_schema_is_type(data, Z_STRVAL_P(type))) {
                json_schema_coerce_type(data, Z_STRVAL_P(type));
            }
        }

        if (!json_schema_is_type(data, Z_STRVAL_P(type))) {
            zend_string *actual_type = json_schema_get_type_name(data);
            char msg[256];
            snprintf(msg, sizeof(msg), "Expected type '%s' but got '%s'",
                Z_STRVAL_P(type), ZSTR_VAL(actual_type));
            json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_TYPE_MISMATCH, msg, NULL);
            zend_string_release(actual_type);
            return 0;
        }
    }

    return 1;
}

/* ============================================================================
 * Main Validation Logic
 * ========================================================================== */

static int validate_against_schema(zval *data, zval *schema, json_schema_context *ctx)
{
    /* Handle boolean schemas (Draft-06+) */
    if (Z_TYPE_P(schema) == IS_TRUE) {
        return 1;
    }
    if (Z_TYPE_P(schema) == IS_FALSE) {
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_TYPE_MISMATCH,
            "Schema is false, validation always fails", NULL);
        return 0;
    }

    if (Z_TYPE_P(schema) != IS_ARRAY) {
        return 1; /* Empty schema validates everything */
    }

    HashTable *schema_ht = Z_ARRVAL_P(schema);
    int valid = 1;
    zval *val;

    /* $ref - if present, should be processed first */
    val = zend_hash_str_find(schema_ht, "$ref", 4);
    if (val && Z_TYPE_P(val) == IS_STRING) {
        if (!json_schema_validate_ref(data, Z_STR_P(val), ctx)) {
            valid = 0;
        }
        /* In Draft-04, $ref should be the only property processed */
        /* In Draft-06+, other properties can be siblings to $ref */
        /* For simplicity, we continue processing other properties */
    }

    /* Type validation */
    if (!json_schema_validate_type(data, schema, ctx)) {
        valid = 0;
    }

    /* Enum validation */
    val = zend_hash_str_find(schema_ht, "enum", 4);
    if (val) {
        if (!json_schema_validate_enum(data, val, ctx)) {
            valid = 0;
        }
    }

    /* Const validation (Draft-06+) */
    val = zend_hash_str_find(schema_ht, "const", 5);
    if (val) {
        if (!json_schema_validate_const(data, val, ctx)) {
            valid = 0;
        }
    }

    /* String constraints */
    if (Z_TYPE_P(data) == IS_STRING) {
        val = zend_hash_str_find(schema_ht, "minLength", 9);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_min_length(data, zval_get_long(val), ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "maxLength", 9);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_max_length(data, zval_get_long(val), ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "pattern", 7);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            if (!json_schema_validate_pattern(data, Z_STR_P(val), ctx)) {
                valid = 0;
            }
        }

        /* Format validation */
        if (!(ctx->check_mode & JSON_SCHEMA_CHECK_MODE_DISABLE_FORMAT)) {
            val = zend_hash_str_find(schema_ht, "format", 6);
            if (val && Z_TYPE_P(val) == IS_STRING) {
                if (!json_schema_validate_format(data, Z_STR_P(val), ctx)) {
                    valid = 0;
                }
            }
        }
    }

    /* Number constraints */
    if (Z_TYPE_P(data) == IS_LONG || Z_TYPE_P(data) == IS_DOUBLE) {
        val = zend_hash_str_find(schema_ht, "minimum", 7);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            zval *exclusive = zend_hash_str_find(schema_ht, "exclusiveMinimum", 16);
            zend_bool is_exclusive = exclusive && (Z_TYPE_P(exclusive) == IS_TRUE ||
                (Z_TYPE_P(exclusive) == IS_LONG && Z_LVAL_P(exclusive)));
            if (!json_schema_validate_minimum(data, val, is_exclusive, ctx)) {
                valid = 0;
            }
        }

        /* Draft-06+ exclusiveMinimum as number */
        val = zend_hash_str_find(schema_ht, "exclusiveMinimum", 16);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_minimum(data, val, 1, ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "maximum", 7);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            zval *exclusive = zend_hash_str_find(schema_ht, "exclusiveMaximum", 16);
            zend_bool is_exclusive = exclusive && (Z_TYPE_P(exclusive) == IS_TRUE ||
                (Z_TYPE_P(exclusive) == IS_LONG && Z_LVAL_P(exclusive)));
            if (!json_schema_validate_maximum(data, val, is_exclusive, ctx)) {
                valid = 0;
            }
        }

        /* Draft-06+ exclusiveMaximum as number */
        val = zend_hash_str_find(schema_ht, "exclusiveMaximum", 16);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_maximum(data, val, 1, ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "multipleOf", 10);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_multiple_of(data, val, ctx)) {
                valid = 0;
            }
        }
    }

    /* Array constraints */
    if (json_schema_is_type(data, "array")) {
        val = zend_hash_str_find(schema_ht, "minItems", 8);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_min_items(data, zval_get_long(val), ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "maxItems", 8);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_max_items(data, zval_get_long(val), ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "uniqueItems", 11);
        if (val && (Z_TYPE_P(val) == IS_TRUE)) {
            if (!json_schema_validate_unique_items(data, ctx)) {
                valid = 0;
            }
        }

        /* Items validation */
        val = zend_hash_str_find(schema_ht, "items", 5);
        if (val) {
            zval *item;
            zend_ulong idx = 0;

            if (Z_TYPE_P(val) == IS_ARRAY) {
                /* Check if it's a schema or tuple validation */
                zval *first = zend_hash_index_find(Z_ARRVAL_P(val), 0);

                if (first && Z_TYPE_P(first) == IS_ARRAY) {
                    /* Tuple validation */
                    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(data), item) {
                        zval *item_schema = zend_hash_index_find(Z_ARRVAL_P(val), idx);
                        if (item_schema) {
                            json_schema_context_push_path_index(ctx, idx);
                            if (!validate_against_schema(item, item_schema, ctx)) {
                                valid = 0;
                            }
                            json_schema_context_pop_path(ctx);
                        }
                        idx++;
                    } ZEND_HASH_FOREACH_END();
                } else {
                    /* Single schema for all items */
                    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(data), item) {
                        json_schema_context_push_path_index(ctx, idx);
                        if (!validate_against_schema(item, val, ctx)) {
                            valid = 0;
                        }
                        json_schema_context_pop_path(ctx);
                        idx++;
                    } ZEND_HASH_FOREACH_END();
                }
            }
        }

        /* Contains validation (Draft-06+) */
        val = zend_hash_str_find(schema_ht, "contains", 8);
        if (val) {
            if (!json_schema_validate_contains(data, val, ctx)) {
                valid = 0;
            }
        }
    }

    /* Object constraints */
    if (json_schema_is_type(data, "object")) {
        val = zend_hash_str_find(schema_ht, "minProperties", 13);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_min_properties(data, zval_get_long(val), ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "maxProperties", 13);
        if (val && (Z_TYPE_P(val) == IS_LONG || Z_TYPE_P(val) == IS_DOUBLE)) {
            if (!json_schema_validate_max_properties(data, zval_get_long(val), ctx)) {
                valid = 0;
            }
        }

        val = zend_hash_str_find(schema_ht, "required", 8);
        if (val) {
            if (!json_schema_validate_required(data, val, ctx)) {
                valid = 0;
            }
        }

        /* Properties validation */
        zval *properties = zend_hash_str_find(schema_ht, "properties", 10);
        zval *pattern_properties = zend_hash_str_find(schema_ht, "patternProperties", 17);
        zval *additional_properties = zend_hash_str_find(schema_ht, "additionalProperties", 20);

        if (properties && Z_TYPE_P(properties) == IS_ARRAY) {
            HashTable *data_ht = (Z_TYPE_P(data) == IS_ARRAY) ? Z_ARRVAL_P(data) : Z_OBJPROP_P(data);
            zend_string *key;
            zval *prop_val;

            ZEND_HASH_FOREACH_STR_KEY_VAL(data_ht, key, prop_val) {
                if (!key) continue;

                zval *prop_schema = zend_hash_find(Z_ARRVAL_P(properties), key);
                int matched = 0;

                if (prop_schema) {
                    json_schema_context_push_path(ctx, ZSTR_VAL(key));
                    if (!validate_against_schema(prop_val, prop_schema, ctx)) {
                        valid = 0;
                    }
                    json_schema_context_pop_path(ctx);
                    matched = 1;
                }

                /* Pattern properties */
                if (pattern_properties && Z_TYPE_P(pattern_properties) == IS_ARRAY) {
                    zend_string *pattern;
                    zval *pattern_schema;

                    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(pattern_properties), pattern, pattern_schema) {
                        if (!pattern) continue;

                        smart_str regex = {0};
                        smart_str_appendc(&regex, '/');
                        smart_str_append(&regex, pattern);
                        smart_str_appendc(&regex, '/');
                        smart_str_appendc(&regex, 'u');
                        smart_str_0(&regex);

                        zend_string *regex_str = smart_str_extract(&regex);
                        pcre_cache_entry *pce = pcre_get_compiled_regex_cache(regex_str);
                        zend_string_release(regex_str);

                        if (pce) {
                            zval matches, retval;
                            ZVAL_UNDEF(&matches);

#if PHP_VERSION_ID >= 80400
                            php_pcre_match_impl(pce, key, &retval, &matches, 0, 0, 0);
#else
                            php_pcre_match_impl(pce, key, &retval, &matches, 0, 0, 0, 0);
#endif

                            if (Z_LVAL(retval) > 0) {
                                json_schema_context_push_path(ctx, ZSTR_VAL(key));
                                if (!validate_against_schema(prop_val, pattern_schema, ctx)) {
                                    valid = 0;
                                }
                                json_schema_context_pop_path(ctx);
                                matched = 1;
                            }

                            zval_ptr_dtor(&matches);
                        }
                    } ZEND_HASH_FOREACH_END();
                }

                /* Additional properties */
                if (!matched && additional_properties) {
                    if (Z_TYPE_P(additional_properties) == IS_FALSE) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Additional property '%s' is not allowed", ZSTR_VAL(key));
                        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_ADDITIONAL_PROPERTIES, msg, ZSTR_VAL(key));
                        valid = 0;
                    } else if (Z_TYPE_P(additional_properties) == IS_ARRAY) {
                        json_schema_context_push_path(ctx, ZSTR_VAL(key));
                        if (!validate_against_schema(prop_val, additional_properties, ctx)) {
                            valid = 0;
                        }
                        json_schema_context_pop_path(ctx);
                    }
                }
            } ZEND_HASH_FOREACH_END();
        }

        /* Property names validation (Draft-06+) */
        val = zend_hash_str_find(schema_ht, "propertyNames", 13);
        if (val) {
            if (!json_schema_validate_property_names(data, val, ctx)) {
                valid = 0;
            }
        }
    }

    /* Combinators */
    val = zend_hash_str_find(schema_ht, "allOf", 5);
    if (val) {
        if (!json_schema_validate_all_of(data, val, ctx)) {
            valid = 0;
        }
    }

    val = zend_hash_str_find(schema_ht, "anyOf", 5);
    if (val) {
        if (!json_schema_validate_any_of(data, val, ctx)) {
            valid = 0;
        }
    }

    val = zend_hash_str_find(schema_ht, "oneOf", 5);
    if (val) {
        if (!json_schema_validate_one_of(data, val, ctx)) {
            valid = 0;
        }
    }

    val = zend_hash_str_find(schema_ht, "not", 3);
    if (val) {
        if (!json_schema_validate_not(data, val, ctx)) {
            valid = 0;
        }
    }

    /* Conditional validation (Draft-07+) */
    zval *if_schema = zend_hash_str_find(schema_ht, "if", 2);
    if (if_schema) {
        zval *then_schema = zend_hash_str_find(schema_ht, "then", 4);
        zval *else_schema = zend_hash_str_find(schema_ht, "else", 4);
        if (!json_schema_validate_if_then_else(data, if_schema, then_schema, else_schema, ctx)) {
            valid = 0;
        }
    }

    return valid;
}

int json_schema_validate(zval *data, zval *schema, json_schema_context *ctx)
{
    /* Store root schema for $ref resolution */
    ctx->root_schema = schema;

    /* Handle definitions/defs */
    if (Z_TYPE_P(schema) == IS_ARRAY) {
        zval *defs = zend_hash_str_find(Z_ARRVAL_P(schema), "definitions", 11);
        if (!defs) {
            defs = zend_hash_str_find(Z_ARRVAL_P(schema), "$defs", 5);
        }
        if (defs && Z_TYPE_P(defs) == IS_ARRAY) {
            ctx->definitions = Z_ARRVAL_P(defs);
        }
    }

    return validate_against_schema(data, schema, ctx);
}
