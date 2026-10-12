--TEST--
enum, const and uniqueItems on deeply nested values fail instead of crashing (issue #14)
--DESCRIPTION--
Schema values nested beyond JSON_SCHEMA_MAX_DEPTH are rejected before comparison,
so only uniqueItems, which compares data with data, reaches the comparison limit.
--EXTENSIONS--
json_schema
--FILE--
<?php

function nest(int $depth): array
{
    $value = [1];
    for ($i = 1; $i < $depth; $i++) {
        $value = [$value];
    }
    return $value;
}

function show(string $name, $data, $schema): void
{
    $result = json_schema_validate_with_errors($data, $schema);
    printf("%s: %s, %s\n",
        $name,
        $result['valid'] ? 'valid' : 'invalid',
        $result['errors'][0]['message'] ?? '-'
    );
}

$deep = nest(10000);
show('enum', $deep, ['enum' => [nest(10000)]]);
show('const', $deep, ['const' => nest(10000)]);
show('uniqueItems', [$deep, nest(10000)], ['uniqueItems' => true]);

$cyclic = new stdClass();
$cyclic->self = $cyclic;
show('uniqueItems cyclic', [$cyclic, 1], ['uniqueItems' => true]);
$branching = new stdClass();
$branching->a = $branching;
$branching->b = $branching;
show('uniqueItems branching cyclic', [$branching, 1], ['uniqueItems' => true]);
show('uniqueItems under not', [$deep, null], ['not' => ['uniqueItems' => true]]);
show('uniqueItems in anyOf', [$deep, null], ['anyOf' => [['uniqueItems' => true], true]]);

// Nesting that json_decode accepts by default is still compared
$json = str_repeat('[', 511) . '1' . str_repeat(']', 511);
$limit = json_decode($json);
var_dump(is_array($limit));
show('const at json_decode depth', $limit, ['const' => json_decode($json)]);
show('uniqueItems at json_decode depth', [$limit, json_decode($json)], ['uniqueItems' => true]);

echo "OK\n";
?>
--EXPECT--
enum: invalid, Maximum validation depth exceeded (possible circular reference)
const: invalid, Maximum validation depth exceeded (possible circular reference)
uniqueItems: invalid, Maximum comparison depth exceeded
uniqueItems cyclic: invalid, Maximum comparison depth exceeded
uniqueItems branching cyclic: invalid, Maximum comparison depth exceeded
uniqueItems under not: invalid, Maximum comparison depth exceeded
uniqueItems in anyOf: invalid, Maximum comparison depth exceeded
bool(true)
const at json_decode depth: valid, -
uniqueItems at json_decode depth: invalid, Array contains duplicate items at indices 0 and 1
OK
