# moarvm-hello

A "hello world" for MoarVM, the bytecode virtual machine behind Rakudo/Raku. Prints
`Hello, MoarVM!` via `moarvm_say()`, the one output primitive this backend recognizes by name
(see `docs/platforms.md`'s moarvm section) - it walks a NUL-terminated byte string out of the
flat memory array and prints it with MoarVM's own `say` op.

MoarVM is a whole-program target, like `parrotvm`/`vex`: `make` runs a single `rcc
--target=moarvm` invocation straight to the final `build/main.moarvm` bytecode file - there is no
separate-compilation object step and no runtime library to link against.

`make test` runs `build/main.moarvm` under the real `moar` interpreter (`brew install moarvm`)
and checks it printed exactly `Hello, MoarVM!`.
