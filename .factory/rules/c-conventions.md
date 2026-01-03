# C Conventions for Embedded

## File Organization
- `src/` - Application source
- `drivers/` - Peripheral drivers
- `hal/` - Hardware abstraction
- `lib/` - Reusable libraries
- `include/` - Public headers
- `test/` - Unit tests (host-compiled)

## Naming Conventions
- Functions: `module_action_object()` e.g., `uart_send_byte()`
- Types: `module_type_t` e.g., `uart_config_t`
- Defines: `MODULE_CONSTANT` e.g., `UART_BAUD_115200`
- Static globals: `s_` prefix e.g., `s_uart_state`

## Integer Types
- Use `<stdint.h>` fixed-width types
- Use `size_t` for sizes and array indices
- Use `bool` from `<stdbool.h>`

## Error Handling
- Return error codes, not just bool
- Check return values at call sites
