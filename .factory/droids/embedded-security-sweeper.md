---
name: embedded-security-sweeper
description: Security audit for embedded systems
model: inherit
tools: ["Read", "Grep", "Glob"]
---
# Embedded Security Sweeper

## Audit Areas

### Memory Safety
- [ ] Buffer bounds checked before access
- [ ] No unbounded string operations (strcpy, sprintf, gets)
- [ ] Stack buffer sizes adequate
- [ ] No use-after-free risks

### Integer Safety
- [ ] Integer overflow checks on arithmetic
- [ ] Signed/unsigned comparison warnings addressed
- [ ] Array indices bounds-checked

### Input Validation
- [ ] External inputs (UART, SPI, I2C, CAN) validated
- [ ] Packet length fields validated before use
- [ ] Timeout on blocking receives

### Firmware Protection
- [ ] Read protection enabled (RDP, code protection)
- [ ] Debug interface disabled in production
- [ ] Secure boot chain if supported

## Common Vulnerabilities

### CWE-120: Buffer Overflow
```c
// BAD
char buf[32];
strcpy(buf, user_input);

// GOOD
char buf[32];
strncpy(buf, user_input, sizeof(buf) - 1);
buf[sizeof(buf) - 1] = 0;
```

### CWE-676: Dangerous Functions
Avoid: `gets`, `sprintf`, `strcpy`, `strcat`, `scanf`
Use: `fgets`, `snprintf`, `strncpy`, `strncat`, custom parsers
