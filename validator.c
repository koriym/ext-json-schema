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
#include <ctype.h>

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

    /* Recursion depth tracking */
    ctx->depth = 0;
    ctx->max_depth = JSON_SCHEMA_MAX_DEPTH;

    /* Lazy path evaluation - start with reasonable capacity */
    ctx->path_capacity = 32;
    ctx->path_segments = emalloc(ctx->path_capacity * sizeof(json_schema_path_segment));
    ctx->path_depth = 0;

    /* $ref cycle detection */
    ctx->ref_stack_capacity = 16;
    ctx->ref_stack = emalloc(ctx->ref_stack_capacity * sizeof(zend_string *));
    ctx->ref_stack_depth = 0;

    return ctx;
}

/* Helper to free a chain of error nodes */
static void json_schema_free_error_chain(json_schema_error *error)
{
    while (error) {
        json_schema_error *next = error->next;
        if (error->message) zend_string_release(error->message);
        if (error->property) zend_string_release(error->property);
        if (error->pointer) zend_string_release(error->pointer);
        efree(error);
        error = next;
    }
}

void json_schema_context_free(json_schema_context *ctx)
{
    json_schema_free_error_chain(ctx->errors);

    /* Free path segments */
    for (int i = 0; i < ctx->path_depth; i++) {
        if (ctx->path_segments[i].segment) {
            efree(ctx->path_segments[i].segment);
        }
    }
    efree(ctx->path_segments);

    /* Free ref stack */
    for (int i = 0; i < ctx->ref_stack_depth; i++) {
        zend_string_release(ctx->ref_stack[i]);
    }
    efree(ctx->ref_stack);

    efree(ctx);
}

/* Build JSON Pointer path from segments (lazy evaluation) */
zend_string *json_schema_context_build_path(json_schema_context *ctx)
{
    if (ctx->path_depth == 0) {
        return zend_string_init("", 0, 0);
    }

    smart_str path = {0};
    for (int i = 0; i < ctx->path_depth; i++) {
        smart_str_appendc(&path, '/');
        if (ctx->path_segments[i].is_index) {
            smart_str_append_long(&path, ctx->path_segments[i].index);
        } else {
            /* Escape special characters for JSON Pointer (RFC 6901) */
            const char *seg = ctx->path_segments[i].segment;
            while (*seg) {
                if (*seg == '~') {
                    smart_str_appendl(&path, "~0", 2);
                } else if (*seg == '/') {
                    smart_str_appendl(&path, "~1", 2);
                } else {
                    smart_str_appendc(&path, *seg);
                }
                seg++;
            }
        }
    }
    smart_str_0(&path);
    return smart_str_extract(&path);
}

/* Build dot-notation property path from segments */
zend_string *json_schema_context_build_property(json_schema_context *ctx)
{
    if (ctx->path_depth == 0) {
        return zend_string_init("", 0, 0);
    }

    smart_str prop = {0};
    for (int i = 0; i < ctx->path_depth; i++) {
        if (i > 0) {
            smart_str_appendc(&prop, '.');
        }
        if (ctx->path_segments[i].is_index) {
            smart_str_append_long(&prop, ctx->path_segments[i].index);
        } else {
            smart_str_appends(&prop, ctx->path_segments[i].segment);
        }
    }
    smart_str_0(&prop);
    return smart_str_extract(&prop);
}

void json_schema_context_add_error(json_schema_context *ctx, int constraint, const char *message, const char *property)
{
    json_schema_error *error = emalloc(sizeof(json_schema_error));
    error->message = zend_string_init(message, strlen(message), 0);
    error->property = property ? zend_string_init(property, strlen(property), 0)
                               : json_schema_context_build_property(ctx);
    error->pointer = json_schema_context_build_path(ctx);
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

void json_schema_context_truncate_errors(json_schema_context *ctx, int target_count)
{
    if (ctx->error_count <= target_count) {
        return;
    }

    /* Guard against negative target_count */
    if (target_count <= 0) {
        json_schema_free_error_chain(ctx->errors);
        ctx->errors = NULL;
        ctx->errors_tail = NULL;
        ctx->error_count = 0;
        return;
    }

    /* Find the node at target_count position */
    json_schema_error *current = ctx->errors;
    for (int i = 1; i < target_count && current; i++) {
        current = current->next;
    }

    /* Free nodes after current */
    if (current) {
        json_schema_error *to_free = current->next;
        current->next = NULL;
        ctx->errors_tail = current;
        json_schema_free_error_chain(to_free);
    }

    ctx->error_count = target_count;
}

void json_schema_context_push_path(json_schema_context *ctx, const char *segment)
{
    /* Expand capacity if needed */
    if (ctx->path_depth >= ctx->path_capacity) {
        ctx->path_capacity *= 2;
        ctx->path_segments = erealloc(ctx->path_segments,
            ctx->path_capacity * sizeof(json_schema_path_segment));
    }

    /* Add string segment */
    ctx->path_segments[ctx->path_depth].segment = estrdup(segment);
    ctx->path_segments[ctx->path_depth].index = -1;
    ctx->path_segments[ctx->path_depth].is_index = 0;
    ctx->path_depth++;
}

void json_schema_context_push_path_index(json_schema_context *ctx, zend_long index)
{
    /* Expand capacity if needed */
    if (ctx->path_depth >= ctx->path_capacity) {
        ctx->path_capacity *= 2;
        ctx->path_segments = erealloc(ctx->path_segments,
            ctx->path_capacity * sizeof(json_schema_path_segment));
    }

    /* Add index segment */
    ctx->path_segments[ctx->path_depth].segment = NULL;
    ctx->path_segments[ctx->path_depth].index = index;
    ctx->path_segments[ctx->path_depth].is_index = 1;
    ctx->path_depth++;
}

void json_schema_context_pop_path(json_schema_context *ctx)
{
    if (ctx->path_depth > 0) {
        ctx->path_depth--;
        if (ctx->path_segments[ctx->path_depth].segment) {
            efree(ctx->path_segments[ctx->path_depth].segment);
            ctx->path_segments[ctx->path_depth].segment = NULL;
        }
    }
}

/* $ref cycle detection */
int json_schema_context_push_ref(json_schema_context *ctx, zend_string *ref)
{
    /* Check for cycles */
    for (int i = 0; i < ctx->ref_stack_depth; i++) {
        if (zend_string_equals(ctx->ref_stack[i], ref)) {
            return 0; /* Cycle detected */
        }
    }

    /* Check max depth */
    if (ctx->ref_stack_depth >= JSON_SCHEMA_MAX_REF_DEPTH) {
        return 0; /* Too deep */
    }

    /* Expand capacity if needed */
    if (ctx->ref_stack_depth >= ctx->ref_stack_capacity) {
        ctx->ref_stack_capacity *= 2;
        ctx->ref_stack = erealloc(ctx->ref_stack,
            ctx->ref_stack_capacity * sizeof(zend_string *));
    }

    /* Push ref onto stack */
    ctx->ref_stack[ctx->ref_stack_depth] = zend_string_copy(ref);
    ctx->ref_stack_depth++;
    return 1;
}

void json_schema_context_pop_ref(json_schema_context *ctx)
{
    if (ctx->ref_stack_depth > 0) {
        ctx->ref_stack_depth--;
        zend_string_release(ctx->ref_stack[ctx->ref_stack_depth]);
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

                /* Sequential array (including empty) is array */
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
            /* Check for associative array (has string keys) */
            zend_ulong idx;
            zend_string *key;
            zend_ulong expected_idx = 0;

            ZEND_HASH_FOREACH_KEY(Z_ARRVAL_P(data), idx, key) {
                if (key != NULL || idx != expected_idx) {
                    return 1;
                }
                expected_idx++;
            } ZEND_HASH_FOREACH_END();

            /* Sequential array (including empty array) is NOT an object */
            return 0;
        }
        return 0;
    }

    return 0;
}

/* Compute hash for a JSON value - used for O(n) uniqueItems check */
static zend_ulong json_schema_value_hash(zval *val)
{
    zend_ulong hash = Z_TYPE_P(val);

    switch (Z_TYPE_P(val)) {
        case IS_NULL:
            return hash;
        case IS_TRUE:
            return hash ^ 1;
        case IS_FALSE:
            return hash;
        case IS_LONG:
            return hash ^ (zend_ulong)Z_LVAL_P(val);
        case IS_DOUBLE: {
            /* Hash the double's bit representation */
            union { double d; zend_ulong u; } u;
            u.d = Z_DVAL_P(val);
            return hash ^ u.u;
        }
        case IS_STRING:
            return hash ^ ZSTR_HASH(Z_STR_P(val));
        case IS_ARRAY:
        case IS_OBJECT: {
            HashTable *ht = (Z_TYPE_P(val) == IS_ARRAY) ? Z_ARRVAL_P(val) : Z_OBJPROP_P(val);
            zend_ulong count = zend_hash_num_elements(ht);
            hash ^= count << 8;

            /* Check if this is an associative array (object-like) by looking for string keys */
            int has_string_key = 0;
            zend_string *key;
            zend_ulong idx;
            zval *elem;
            ZEND_HASH_FOREACH_KEY(ht, idx, key) {
                if (key) {
                    has_string_key = 1;
                    break;
                }
            } ZEND_HASH_FOREACH_END();

            if (has_string_key) {
                /* For objects/associative arrays: use XOR for order-independence */
                ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, elem) {
                    zend_ulong elem_hash = 0;
                    if (key) {
                        elem_hash = ZSTR_HASH(key);
                    }
                    /* Mix key hash with value hash using multiplication to avoid simple XOR cancellation */
                    elem_hash = (elem_hash * 31) ^ json_schema_value_hash(elem);
                    hash ^= elem_hash;  /* XOR is commutative - order independent */
                } ZEND_HASH_FOREACH_END();
            } else {
                /* For numeric arrays: order matters, use position-sensitive hashing */
                ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, elem) {
                    hash = (hash * 31) ^ (idx << 4) ^ json_schema_value_hash(elem);
                } ZEND_HASH_FOREACH_END();
            }
            return hash;
        }
        default:
            return hash;
    }
}

int json_schema_values_equal(zval *a, zval *b)
{
    /* Handle array/object comparison - both are comparable in JSON context */
    int a_is_container = (Z_TYPE_P(a) == IS_ARRAY || Z_TYPE_P(a) == IS_OBJECT);
    int b_is_container = (Z_TYPE_P(b) == IS_ARRAY || Z_TYPE_P(b) == IS_OBJECT);

    if (a_is_container && b_is_container) {
        HashTable *ht_a = (Z_TYPE_P(a) == IS_ARRAY) ? Z_ARRVAL_P(a) : Z_OBJPROP_P(a);
        HashTable *ht_b = (Z_TYPE_P(b) == IS_ARRAY) ? Z_ARRVAL_P(b) : Z_OBJPROP_P(b);

        if (zend_hash_num_elements(ht_a) != zend_hash_num_elements(ht_b)) {
            return 0;
        }

        zend_string *key;
        zend_ulong idx;
        zval *val_a;

        ZEND_HASH_FOREACH_KEY_VAL(ht_a, idx, key, val_a) {
            zval *val_b;
            if (key) {
                val_b = zend_hash_find(ht_b, key);
            } else {
                val_b = zend_hash_index_find(ht_b, idx);
            }
            if (!val_b || !json_schema_values_equal(val_a, val_b)) {
                return 0;
            }
        } ZEND_HASH_FOREACH_END();

        return 1;
    }

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

/* Count UTF-8 characters (code points) in a string */
static size_t utf8_strlen(const char *str, size_t byte_len)
{
    size_t char_count = 0;
    const unsigned char *s = (const unsigned char *)str;
    const unsigned char *end = s + byte_len;

    while (s < end) {
        if ((*s & 0x80) == 0) {
            /* ASCII: 0xxxxxxx */
            s++;
        } else if ((*s & 0xE0) == 0xC0) {
            /* 2-byte: 110xxxxx */
            s += 2;
        } else if ((*s & 0xF0) == 0xE0) {
            /* 3-byte: 1110xxxx */
            s += 3;
        } else if ((*s & 0xF8) == 0xF0) {
            /* 4-byte: 11110xxx */
            s += 4;
        } else {
            /* Invalid UTF-8, count as single byte */
            s++;
        }
        char_count++;
    }
    return char_count;
}

int json_schema_validate_min_length(zval *data, zend_long min_length, json_schema_context *ctx)
{
    if (Z_TYPE_P(data) != IS_STRING) {
        return 1; /* Not applicable */
    }

    size_t len = utf8_strlen(ZSTR_VAL(Z_STR_P(data)), ZSTR_LEN(Z_STR_P(data)));

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

    size_t len = utf8_strlen(ZSTR_VAL(Z_STR_P(data)), ZSTR_LEN(Z_STR_P(data)));

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

/* Item entry for uniqueItems hash bucket chain */
typedef struct _unique_item_entry {
    zval *item;
    zend_ulong index;
    struct _unique_item_entry *next;
} unique_item_entry;

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

    /* Use hash table with chaining for O(n) average case instead of O(n²) */
    HashTable buckets;
    zend_hash_init(&buckets, count, NULL, NULL, 0);

    zval *item;
    zend_ulong idx = 0;
    int result = 1;
    unique_item_entry *all_entries = emalloc(count * sizeof(unique_item_entry));
    zend_ulong entry_count = 0;

    ZEND_HASH_FOREACH_VAL(arr, item) {
        zend_ulong hash = json_schema_value_hash(item);

        /* Check existing items in this hash bucket */
        zval *bucket_head = zend_hash_index_find(&buckets, hash);
        if (bucket_head) {
            unique_item_entry *entry = (unique_item_entry *)(uintptr_t)Z_LVAL_P(bucket_head);
            while (entry) {
                if (json_schema_values_equal(item, entry->item)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Array contains duplicate items at indices %lu and %lu",
                             entry->index, idx);
                    json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_UNIQUE_ITEMS, msg, NULL);
                    result = 0;
                    break;
                }
                entry = entry->next;
            }
            if (!result) break;
        }

        /* Add new entry to bucket chain */
        unique_item_entry *new_entry = &all_entries[entry_count++];
        new_entry->item = item;
        new_entry->index = idx;

        if (bucket_head) {
            new_entry->next = (unique_item_entry *)(uintptr_t)Z_LVAL_P(bucket_head);
            /* Update the bucket head pointer */
            Z_LVAL_P(bucket_head) = (zend_long)(uintptr_t)new_entry;
        } else {
            new_entry->next = NULL;
            zval head;
            ZVAL_LONG(&head, (zend_long)(uintptr_t)new_entry);
            zend_hash_index_add(&buckets, hash, &head);
        }

        idx++;
    } ZEND_HASH_FOREACH_END();

    efree(all_entries);
    zend_hash_destroy(&buckets);
    return result;
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
            /* Truncate errors to original count since we found a match */
            json_schema_context_truncate_errors(ctx, original_error_count);
            return 1;
        }
    } ZEND_HASH_FOREACH_END();

    /* Truncate errors and add contains error */
    json_schema_context_truncate_errors(ctx, original_error_count);
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

/* Forward declaration */
static int validate_against_schema(zval *data, zval *schema, json_schema_context *ctx);

/* Dependencies validation */
int json_schema_validate_dependencies(zval *data, zval *dependencies, json_schema_context *ctx)
{
    if (!json_schema_is_type(data, "object")) {
        return 1;
    }
    if (Z_TYPE_P(dependencies) != IS_ARRAY) {
        return 1;
    }

    HashTable *data_ht = (Z_TYPE_P(data) == IS_ARRAY) ? Z_ARRVAL_P(data) : Z_OBJPROP_P(data);
    zend_string *dep_key;
    zval *dep_value;
    int valid = 1;

    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(dependencies), dep_key, dep_value) {
        if (!dep_key) continue;

        /* Check if the property exists in data */
        if (!zend_hash_exists(data_ht, dep_key)) {
            continue; /* Property not present, dependency doesn't apply */
        }

        /* Handle boolean schemas */
        if (Z_TYPE_P(dep_value) == IS_TRUE) {
            /* true schema always passes */
            continue;
        }
        if (Z_TYPE_P(dep_value) == IS_FALSE) {
            /* false schema always fails */
            char msg[256];
            snprintf(msg, sizeof(msg), "Dependency failed: '%s' has false schema",
                ZSTR_VAL(dep_key));
            json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_REQUIRED_PROPERTY, msg, NULL);
            valid = 0;
            continue;
        }

        /* Dependency can be an array of required properties or a schema */
        if (Z_TYPE_P(dep_value) == IS_ARRAY) {
            /* Check if it's an array of strings (property dependencies) or a schema */
            zval *first = zend_hash_index_find(Z_ARRVAL_P(dep_value), 0);

            if (first && Z_TYPE_P(first) == IS_STRING) {
                /* Array of required property names */
                zval *required_prop;
                ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(dep_value), required_prop) {
                    if (Z_TYPE_P(required_prop) != IS_STRING) continue;

                    if (!zend_hash_exists(data_ht, Z_STR_P(required_prop))) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Dependency failed: '%s' requires '%s'",
                            ZSTR_VAL(dep_key), Z_STRVAL_P(required_prop));
                        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_REQUIRED_PROPERTY, msg,
                            Z_STRVAL_P(required_prop));
                        valid = 0;
                    }
                } ZEND_HASH_FOREACH_END();
            } else {
                /* Schema dependency */
                if (!validate_against_schema(data, dep_value, ctx)) {
                    valid = 0;
                }
            }
        }
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
            /* Truncate errors to original count since we found a match */
            json_schema_context_truncate_errors(ctx, original_error_count);
            return 1;
        }
    } ZEND_HASH_FOREACH_END();

    /* Truncate errors and add anyOf error */
    json_schema_context_truncate_errors(ctx, original_error_count);
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

    /* Truncate errors to original count */
    json_schema_context_truncate_errors(ctx, original_error_count);

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

    /* Truncate errors since validation should fail */
    json_schema_context_truncate_errors(ctx, original_error_count);
    return 1;
}

int json_schema_validate_if_then_else(zval *data, zval *if_schema, zval *then_schema, zval *else_schema, json_schema_context *ctx)
{
    int original_error_count = ctx->error_count;
    validate_against_schema(data, if_schema, ctx);

    int if_passed = (ctx->error_count == original_error_count);
    json_schema_context_truncate_errors(ctx, original_error_count);

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

            /* Decode percent-encoding first, then JSON Pointer escapes */
            char *src = segment;
            char *dst = segment;

            /* Decode percent-encoding */
            while (*src) {
                if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
                    int high = (src[1] >= 'a') ? (src[1] - 'a' + 10) : ((src[1] >= 'A') ? (src[1] - 'A' + 10) : (src[1] - '0'));
                    int low = (src[2] >= 'a') ? (src[2] - 'a' + 10) : ((src[2] >= 'A') ? (src[2] - 'A' + 10) : (src[2] - '0'));
                    *dst++ = (char)((high << 4) | low);
                    src += 3;
                } else {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';

            /* Decode JSON Pointer escapes (~0 = ~, ~1 = /) */
            src = segment;
            dst = segment;
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
    /*
     * Note: We rely on recursion depth limit to prevent infinite recursion.
     * The $ref cycle detection stack is available for detecting true schema cycles
     * (A -> B -> A) but recursive data structures validating against self-referencing
     * schemas (like {"$ref": "#"}) are handled by the depth limit.
     */
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
    /* Check recursion depth limit */
    if (ctx->depth >= ctx->max_depth) {
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_TYPE_MISMATCH,
            "Maximum validation depth exceeded (possible circular reference)", NULL);
        return 0;
    }
    ctx->depth++;

    int result = 1;

    /* Handle boolean schemas (Draft-06+) */
    if (Z_TYPE_P(schema) == IS_TRUE) {
        ctx->depth--;
        return 1;
    }
    if (Z_TYPE_P(schema) == IS_FALSE) {
        json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_TYPE_MISMATCH,
            "Schema is false, validation always fails", NULL);
        ctx->depth--;
        return 0;
    }

    if (Z_TYPE_P(schema) != IS_ARRAY) {
        ctx->depth--;
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
        zval *additional_items = zend_hash_str_find(schema_ht, "additionalItems", 15);

        if (val) {
            zval *item;
            zend_ulong idx = 0;

            if (Z_TYPE_P(val) == IS_ARRAY) {
                /* Check if it's a schema or tuple validation */
                zval *first = zend_hash_index_find(Z_ARRVAL_P(val), 0);

                if (first && (Z_TYPE_P(first) == IS_ARRAY || Z_TYPE_P(first) == IS_TRUE || Z_TYPE_P(first) == IS_FALSE)) {
                    /* Tuple validation - items is an array of schemas */
                    zend_ulong items_count = zend_hash_num_elements(Z_ARRVAL_P(val));

                    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(data), item) {
                        if (idx < items_count) {
                            /* Validate against items schema */
                            zval *item_schema = zend_hash_index_find(Z_ARRVAL_P(val), idx);
                            if (item_schema) {
                                json_schema_context_push_path_index(ctx, idx);
                                if (!validate_against_schema(item, item_schema, ctx)) {
                                    valid = 0;
                                }
                                json_schema_context_pop_path(ctx);
                            }
                        } else {
                            /* Additional items - beyond the items array */
                            if (additional_items) {
                                if (Z_TYPE_P(additional_items) == IS_FALSE) {
                                    char msg[256];
                                    snprintf(msg, sizeof(msg), "Additional item at index %lu is not allowed", idx);
                                    json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_ADDITIONAL_PROPERTIES, msg, NULL);
                                    valid = 0;
                                } else if (Z_TYPE_P(additional_items) == IS_ARRAY) {
                                    json_schema_context_push_path_index(ctx, idx);
                                    if (!validate_against_schema(item, additional_items, ctx)) {
                                        valid = 0;
                                    }
                                    json_schema_context_pop_path(ctx);
                                }
                                /* true or not present = allow any additional items */
                            }
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
            } else if (Z_TYPE_P(val) == IS_FALSE) {
                /* items: false - no items allowed */
                if (zend_hash_num_elements(Z_ARRVAL_P(data)) > 0) {
                    json_schema_context_add_error(ctx, JSON_SCHEMA_ERROR_TYPE_MISMATCH,
                        "Array items are not allowed", NULL);
                    valid = 0;
                }
            }
            /* items: true - all items allowed (default) */
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

        /* Run if any of properties, patternProperties, or additionalProperties are present */
        if ((properties && Z_TYPE_P(properties) == IS_ARRAY) ||
            (pattern_properties && Z_TYPE_P(pattern_properties) == IS_ARRAY) ||
            additional_properties) {

            HashTable *data_ht = (Z_TYPE_P(data) == IS_ARRAY) ? Z_ARRVAL_P(data) : Z_OBJPROP_P(data);
            zend_string *key;
            zval *prop_val;

            ZEND_HASH_FOREACH_STR_KEY_VAL(data_ht, key, prop_val) {
                if (!key) continue;

                int matched = 0;

                /* Check properties */
                if (properties && Z_TYPE_P(properties) == IS_ARRAY) {
                    zval *prop_schema = zend_hash_find(Z_ARRVAL_P(properties), key);
                    if (prop_schema) {
                        json_schema_context_push_path(ctx, ZSTR_VAL(key));
                        if (!validate_against_schema(prop_val, prop_schema, ctx)) {
                            valid = 0;
                        }
                        json_schema_context_pop_path(ctx);
                        matched = 1;
                    }
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

        /* Dependencies validation */
        val = zend_hash_str_find(schema_ht, "dependencies", 12);
        if (val) {
            if (!json_schema_validate_dependencies(data, val, ctx)) {
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

    ctx->depth--;
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
