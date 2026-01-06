--TEST--
JsonSchema\Constraint class constants
--EXTENSIONS--
json_schema
--FILE--
<?php

use JsonSchema\Constraint;

// Test that all constants are defined
echo "CHECK_MODE_NONE: " . Constraint::CHECK_MODE_NONE . "\n";
echo "CHECK_MODE_NORMAL: " . Constraint::CHECK_MODE_NORMAL . "\n";
echo "CHECK_MODE_TYPE_CAST: " . Constraint::CHECK_MODE_TYPE_CAST . "\n";
echo "CHECK_MODE_COERCE_TYPES: " . Constraint::CHECK_MODE_COERCE_TYPES . "\n";
echo "CHECK_MODE_APPLY_DEFAULTS: " . Constraint::CHECK_MODE_APPLY_DEFAULTS . "\n";
echo "CHECK_MODE_EXCEPTIONS: " . Constraint::CHECK_MODE_EXCEPTIONS . "\n";
echo "CHECK_MODE_DISABLE_FORMAT: " . Constraint::CHECK_MODE_DISABLE_FORMAT . "\n";
echo "CHECK_MODE_EARLY_COERCE: " . Constraint::CHECK_MODE_EARLY_COERCE . "\n";
echo "CHECK_MODE_ONLY_REQUIRED_DEFAULTS: " . Constraint::CHECK_MODE_ONLY_REQUIRED_DEFAULTS . "\n";
echo "CHECK_MODE_VALIDATE_SCHEMA: " . Constraint::CHECK_MODE_VALIDATE_SCHEMA . "\n";

// Test combining flags
$combined = Constraint::CHECK_MODE_COERCE_TYPES | Constraint::CHECK_MODE_APPLY_DEFAULTS;
echo "Combined flags: " . $combined . "\n";

echo "OK\n";
?>
--EXPECT--
CHECK_MODE_NONE: 0
CHECK_MODE_NORMAL: 1
CHECK_MODE_TYPE_CAST: 2
CHECK_MODE_COERCE_TYPES: 4
CHECK_MODE_APPLY_DEFAULTS: 8
CHECK_MODE_EXCEPTIONS: 16
CHECK_MODE_DISABLE_FORMAT: 32
CHECK_MODE_EARLY_COERCE: 64
CHECK_MODE_ONLY_REQUIRED_DEFAULTS: 128
CHECK_MODE_VALIDATE_SCHEMA: 256
Combined flags: 12
OK
