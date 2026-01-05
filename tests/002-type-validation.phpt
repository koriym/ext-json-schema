--TEST--
Type validation
--EXTENSIONS--
json_schema
--FILE--
<?php

// Test all basic types
$types = [
    ['type' => 'string', 'valid' => 'hello', 'invalid' => 123],
    ['type' => 'integer', 'valid' => 42, 'invalid' => 'hello'],
    ['type' => 'number', 'valid' => 3.14, 'invalid' => 'hello'],
    ['type' => 'boolean', 'valid' => true, 'invalid' => 'true'],
    ['type' => 'null', 'valid' => null, 'invalid' => ''],
    ['type' => 'array', 'valid' => [1, 2, 3], 'invalid' => ['a' => 1]],
    ['type' => 'object', 'valid' => ['a' => 1], 'invalid' => [1, 2, 3]],
];

foreach ($types as $test) {
    $schema = ['type' => $test['type']];

    $validResult = json_schema_validate($test['valid'], $schema);
    $invalidResult = json_schema_validate($test['invalid'], $schema);

    echo $test['type'] . ': valid=' . ($validResult ? 'true' : 'false') .
         ', invalid=' . ($invalidResult ? 'true' : 'false') . "\n";
}

echo "OK\n";
?>
--EXPECT--
string: valid=true, invalid=false
integer: valid=true, invalid=false
number: valid=true, invalid=false
boolean: valid=true, invalid=false
null: valid=true, invalid=false
array: valid=true, invalid=false
object: valid=true, invalid=false
OK
