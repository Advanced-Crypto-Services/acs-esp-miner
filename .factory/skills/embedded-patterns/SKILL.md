---
name: Embedded Patterns
description: Common patterns for embedded C development
---
# Embedded C Patterns

## Register Access

### Memory-Mapped I/O
```c
#define PERIPH_BASE     ((uint32_t)0x40000000)
#define GPIO_BASE       (PERIPH_BASE + 0x20000)
#define GPIOA           ((GPIO_TypeDef *)GPIO_BASE)

#define REG32(addr)     (*(volatile uint32_t *)(addr))

static inline void reg_set_bits(volatile uint32_t *reg, uint32_t mask) {
    *reg |= mask;
}

static inline void reg_clear_bits(volatile uint32_t *reg, uint32_t mask) {
    *reg &= ~mask;
}
```

### Bit Manipulation
```c
#define BIT(n)              (1UL << (n))
#define SET_BIT(reg, bit)   ((reg) |= BIT(bit))
#define CLR_BIT(reg, bit)   ((reg) &= ~BIT(bit))
#define TOG_BIT(reg, bit)   ((reg) ^= BIT(bit))
#define GET_BIT(reg, bit)   (((reg) >> (bit)) & 1UL)

#define FIELD_MASK(width, pos)  (((1UL << (width)) - 1) << (pos))
#define GET_FIELD(reg, width, pos) \
    (((reg) & FIELD_MASK(width, pos)) >> (pos))
#define SET_FIELD(reg, width, pos, val) \
    ((reg) = ((reg) & ~FIELD_MASK(width, pos)) | (((val) << (pos)) & FIELD_MASK(width, pos)))
```

## Interrupt Handling

### ISR Best Practices
```c
volatile uint8_t g_uart_rx_flag = 0;
volatile uint8_t g_uart_rx_data;

void UART_IRQHandler(void) {
    if (UART->SR & UART_SR_RXNE) {
        g_uart_rx_data = UART->DR;
        g_uart_rx_flag = 1;
    }
}

void main_loop(void) {
    if (g_uart_rx_flag) {
        g_uart_rx_flag = 0;
        process_uart_byte(g_uart_rx_data);
    }
}
```

### Critical Sections
```c
#define ENTER_CRITICAL()    __disable_irq()
#define EXIT_CRITICAL()     __enable_irq()

static volatile uint32_t critical_nesting = 0;

void enter_critical(void) {
    __disable_irq();
    critical_nesting++;
}

void exit_critical(void) {
    if (--critical_nesting == 0) {
        __enable_irq();
    }
}
```

## State Machines

### Table-Driven FSM
```c
typedef enum { STATE_IDLE, STATE_RUNNING, STATE_ERROR, STATE_COUNT } state_t;
typedef enum { EVT_START, EVT_STOP, EVT_FAULT, EVT_COUNT } event_t;

typedef struct {
    state_t next_state;
    void (*action)(void);
} transition_t;

static const transition_t fsm_table[STATE_COUNT][EVT_COUNT] = {
    [STATE_IDLE] = {
        [EVT_START] = { STATE_RUNNING, action_start },
        [EVT_STOP]  = { STATE_IDLE,    NULL },
        [EVT_FAULT] = { STATE_ERROR,   action_fault },
    },
};

void fsm_dispatch(event_t evt) {
    const transition_t *t = &fsm_table[current_state][evt];
    if (t->action) t->action();
    current_state = t->next_state;
}
```

## Ring Buffer

```c
typedef struct {
    uint8_t *buf;
    uint16_t size;
    volatile uint16_t head;
    volatile uint16_t tail;
} ringbuf_t;

static inline bool ringbuf_put(ringbuf_t *rb, uint8_t data) {
    uint16_t next = (rb->head + 1) % rb->size;
    if (next == rb->tail) return false;
    rb->buf[rb->head] = data;
    rb->head = next;
    return true;
}

static inline bool ringbuf_get(ringbuf_t *rb, uint8_t *data) {
    if (rb->head == rb->tail) return false;
    *data = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return true;
}
```
