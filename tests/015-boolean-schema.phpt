--TEST--
Boolean schema (Draft-06+)
--EXTENSIONS--
json_schema
--FILE--
<?php

// true schema - always valid
var_dump(json_schema_validate('anything', true));
var_dump(json_schema_validate(123, true));
var_dump(json_schema_validate(['a' => 1], true));

// false schema - always invalid
var_dump(json_schema_validate('anything', false));
var_dump(json_schema_validate(123, false));
var_dump(json_schema_validate(null, false));

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
bool(false)
OK
