<?php

declare(strict_types=1);

namespace JsonSchema\Tests;

use PHPUnit\Framework\TestCase;

/**
 * Test ValidatorAdapter compatibility with jsonrainbow/json-schema API
 */
class ValidatorAdapterTest extends TestCase
{
    private $validatorClass;

    protected function setUp(): void
    {
        // Load the adapter
        require_once __DIR__ . '/../src/JsonSchema/Validator.php';

        // The adapter should now be available
        if (class_exists(\JsonSchema\ValidatorAdapter::class)) {
            $this->validatorClass = \JsonSchema\ValidatorAdapter::class;
        } else {
            $this->markTestSkipped('ValidatorAdapter not available');
        }
    }

    public function testValidatorExtendsBaseConstraint(): void
    {
        $validator = new $this->validatorClass();
        $this->assertInstanceOf(\JsonSchema\Constraints\BaseConstraint::class, $validator);
    }

    public function testValidateReturnsErrorMask(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": "John"}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $result = $validator->validate($data, $schema);

        $this->assertIsInt($result);
        $this->assertEquals(0, $result);
    }

    public function testIsValidReturnsTrueForValidData(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": "John"}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);

        $this->assertTrue($validator->isValid());
    }

    public function testIsValidReturnsFalseForInvalidData(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);

        $this->assertFalse($validator->isValid());
    }

    public function testGetErrorsReturnsArray(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);

        $errors = $validator->getErrors();
        $this->assertIsArray($errors);
        $this->assertNotEmpty($errors);
    }

    public function testGetErrorsIncludesPropertyAndMessage(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);

        $errors = $validator->getErrors();
        $this->assertArrayHasKey('property', $errors[0]);
        $this->assertArrayHasKey('message', $errors[0]);
    }

    /**
     * Test error format matches jsonrainbow/json-schema format
     * Error should have: property (string), pointer (string), message (string), constraint (string), context (int)
     */
    public function testErrorFormatMatchesJsonrainbow(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);
        $errors = $validator->getErrors();

        $this->assertNotEmpty($errors);
        $error = $errors[0];

        // All required keys must be present
        $this->assertArrayHasKey('property', $error, 'Error must have "property" key');
        $this->assertArrayHasKey('pointer', $error, 'Error must have "pointer" key');
        $this->assertArrayHasKey('message', $error, 'Error must have "message" key');
        $this->assertArrayHasKey('constraint', $error, 'Error must have "constraint" key');
        $this->assertArrayHasKey('context', $error, 'Error must have "context" key');
    }

    public function testErrorPropertyIsNonEmptyString(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);
        $errors = $validator->getErrors();
        $error = $errors[0];

        // property should be "name" (dot notation property path)
        $this->assertIsString($error['property']);
        $this->assertNotEmpty($error['property'], 'Property path should not be empty');
        $this->assertEquals('name', $error['property']);
    }

    public function testErrorConstraintIsString(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);
        $errors = $validator->getErrors();
        $error = $errors[0];

        // constraint should be a string like "type", not an integer
        $this->assertIsString($error['constraint'], 'Constraint should be a string, not integer');
        $this->assertEquals('type', $error['constraint']);
    }

    public function testErrorPointerIsJsonPointer(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);
        $errors = $validator->getErrors();
        $error = $errors[0];

        // pointer should be "/name" (JSON pointer format)
        $this->assertIsString($error['pointer']);
        $this->assertEquals('/name', $error['pointer']);
    }

    public function testErrorContextIsInteger(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);
        $errors = $validator->getErrors();
        $error = $errors[0];

        // context should be ERROR_DOCUMENT_VALIDATION = 1
        $this->assertIsInt($error['context']);
        $this->assertEquals(1, $error['context']);
    }

    public function testNestedPropertyErrorFormat(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"user": {"email": 123}}');
        $schema = json_decode('{"type": "object", "properties": {"user": {"type": "object", "properties": {"email": {"type": "string"}}}}}');

        $validator->validate($data, $schema);
        $errors = $validator->getErrors();

        $this->assertNotEmpty($errors);
        $error = $errors[0];

        // property should be "user.email" (dot notation)
        $this->assertEquals('user.email', $error['property']);
        // pointer should be "/user/email" (JSON pointer)
        $this->assertEquals('/user/email', $error['pointer']);
        // constraint should be "type"
        $this->assertEquals('type', $error['constraint']);
    }

    public function testRequiredErrorFormat(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{}');
        $schema = json_decode('{"type": "object", "required": ["name"]}');

        $validator->validate($data, $schema);
        $errors = $validator->getErrors();

        $this->assertNotEmpty($errors);
        $error = $errors[0];

        // constraint should be "required"
        $this->assertIsString($error['constraint']);
        $this->assertEquals('required', $error['constraint']);
    }

    public function testNumErrorsReturnsCount(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123, "age": "not-a-number"}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}, "age": {"type": "integer"}}}');

        $validator->validate($data, $schema);

        $numErrors = $validator->numErrors();
        $this->assertIsInt($numErrors);
        $this->assertGreaterThan(0, $numErrors);
    }

    public function testResetClearsErrors(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": 123}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}}');

        $validator->validate($data, $schema);
        $this->assertFalse($validator->isValid());

        $validator->reset();
        $this->assertTrue($validator->isValid());
        $this->assertEquals(0, $validator->numErrors());
    }

    public function testCheckMethodExists(): void
    {
        $validator = new $this->validatorClass();
        $this->assertTrue(method_exists($validator, 'check'));
    }

    public function testCoerceMethodExists(): void
    {
        $validator = new $this->validatorClass();
        $this->assertTrue(method_exists($validator, 'coerce'));
    }

    public function testErrorNoneConstant(): void
    {
        $this->assertEquals(0, $this->validatorClass::ERROR_NONE);
    }

    public function testErrorDocumentValidationConstant(): void
    {
        $this->assertIsInt($this->validatorClass::ERROR_DOCUMENT_VALIDATION);
        $this->assertNotEquals(0, $this->validatorClass::ERROR_DOCUMENT_VALIDATION);
    }

    public function testIsPeclAcceleratedReturnsTrue(): void
    {
        if (!extension_loaded('json_schema')) {
            $this->markTestSkipped('PECL json_schema extension not loaded');
        }

        $validator = new $this->validatorClass();
        $this->assertTrue($validator->isPeclAccelerated());
    }

    public function testValidateWithAssocSchema(): void
    {
        $validator = new $this->validatorClass();
        $schema = json_decode('{"properties":{"propertyOne":{"type":"array","items":[{"type":"string"}]}}}', true);
        $data = json_decode('{"propertyOne":[42]}', true);

        $validator->validate($data, $schema);

        $this->assertFalse($validator->isValid(), 'Validation succeeded, but should have failed.');
    }

    public function testValidateRequiredProperty(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{}');
        $schema = json_decode('{"type": "object", "required": ["name"]}');

        $validator->validate($data, $schema);

        $this->assertFalse($validator->isValid());
    }

    public function testValidateAdditionalProperties(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": "John", "extra": "value"}');
        $schema = json_decode('{"type": "object", "properties": {"name": {"type": "string"}}, "additionalProperties": false}');

        $validator->validate($data, $schema);

        $this->assertFalse($validator->isValid());
    }

    public function testValidateEnum(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('"red"');
        $schema = json_decode('{"enum": ["red", "green", "blue"]}');

        $validator->validate($data, $schema);
        $this->assertTrue($validator->isValid());

        $data2 = json_decode('"yellow"');
        $validator->validate($data2, $schema);
        $this->assertFalse($validator->isValid());
    }

    public function testValidateMinMax(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('5');
        $schema = json_decode('{"type": "integer", "minimum": 1, "maximum": 10}');

        $validator->validate($data, $schema);
        $this->assertTrue($validator->isValid());

        $data2 = json_decode('15');
        $validator->validate($data2, $schema);
        $this->assertFalse($validator->isValid());
    }

    public function testValidatePattern(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('"test@example.com"');
        $schema = json_decode('{"type": "string", "pattern": "^[a-z]+@[a-z]+\\\\.[a-z]+$"}');

        $validator->validate($data, $schema);
        $this->assertTrue($validator->isValid());
    }

    public function testValidateAllOf(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('{"name": "John", "age": 30}');
        $schema = json_decode('{"allOf": [{"properties": {"name": {"type": "string"}}}, {"properties": {"age": {"type": "integer"}}}]}');

        $validator->validate($data, $schema);
        $this->assertTrue($validator->isValid());
    }

    public function testValidateAnyOf(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('"hello"');
        $schema = json_decode('{"anyOf": [{"type": "string"}, {"type": "number"}]}');

        $validator->validate($data, $schema);
        $this->assertTrue($validator->isValid());
    }

    public function testValidateOneOf(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('5');
        $schema = json_decode('{"oneOf": [{"type": "integer"}, {"type": "string"}]}');

        $validator->validate($data, $schema);
        $this->assertTrue($validator->isValid());
    }

    public function testValidateNot(): void
    {
        $validator = new $this->validatorClass();
        $data = json_decode('"hello"');
        $schema = json_decode('{"not": {"type": "integer"}}');

        $validator->validate($data, $schema);
        $this->assertTrue($validator->isValid());
    }
}
