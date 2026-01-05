<?php

declare(strict_types=1);

/**
 * JsonSchema\Validator Bridge
 *
 * This file provides compatibility between the PECL json_schema extension
 * and the jsonrainbow/json-schema library.
 *
 * Usage scenarios:
 *
 * 1. PECL extension only (no jsonrainbow):
 *    - Use \JsonSchema\Validator directly from the extension
 *    - This file is not needed
 *
 * 2. jsonrainbow/json-schema + PECL extension:
 *    - Install both: composer require justinrainbow/json-schema
 *    - Include this adapter to get a Validator that extends BaseConstraint
 *      but uses the PECL extension for performance
 *
 * Example:
 *   use JsonSchema\ValidatorAdapter as Validator;
 *   $validator = new Validator();
 *   $validator->validate($data, $schema);
 */

namespace JsonSchema;

// This file only provides the bridge when jsonrainbow is installed
// The PECL extension already provides the main \JsonSchema\Validator class

if (class_exists(\JsonSchema\Constraints\BaseConstraint::class, true)) {
    // jsonrainbow/json-schema is installed, include the adapter
    require_once __DIR__ . '/ValidatorAdapter.php';
}

// Also provide a type alias for convenience
if (!class_exists(PeclValidator::class, false) && extension_loaded('json_schema')) {
    /**
     * Alias to the PECL extension's Validator for explicit usage
     */
    class_alias(\JsonSchema\Validator::class, PeclValidator::class);
}
