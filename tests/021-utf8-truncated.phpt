--TEST--
minLength/maxLength with truncated or invalid UTF-8 sequences (issue #15)
--EXTENSIONS--
json_schema
--FILE--
<?php

$cases = [
    'truncated 2-byte' => ["\xC3", 1],
    'truncated 3-byte' => ["\xE3\x81", 1],
    'truncated 4-byte' => ["\xF0\x9F\x98", 1],
    'lead byte at end' => ["a\xF0", 2],
    'valid then truncated' => ["\xE3\x81\x82\xE3", 2],
    'lone continuation bytes' => ["\x80\x80", 2],
    'invalid lead byte' => ["\xFF", 1],
];

foreach ($cases as $name => [$string, $length]) {
    $ok = json_schema_validate($string, ['minLength' => $length])
        && json_schema_validate($string, ['maxLength' => $length])
        && !json_schema_validate($string, ['minLength' => $length + 1])
        && !json_schema_validate($string, ['maxLength' => $length - 1]);
    printf("%s: %s\n", $name, $ok ? 'ok' : 'unexpected length');
}

echo "OK\n";
?>
--EXPECT--
truncated 2-byte: ok
truncated 3-byte: ok
truncated 4-byte: ok
lead byte at end: ok
valid then truncated: ok
lone continuation bytes: ok
invalid lead byte: ok
OK
