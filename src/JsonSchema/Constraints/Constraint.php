<?php

declare(strict_types=1);

namespace JsonSchema\Constraints;

/**
 * Constraint constants for JSON Schema validation modes
 *
 * These constants are compatible with jsonrainbow/json-schema
 */
class Constraint
{
    public const CHECK_MODE_NONE = 0x00000000;
    public const CHECK_MODE_NORMAL = 0x00000001;
    public const CHECK_MODE_TYPE_CAST = 0x00000002;
    public const CHECK_MODE_COERCE_TYPES = 0x00000004;
    public const CHECK_MODE_APPLY_DEFAULTS = 0x00000008;
    public const CHECK_MODE_EXCEPTIONS = 0x00000010;
    public const CHECK_MODE_DISABLE_FORMAT = 0x00000020;
    public const CHECK_MODE_EARLY_COERCE = 0x00000040;
    public const CHECK_MODE_ONLY_REQUIRED_DEFAULTS = 0x00000080;
    public const CHECK_MODE_VALIDATE_SCHEMA = 0x00000100;
    public const CHECK_MODE_STRICT = 0x00000200;
}
