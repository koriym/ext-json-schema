--TEST--
stdClass schemas validate the same as array schemas (issue #18)
--EXTENSIONS--
json_schema
--FILE--
<?php

use JsonSchema\Ext\Validator;

// Reproduction from issue #18
$data = ['users' => [['id' => 'x']]];
$schemaJson = '{"type":"object","properties":{"users":{"type":"array","items":{"type":"object","properties":{"id":{"type":"integer"}}}}}}';

$v = new Validator();
$v->validate($data, json_decode($schemaJson));
var_dump($v->isValid());

$v = new Validator();
$v->validate($data, json_decode($schemaJson, true));
var_dump($v->isValid());

$v = new Validator();
$v->validate([1, 2], (object)['type' => 'string']);
var_dump($v->isValid());

// Every nested schema position, as stdClass and as array
$cases = [
    'properties' => ['{"properties":{"a":{"type":"integer"}}}', '{"a":"x"}', '{"a":1}'],
    'items single' => ['{"items":{"type":"integer"}}', '[1,"x"]', '[1,2]'],
    'items tuple' => ['{"items":[{"type":"integer"},{"type":"string"}]}', '[1,2]', '[1,"x"]'],
    'items with "0" keyword' => ['{"items":{"0":"x","type":"integer"}}', '[1,"x"]', '[1,2]'],
    'additionalItems' => ['{"items":[{}],"additionalItems":{"type":"integer"}}', '[1,"x"]', '[1,2]'],
    'additionalProperties' => ['{"properties":{"a":{}},"additionalProperties":{"type":"integer"}}', '{"a":1,"b":"x"}', '{"a":1,"b":2}'],
    'patternProperties' => ['{"patternProperties":{"^f":{"type":"integer"}}}', '{"foo":"x"}', '{"foo":1}'],
    'propertyNames' => ['{"propertyNames":{"maxLength":2}}', '{"abc":1}', '{"ab":1}'],
    'contains' => ['{"contains":{"type":"integer"}}', '["x"]', '["x",1]'],
    'definitions + $ref' => ['{"definitions":{"n":{"type":"integer"}},"properties":{"a":{"$ref":"#/definitions/n"}}}', '{"a":"x"}', '{"a":1}'],
    '$defs + $ref' => ['{"$defs":{"n":{"type":"integer"}},"items":{"$ref":"#/$defs/n"}}', '["x"]', '[1]'],
    '$ref root' => ['{"properties":{"a":{"type":"integer"},"child":{"$ref":"#"}}}', '{"child":{"a":"x"}}', '{"child":{"a":1}}'],
    'allOf' => ['{"allOf":[{"type":"integer"},{"minimum":2}]}', '1', '2'],
    'anyOf' => ['{"anyOf":[{"type":"integer"},{"type":"null"}]}', '"x"', 'null'],
    'oneOf' => ['{"oneOf":[{"type":"integer"},{"minimum":2}]}', '3', '1'],
    'not' => ['{"not":{"type":"integer"}}', '1', '"x"'],
    'if/then/else' => ['{"if":{"type":"integer"},"then":{"minimum":2},"else":{"type":"string"}}', '1', '"x"'],
    'dependencies schema' => ['{"dependencies":{"a":{"required":["b"]}}}', '{"a":1}', '{"a":1,"b":2}'],
    'dependencies list' => ['{"dependencies":{"a":["b"]}}', '{"a":1}', '{"a":1,"b":2}'],
    'enum with object' => ['{"enum":[{"a":1}]}', '{"a":2}', '{"a":1}'],
    'const with object' => ['{"const":{"a":[1]}}', '{"a":[2]}', '{"a":[1]}'],
    'empty object schema' => ['{"type":"object","properties":{"a":{}}}', '[]', '{"a":1}'],
];

foreach ($cases as $name => [$schemaJson, $invalidJson, $validJson]) {
    foreach (['invalid' => $invalidJson, 'valid' => $validJson] as $expected => $dataJson) {
        $data = json_decode($dataJson);
        $objectSchema = json_decode($schemaJson);
        $arraySchema = json_decode($schemaJson, true);

        $objectResult = json_schema_validate_with_errors($data, $objectSchema);
        $arrayResult = json_schema_validate_with_errors($data, $arraySchema);

        $v = new Validator();
        $oopObject = $v->validate($data, $objectSchema);
        $oopObjectErrors = $v->getErrors();
        $v = new Validator();
        $oopArray = $v->validate($data, $arraySchema);

        $same = $objectResult === $arrayResult
            && $oopObject === $arrayResult['valid']
            && $oopArray === $arrayResult['valid']
            && $oopObjectErrors === $arrayResult['errors']
            && json_schema_validate($data, $objectSchema) === $arrayResult['valid'];
        $got = $objectResult['valid'] ? 'valid' : 'invalid';
        printf("%s (%s): %s%s\n", $name, $expected, $got, $same ? '' : ' MISMATCH');
    }
}

// Numeric-string property names, with object data and with array data whose keys PHP stores as integers.
// Keys start at 1 because an array keyed from 0 is a list, which is a JSON array.
$numericCases = [
    'numeric properties' => ['{"properties":{"1":{"type":"integer"}}}', '{"1":"x"}', '{"1":1}'],
    'numeric required' => ['{"required":["1"]}', '{"2":1}', '{"1":1}'],
    'numeric patternProperties' => ['{"patternProperties":{"1":{"type":"integer"}}}', '{"10":"x"}', '{"10":1}'],
    'numeric additionalProperties' => ['{"properties":{"1":{}},"additionalProperties":false}', '{"1":1,"a":2}', '{"1":1}'],
    'numeric dependencies' => ['{"dependencies":{"1":["2"]}}', '{"1":1}', '{"1":1,"2":2}'],
    'numeric propertyNames' => ['{"propertyNames":{"pattern":"^[a-z]+$"}}', '{"1":1}', '{"a":1}'],
];

foreach ($numericCases as $name => [$schemaJson, $invalidJson, $validJson]) {
    foreach (['invalid' => $invalidJson, 'valid' => $validJson] as $expected => $dataJson) {
        $results = [];
        foreach ([json_decode($dataJson), json_decode($dataJson, true)] as $data) {
            $results[] = json_schema_validate($data, json_decode($schemaJson));
            $results[] = json_schema_validate($data, json_decode($schemaJson, true));
        }
        $same = count(array_unique($results)) === 1;
        printf("%s (%s): %s%s\n", $name, $expected, $results[0] ? 'valid' : 'invalid', $same ? '' : ' MISMATCH');
    }
}

// propertyNames does not apply to lists
var_dump(json_schema_validate([1, 2], ['propertyNames' => ['pattern' => '^[a-z]+$']]));

// Coercing a numeric property name to an integer
var_dump(json_schema_validate(['12345' => 1], ['propertyNames' => ['type' => 'integer', 'minimum' => 99999]], JsonSchema\Constraint::CHECK_MODE_COERCE_TYPES));

// External schema returned as stdClass by the resolver
$v = new Validator();
$v->setRefResolver(fn(string $uri) => json_decode('{"definitions":{"n":{"type":"integer"}}}'));
var_dump($v->validate('x', (object)['$ref' => 'types.json#/definitions/n']));
var_dump($v->validate(1, (object)['$ref' => 'types.json#/definitions/n']));
$v->setRefResolver(fn(string $uri) => json_decode('{"properties":{"0":{"type":"integer"}}}'));
var_dump($v->validate((object)['0' => 'x'], (object)['$ref' => 'types.json']));

// Schemas reached through PHP references
$integer = ['type' => 'integer'];
$byRef = ['properties' => ['a' => &$integer]];
var_dump(json_schema_validate(['a' => 'x'], $byRef));
var_dump(json_schema_validate(['a' => 1], $byRef));

// Schema objects are not modified
$schema = json_decode('{"properties":{"a":{"type":"integer"}}}');
$before = serialize($schema);
json_schema_validate(['a' => 1], $schema);
var_dump(serialize($schema) === $before);

echo "OK\n";
?>
--EXPECT--
bool(false)
bool(false)
bool(false)
properties (invalid): invalid
properties (valid): valid
items single (invalid): invalid
items single (valid): valid
items tuple (invalid): invalid
items tuple (valid): valid
items with "0" keyword (invalid): invalid
items with "0" keyword (valid): valid
additionalItems (invalid): invalid
additionalItems (valid): valid
additionalProperties (invalid): invalid
additionalProperties (valid): valid
patternProperties (invalid): invalid
patternProperties (valid): valid
propertyNames (invalid): invalid
propertyNames (valid): valid
contains (invalid): invalid
contains (valid): valid
definitions + $ref (invalid): invalid
definitions + $ref (valid): valid
$defs + $ref (invalid): invalid
$defs + $ref (valid): valid
$ref root (invalid): invalid
$ref root (valid): valid
allOf (invalid): invalid
allOf (valid): valid
anyOf (invalid): invalid
anyOf (valid): valid
oneOf (invalid): invalid
oneOf (valid): valid
not (invalid): invalid
not (valid): valid
if/then/else (invalid): invalid
if/then/else (valid): valid
dependencies schema (invalid): invalid
dependencies schema (valid): valid
dependencies list (invalid): invalid
dependencies list (valid): valid
enum with object (invalid): invalid
enum with object (valid): valid
const with object (invalid): invalid
const with object (valid): valid
empty object schema (invalid): invalid
empty object schema (valid): valid
numeric properties (invalid): invalid
numeric properties (valid): valid
numeric required (invalid): invalid
numeric required (valid): valid
numeric patternProperties (invalid): invalid
numeric patternProperties (valid): valid
numeric additionalProperties (invalid): invalid
numeric additionalProperties (valid): valid
numeric dependencies (invalid): invalid
numeric dependencies (valid): valid
numeric propertyNames (invalid): invalid
numeric propertyNames (valid): valid
bool(true)
bool(false)
bool(false)
bool(true)
bool(false)
bool(false)
bool(true)
bool(true)
OK
