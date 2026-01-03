---
name: Hardware Debugging
description: Techniques for debugging embedded systems
---
# Hardware Debugging Skill

## Common Embedded Bugs

### 1. Interrupt Issues
**Symptoms:** System hangs, erratic behavior, missed events

**Checks:**
- ISR registered in vector table?
- Interrupt enabled in NVIC/peripheral?
- Correct priority configured?
- Flag cleared in ISR?
- Volatile on shared variables?

### 2. Memory Corruption
**Symptoms:** Random crashes, wrong values, HardFault

**Checks:**
- Stack overflow? (Check stack pointer, add canaries)
- Buffer overrun? (Array bounds)
- Unaligned access? (On strict-alignment platforms)
- DMA buffer in correct memory region?
- Write to flash/ROM?

### 3. Timing Issues
**Symptoms:** Works sometimes, fails under load

**Checks:**
- Race condition between ISR and main?
- Missing critical section?
- Peripheral needs delay after enable?
- Clock configuration correct?

### 4. Peripheral Configuration
**Symptoms:** Peripheral does not respond

**Checks:**
- Clock enabled for peripheral?
- GPIO alternate function configured?
- Correct pin mapping?
- Peripheral reset released?

## Debugging Techniques

### Printf Debugging (SWO/UART)
```c
int _write(int fd, char *ptr, int len) {
    for (int i = 0; i < len; i++) {
        uart_putc(ptr[i]);
    }
    return len;
}

#define TRACE(fmt, ...) printf("[%lu] " fmt "\n", HAL_GetTick(), ##__VA_ARGS__)
```

### GPIO Toggle Debugging
```c
#define DEBUG_PIN_HIGH()    GPIOA->BSRR = GPIO_PIN_0
#define DEBUG_PIN_LOW()     GPIOA->BRR = GPIO_PIN_0
#define DEBUG_PIN_TOGGLE()  GPIOA->ODR ^= GPIO_PIN_0

void critical_function(void) {
    DEBUG_PIN_HIGH();
    // ... code to measure ...
    DEBUG_PIN_LOW();
}
```

### Fault Handlers
```c
void HardFault_Handler(void) {
    __asm volatile (
        "tst lr, #4          \n"
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "b hard_fault_handler\n"
    );
}

void hard_fault_handler(uint32_t *stack) {
    volatile uint32_t pc  = stack[6];
    volatile uint32_t lr  = stack[5];
    printf("HardFault! PC=0x%08lX LR=0x%08lX\n", pc, lr);
    while(1);
}
```

### Stack Usage Analysis
```c
#define STACK_FILL_PATTERN 0xDEADBEEF

void fill_stack(void) {
    extern uint32_t _sstack, _estack;
    uint32_t *p = &_sstack;
    while (p < &_estack) *p++ = STACK_FILL_PATTERN;
}

uint32_t get_stack_usage(void) {
    extern uint32_t _sstack, _estack;
    uint32_t *p = &_sstack;
    while (*p == STACK_FILL_PATTERN && p < &_estack) p++;
    return (uint32_t)(&_estack - p) * sizeof(uint32_t);
}
```
