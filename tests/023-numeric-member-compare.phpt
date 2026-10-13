--TEST--
const, enum and uniqueItems match numeric member names in arrays and objects
--EXTENSIONS--
json_schema
--FILE--
<?php

$const = ['const' => ['1' => 'a']];
var_dump(json_schema_validate(json_decode('{"1":"a"}', true), $const));
var_dump(json_schema_validate(json_decode('{"1":"a"}'), $const));
var_dump(json_schema_validate(json_decode('{"1":"a"}'), json_decode('{"const":{"1":"a"}}')));
var_dump(json_schema_validate(json_decode('{"1":"b"}'), $const));
var_dump(json_schema_validate(json_decode('{"1":"a"}'), ['enum' => [['1' => 'a']]]));

$unique = ['uniqueItems' => true];
var_dump(json_schema_validate([['1' => 'a'], (object)['1' => 'a']], $unique));
var_dump(json_schema_validate([['a' => 1], (object)['a' => 1]], $unique));
var_dump(json_schema_validate([[1, 2], [2, 1]], $unique));
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(false)
bool(true)
