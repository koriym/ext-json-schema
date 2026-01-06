--TEST--
Format validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test email format
$schema = ['type' => 'string', 'format' => 'email'];
var_dump(json_schema_validate('test@example.com', $schema)); // true
var_dump(json_schema_validate('invalid-email', $schema));    // false

// Test date format
$schema = ['type' => 'string', 'format' => 'date'];
var_dump(json_schema_validate('2024-01-15', $schema)); // true
var_dump(json_schema_validate('2024/01/15', $schema)); // false

// Test ipv4 format
$schema = ['type' => 'string', 'format' => 'ipv4'];
var_dump(json_schema_validate('192.168.1.1', $schema)); // true
var_dump(json_schema_validate('999.999.999.999', $schema)); // false

// Test uuid format
$schema = ['type' => 'string', 'format' => 'uuid'];
var_dump(json_schema_validate('550e8400-e29b-41d4-a716-446655440000', $schema)); // true
var_dump(json_schema_validate('not-a-uuid', $schema)); // false

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
OK
