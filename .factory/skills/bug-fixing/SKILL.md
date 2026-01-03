---
name: Bug Fixing
description: Universal patterns for fixing bugs
---
# Bug Fixing Skill

## Common Bug Categories

### 1. Null/Undefined Handling
- Add defensive null checks
- Use optional chaining
- Provide sensible defaults

### 2. Race Conditions
- Proper async/await handling
- Locks for critical sections
- Retry with backoff

### 3. State Management
- Single source of truth
- Proper dependency tracking
- Cleanup on unmount

### 4. Input Validation
- Validate at boundaries
- Use schema validation
- Return descriptive errors

### 5. Error Handling
- Catch specific errors
- Log with context
- User-friendly messages

## Testing Pattern

```
describe("bugfix: [description]", () => {
  it("should handle [edge case]", () => {
    // Arrange: conditions that triggered bug
    // Act: run fixed code
    // Assert: verify correct behavior
  });
});
```
