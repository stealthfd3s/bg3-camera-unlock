"""LLDB breakpoint filter for BG3's ToggleInputMode input event.

The game calls InputManager::FireInputEvents with a Span<FireEventDesc const>.
At function entry on Apple Silicon:

    x2 -> { begin, end }

Each FireEventDesc is 0x38 bytes. Its first field points to the registered
input-event definition, whose first uint32_t is the event ID. ToggleInputMode
is registered as 0xC0 in this BG3 build.

This callback uses LLDB's SBProcess memory-reading API instead of breakpoint
expressions. That avoids invoking LLDB's expression JIT on every input frame.
"""

import lldb


SYMBOL = (
    "ls::InputManager::FireInputEvents("
    "ls::Span<ls::input::FireEventDesc const> const&, bool)"
)
BREAKPOINT_NAME = "bg3-toggle-input-probe"
TOGGLE_INPUT_MODE = 0xC0
FIRE_EVENT_DESC_SIZE = 0x38
MAX_DESCRIPTORS = 256


def _read_unsigned(process, address, size):
    error = lldb.SBError()
    value = process.ReadUnsignedFromMemory(address, size, error)
    if not error.Success():
        return None
    return value


def stop_on_toggle(frame, _bp_location, _internal_dict):
    """Return True only when the current span contains event ID 0xC0."""
    process = frame.GetThread().GetProcess()
    span_address = frame.FindRegister("x2").GetValueAsUnsigned()

    begin = _read_unsigned(process, span_address, 8)
    end = _read_unsigned(process, span_address + 8, 8)
    if begin is None or end is None or begin >= end:
        return False

    byte_count = end - begin
    if byte_count % FIRE_EVENT_DESC_SIZE != 0:
        return False

    descriptor_count = byte_count // FIRE_EVENT_DESC_SIZE
    if descriptor_count > MAX_DESCRIPTORS:
        return False

    for index in range(descriptor_count):
        descriptor = begin + index * FIRE_EVENT_DESC_SIZE
        definition = _read_unsigned(process, descriptor, 8)
        if not definition:
            continue

        event_id = _read_unsigned(process, definition, 4)
        if event_id != TOGGLE_INPUT_MODE:
            continue

        phase = _read_unsigned(process, descriptor + 0x13, 1)
        device_id = _read_unsigned(process, descriptor + 0x30, 2)
        print(
            "[toggle-probe] event=0x%X descriptor=0x%X "
            "definition=0x%X phase=%s device=%s"
            % (event_id, descriptor, definition, phase, device_id)
        )
        return True

    return False


def install_toggle_probe(debugger, _command, _exe_ctx, result, _internal_dict):
    target = debugger.GetSelectedTarget()
    if not target.IsValid():
        result.SetError("No selected LLDB target")
        return

    breakpoint = target.BreakpointCreateByName(SYMBOL)
    if not breakpoint.IsValid() or breakpoint.GetNumLocations() == 0:
        result.SetError("FireInputEvents symbol was not found")
        return

    breakpoint.AddName(BREAKPOINT_NAME)
    callback = __name__ + ".stop_on_toggle"
    # LLDB's Python binding is version-dependent here: current Apple LLDB
    # returns None even when it attaches the callback successfully.
    breakpoint.SetScriptCallbackFunction(callback)

    result.PutCString(
        "Installed breakpoint %d at %d location(s); filtering for event 0xC0"
        % (breakpoint.GetID(), breakpoint.GetNumLocations())
    )


def __lldb_init_module(debugger, _internal_dict):
    debugger.HandleCommand(
        "command script add --overwrite -f "
        + __name__
        + ".install_toggle_probe bg3-toggle-probe"
    )
    print("Loaded bg3-toggle-probe command")
