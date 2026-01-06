<?php

declare(strict_types=1);

namespace JsonSchema;

use JsonSchema\Constraints\BaseConstraint;
use JsonSchema\Constraints\Constraint;

/**
 * PECL-accelerated JSON Schema Validator (Adapter)
 *
 * This class extends the jsonrainbow/json-schema BaseConstraint to provide
 * full API compatibility while using the PECL extension for validation.
 *
 * @see https://github.com/jsonrainbow/json-schema
 */
class ValidatorAdapter extends BaseConstraint
{
    public const ERROR_NONE = 0x00000000;
    public const ERROR_ALL = 0xFFFFFFFF;
    public const ERROR_DOCUMENT_VALIDATION = 0x00000001;
    public const ERROR_SCHEMA_VALIDATION = 0x00000002;

    public const SCHEMA_MEDIA_TYPE = 'application/schema+json';

    /**
     * @var bool Whether the PECL extension is available
     */
    private bool $peclAvailable;

    /**
     * Constructor
     *
     * @param mixed $factory Factory instance for constraint creation
     */
    public function __construct($factory = null)
    {
        parent::__construct($factory);
        $this->peclAvailable = extension_loaded('json_schema');
    }

    /**
     * Validates the given data against the schema
     *
     * @param mixed    $value     The value to validate (passed by reference for coercion)
     * @param mixed    $schema    The schema to validate against
     * @param int|null $checkMode Validation mode flags
     *
     * @return int Error mask
     */
    public function validate(&$value, $schema = null, ?int $checkMode = null): int
    {
        $this->reset();

        if ($schema === null) {
            return self::ERROR_NONE;
        }

        // Use PECL extension if available
        if ($this->peclAvailable) {
            return $this->validateWithPecl($value, $schema, $checkMode ?? Constraint::CHECK_MODE_NORMAL);
        }

        // Fallback to parent implementation (pure PHP)
        return parent::validate($value, $schema, $checkMode); // @codeCoverageIgnore
    }

    /**
     * Validate using the PECL extension
     */
    private function validateWithPecl(&$value, $schema, int $checkMode): int
    {
        // Convert schema to array if it's an object
        $schemaArray = $this->toArray($schema);

        // Map check mode to PECL extension mode
        $peclMode = $this->mapCheckMode($checkMode);

        // Perform validation and get errors in a single call
        $result = \json_schema_validate_with_errors($value, $schemaArray, $peclMode);

        if (!$result['valid']) {
            foreach ($result['errors'] as $error) {
                $this->addErrorFromPecl($error);
            }

            return self::ERROR_DOCUMENT_VALIDATION;
        }

        return self::ERROR_NONE;
    }

    /**
     * Add error from PECL extension format to BaseConstraint format
     */
    private function addErrorFromPecl(array $error): void
    {
        $this->addErrors([[
            'property' => $error['property'] ?? '',
            'pointer' => $error['pointer'] ?? '',
            'message' => $error['message'] ?? 'Validation error',
            'constraint' => $error['constraint'] ?? '',
            'context' => self::ERROR_DOCUMENT_VALIDATION,
        ]]);
    }

    /**
     * Map jsonrainbow check modes to PECL extension modes
     */
    private function mapCheckMode(int $checkMode): int
    {
        $peclMode = 0;

        if (defined('JSON_SCHEMA_CHECK_MODE_TYPE_CAST') && ($checkMode & Constraint::CHECK_MODE_TYPE_CAST)) {
            $peclMode |= \JSON_SCHEMA_CHECK_MODE_TYPE_CAST; // @codeCoverageIgnore
        }
        if (defined('JSON_SCHEMA_CHECK_MODE_COERCE_TYPES') && ($checkMode & Constraint::CHECK_MODE_COERCE_TYPES)) {
            $peclMode |= \JSON_SCHEMA_CHECK_MODE_COERCE_TYPES; // @codeCoverageIgnore
        }
        if (defined('JSON_SCHEMA_CHECK_MODE_APPLY_DEFAULTS') && ($checkMode & Constraint::CHECK_MODE_APPLY_DEFAULTS)) {
            $peclMode |= \JSON_SCHEMA_CHECK_MODE_APPLY_DEFAULTS; // @codeCoverageIgnore
        }
        if (defined('JSON_SCHEMA_CHECK_MODE_DISABLE_FORMAT') && ($checkMode & Constraint::CHECK_MODE_DISABLE_FORMAT)) {
            $peclMode |= \JSON_SCHEMA_CHECK_MODE_DISABLE_FORMAT; // @codeCoverageIgnore
        }

        return $peclMode;
    }

    /**
     * Convert object/array to array recursively
     */
    private function toArray($data)
    {
        if (is_object($data)) {
            $data = get_object_vars($data);
        }
        if (is_array($data)) {
            return array_map([$this, 'toArray'], $data);
        }
        return $data;
    }

    /**
     * Check if the PECL extension is being used
     *
     * @return bool
     */
    public function isPeclAccelerated(): bool
    {
        return $this->peclAvailable;
    }

    /**
     * @deprecated Use validate() instead
     */
    public function check(&$value, $schema): int
    {
        return $this->validate($value, $schema);
    }

    /**
     * @deprecated Use validate() with CHECK_MODE_COERCE_TYPES instead
     */
    public function coerce(&$value, $schema): int
    {
        return $this->validate($value, $schema, Constraint::CHECK_MODE_COERCE_TYPES);
    }
}
