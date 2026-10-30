# Lua in the Ferrite Core Kernel

LuaJIT is embedded in the Ferrite Core kernel. The kernel creates a Lua state,
registers the available functions, executes the supplied script, and closes the
state when execution finishes. Lua code does not run as a separate user process:
it executes synchronously inside the kernel caller.

The build configuration disables LuaJIT's JIT compiler and FFI. The kernel opens
the Lua standard libraries `base`, `math`, and `string`. Other standard
libraries are not opened automatically.

## Running scripts

The shell command `rl` accepts Lua source directly as its argument:

```text
rl print("Hello from Ferrite Core")
```

To read and execute a script from the VFS, use `-file` followed by its path:

```text
rl -file /scripts/hello.lua
```

The file is loaded as text, and the kernel adds a terminating NUL byte. Kernel
code can also call `lua_run_luac(char*)` directly.

Each call to `lua_run_luac` creates a new Lua state. Ordinary Lua variables and
loaded modules do not persist across runs. The `glob_*` API provides storage for
small values that need to survive separate Lua runs.

## Kernel API

Unless noted otherwise, the following functions are registered globally when a
Lua state is created.

### Global functions

| Function | Behavior |
|---|---|
| `print(value)` | Prints the first value to the terminal and appends a newline. Prints an empty string for `nil`. |
| `printi(value)` | Prints the first value without automatically appending a newline. |
| `clear()` | Clears the screen. |
| `k_req(name)` | Loads the kernel module `shell` or `easyinput`, creating its global table. No return value is needed. |
| `glob_set(name, value)` | Stores a number, string, or boolean in shared storage. Passing `nil` removes the value with that name. |
| `glob_get(name)` | Returns the stored number, string, or boolean; returns `nil` if the name is not present. |
| `glob_free(name)` | Removes a value from shared storage. Does nothing if the name is not present. |

`glob_set` only accepts Lua values of type `number`, `string`, `boolean`, or
`nil`. Tables, functions, and other types are unsupported and raise a Lua error.
Names are case-sensitive.

Example:

```lua
glob_set("player_name", "Mira")
glob_set("high_score", 1250)
glob_set("sound_enabled", true)

print(glob_get("player_name"))

glob_free("high_score")
glob_set("sound_enabled", nil) -- nil also removes the value
```

### The `shell` module

Load it with `k_req("shell")`. This creates the global table `shell`.

| Function | Behavior |
|---|---|
| `shell.exec(command)` | Executes one shell command. It does not return an execution status. The command runs without elevated privileges. |

Example:

```lua
k_req("shell")
shell.exec("help")
```

The command runs in the regular shell context. Do not use `shell.exec` for
commands that require administrator privileges.

### The `easyinput` module

Load it with `k_req("easyinput")`. This creates the global table `easyinput`.

| Function | Behavior |
|---|---|
| `easyinput.input()` | Blocks the current Lua script until Enter is pressed, then returns the entered line. Backspace removes the last entered character. |
| `easyinput.finput()` | Placeholder: currently does not read input, pause execution, or return a value. |

The current implementation of `easyinput.input()` accepts printable ASCII
characters (codes 32–126). Input it cannot handle, such as non-ASCII characters,
is not added to the returned string. The line is limited by available Lua
memory; if memory allocation fails, the function raises a Lua error.

Example:

```lua
k_req("easyinput")
printi("Your name: ")
local name = easyinput.input()
print("Hello, " .. name)
```

While `easyinput.input()` waits for keyboard input, this Lua call does not
continue. This is synchronous blocking input, not background input.

### The `serial` table

Available without calling `k_req`.

| Function | Behavior |
|---|---|
| `serial.println(text)` | Sends a line to the serial port. Pass a string. |
| `serial.newline_m()` | Writes a manual newline to the serial port. |

Example:

```lua
serial.println("Kernel message")
serial.newline_m()
```

### The `kernel` table

Available without calling `k_req`.

| Function | Behavior |
|---|---|
| `kernel.version()` | Returns the Ferrite Core version (`FERR_VER`). |
| `kernel.qms_ver()` | Returns the QMS version (`CORE_VER`). |

Example:

```lua
print("Ferrite Core " .. kernel.version())
print("QMS " .. kernel.qms_ver())
```

## Errors and limitations

- Script loading or runtime errors are printed by the kernel as `Lua error`.
- API argument errors (for example, an unsupported value passed to `glob_set`)
  become Lua errors and stop the current script.
- `k_req` recognizes only the exact names `shell` and `easyinput`. For an
  unknown name, it returns the string `invalid kernel module name`; it does not
  raise a Lua exception.
- The kernel's built-in `print` prints only its first argument. To print multiple
  values, concatenate them in Lua, for example `print("score: " .. score)`.
- Unicode support depends on the keyboard driver and terminal; input through
  `easyinput.input()` is currently limited to ASCII.
- Do not treat Lua as a security sandbox: scripts execute inside the kernel and
  can call the kernel API exposed to them.
