"""Keep GDB connected while Pintos boots each selected test in order."""

import json
from pathlib import Path
import re
import socket

import gdb


class PintosSequence:
    def __init__(self, session):
        self.session = session
        self.breakpoint = None
        if len(session["tests"]) < 2:
            return
        source = Path(gdb.lookup_global_symbol("power_off").symtab.fullname())
        shutdown_lines = [
            number for number, line in enumerate(source.read_text().splitlines(), 1)
            if re.search(r"\boutw\s*\(\s*0x604\s*,\s*0x2000\s*\)", line)
        ]
        if len(shutdown_lines) != 1:
            raise gdb.GdbError("Cannot locate the Pintos shutdown instruction for the test sequence.")
        self.breakpoint = gdb.Breakpoint(
            f"{source}:{shutdown_lines[0]}",
            type=gdb.BP_HARDWARE_BREAKPOINT, internal=True,
        )
        # Commands run after stopping, so switching targets is safe here.
        self.breakpoint.commands = "silent\npython _pintos_sequence.advance()\ncontinue"
        gdb.write(f"Pintos sequence: {' -> '.join(session['tests'])}\n")

    def advance(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(20)
            connection.connect(self.session["socket"])
            connection.sendall((json.dumps({
                "action": "next", "token": self.session["token"],
                "index": self.session["index"],
            }) + "\n").encode())
            response = b""
            while not response.endswith(b"\n") and len(response) < 4096:
                chunk = connection.recv(4096 - len(response))
                if not chunk:
                    raise gdb.GdbError("The Pintos sequence launcher stopped before the next test was ready.")
                response += chunk
        reply = json.loads(response)
        if "error" in reply:
            raise gdb.GdbError(reply["error"])
        self.session["index"] = reply["index"]
        if reply["last"]:
            self.breakpoint.enabled = False
        gdb.write(f"\nPintos next test: {reply['test']}\n")
        gdb.execute("monitor system_reset", to_string=True)
        gdb.execute("maintenance flush register-cache", to_string=True)


class PintosSequenceCommand(gdb.Command):
    def __init__(self):
        super().__init__("pintos-sequence", gdb.COMMAND_RUNNING)

    def invoke(self, argument, from_tty):
        global _pintos_sequence
        arguments = gdb.string_to_argv(argument)
        if len(arguments) != 1:
            raise gdb.GdbError("Usage: pintos-sequence <session.json>")
        session = json.loads(Path(arguments[0]).read_text())
        if _pintos_sequence and _pintos_sequence.breakpoint:
            _pintos_sequence.breakpoint.delete()
        _pintos_sequence = PintosSequence(session)


_pintos_sequence = None
PintosSequenceCommand()
