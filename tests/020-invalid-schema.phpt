--TEST--
Schemas that are not an array, stdClass or bool are rejected (issue #18)
--EXTENSIONS--
json_schema
--FILE--
<?php

use JsonSchema\Ext\Validator;

$cases = [
    'string' => ['string', 'x'],
    'int' => [1, 'x'],
    'null' => [null, 'x'],
    'non-stdClass object' => [new ArrayObject(['type' => 'string']), 'x'],
    'nested string' => [['properties' => ['a' => 'integer']], ['a' => 1]],
    'nested object in array schema' => [['properties' => ['a' => new ArrayObject()]], ['a' => 1]],
    'nested in stdClass' => [json_decode('{"items":"integer"}'), [1]],
    'items' => [['items' => 1], [1, 2]],
    'additionalItems' => [['items' => [[]], 'additionalItems' => 'x'], [1, 2]],
    'additionalProperties' => [['additionalProperties' => 'x'], ['a' => 1]],
    'dependencies' => [['dependencies' => ['a' => 'b']], ['a' => 1]],
    'items list' => [['items' => ['x']], [1]],
    'properties' => [['properties' => 'x'], ['a' => 1]],
    'patternProperties' => [['patternProperties' => 'x'], ['a' => 1]],
    'allOf' => [['allOf' => 'x'], 1],
    'anyOf' => [['anyOf' => 'x'], 1],
    'oneOf' => [['oneOf' => 'x'], 1],
    'inside not' => [['not' => 'x'], 1],
    'inside if' => [['if' => 'x', 'then' => false], 1],
    'then null' => [['if' => true, 'then' => null], 1],
    'else null' => [['if' => false, 'else' => null], 1],
    'inside anyOf' => [['anyOf' => [['type' => 'string'], 'x']], 1],
    'inside oneOf' => [['oneOf' => ['x', ['type' => 'integer']]], 1],
    'inside nested not' => [['properties' => ['a' => ['not' => ['not' => 'x']]]], ['a' => 1]],
];

$string = 'integer';
$cases['nested reference'] = [['properties' => ['a' => &$string]], ['a' => 1]];

foreach ($cases as $name => [$schema, $data]) {
    $result = json_schema_validate_with_errors($data, $schema);
    printf("%s: %s, %d error(s), %s\n",
        $name,
        $result['valid'] ? 'valid' : 'invalid',
        count($result['errors']),
        $result['errors'][0]['message'] ?? '-'
    );
}

$v = new Validator();
var_dump($v->validate('x', 'string'));
var_dump($v->isValid());
var_dump(json_schema_validate('x', 'string'));

echo "OK\n";
?>
--EXPECT--
string: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
int: invalid, 1 error(s), Invalid schema: expected object or boolean, got int
null: invalid, 1 error(s), Invalid schema: expected object or boolean, got null
non-stdClass object: invalid, 1 error(s), Invalid schema: expected object or boolean, got ArrayObject
nested string: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
nested object in array schema: invalid, 1 error(s), Invalid schema: expected object or boolean, got ArrayObject
nested in stdClass: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
items: invalid, 1 error(s), Invalid schema: expected object or boolean, got int
additionalItems: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
additionalProperties: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
dependencies: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
items list: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
properties: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
patternProperties: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
allOf: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
anyOf: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
oneOf: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
inside not: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
inside if: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
then null: invalid, 2 error(s), Invalid schema: expected object or boolean, got null
else null: invalid, 2 error(s), Invalid schema: expected object or boolean, got null
inside anyOf: invalid, 2 error(s), Value does not match any schema in anyOf
inside oneOf: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
inside nested not: invalid, 2 error(s), Value matches schema in 'not'
nested reference: invalid, 1 error(s), Invalid schema: expected object or boolean, got string
bool(false)
bool(false)
bool(false)
OK
