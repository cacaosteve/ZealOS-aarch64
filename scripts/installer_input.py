"""QMP input for the graphical installer acceptance test (disposable guest)."""
import json
import socket
import time


def qmp(path, execute, arguments=None):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(10)
        sock.connect(str(path))
        with sock.makefile('rwb') as stream:
            assert 'QMP' in json.loads(stream.readline())
            for request in ({'execute': 'qmp_capabilities'},
                            {'execute': execute, 'arguments': arguments or {}}):
                stream.write((json.dumps(request)+'\n').encode())
                stream.flush()
                while True:
                    response = json.loads(stream.readline())
                    if 'event' not in response:
                        break
                if 'return' not in response:
                    raise RuntimeError(response)
            return response['return']


def key(path, code, down):
    qmp(path, 'input-send-event', {'events': [
        {'type': 'key', 'data': {'down': down,
         'key': {'type': 'qcode', 'data': code}}}]})
    time.sleep(0.25)


def text(path, value):
    punctuation = {' ': ('spc',), '/': ('slash',), '.': ('dot',),
                   ';': ('semicolon',), '"': ('shift', 'apostrophe'),
                   '(': ('shift', '9'), ')': ('shift', '0'),
                   '{': ('shift', 'bracket_left'), '}': ('shift', 'bracket_right'),
                   '\n': ('ret',), '\x1b': ('esc',)}
    for char in value:
        keys = punctuation.get(char, ('shift', char.lower()) if char.isupper() else (char,))
        for code in keys:
            key(path, code, True)
        for code in reversed(keys):
            key(path, code, False)


def stage(guest, wanted, timeout=120):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        out = guest.command('zcall InstallerAcceptanceStage', 'zc =>')
        expected = f'zc => 0x{wanted & ((1 << 64)-1):016x}'.encode()
        if expected in out:
            time.sleep(1)
            return
        if b'zc => 0xfffffffffffffffe' in out:
            raise RuntimeError('graphical installer reported failure')
        time.sleep(0.25)
    guest.command('zstatus', 'keyboard_focus=')
    raise RuntimeError(f'installer did not reach stage {wanted}')


def focus(guest):
    if not getattr(guest, "startup_splash_dismissed", False):
        # Initial shell readiness precedes Home/Once's asynchronous splash.
        # Observe its actual arrival before sending any terminal keystrokes.
        deadline = time.monotonic() + 180
        while True:
            status = guest.command('zstatus', 'keyboard_focus=')
            if b'keyboard_focus=Server' in status:
                text(guest.qmp_path, '\x1b')
                guest.startup_splash_dismissed = True
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('startup splash did not arrive before input test')
            time.sleep(1)
    # Home/Once can open its splash after the initial desktop marker. Dismiss
    # that Server popup before typing, otherwise it can consume the first key.
    for _ in range(10):
        status = guest.command('zstatus', 'keyboard_focus=')
        if b'keyboard_focus=Server' in status:
            text(guest.qmp_path, '\x1b')
        guest.command('zc WinFocus(bootstrap_gui_terminal_task);', 'zc: executed')
        time.sleep(0.5)
        status = guest.command('zstatus', 'keyboard_focus=')
        if b'keyboard_focus=Terminal' in status:
            time.sleep(0.5)
            return
    raise RuntimeError('installer terminal did not retain keyboard focus')


def editor(guest, reopen=False, stock_ed=False):
    focus(guest)
    command = 'Ed("/Home/Boot.TXT");' if stock_ed else 'edit /Home/Boot.TXT'
    for _ in range(3):
        text(guest.qmp_path, command)
        status = guest.command('zcall BootstrapGuiTerminalInputDiag', 'zc =>')
        value = int(status.rsplit(b'zc => 0x', 1)[1].split()[0], 16)
        if value & 255 == len(command):
            break
        # A busy host can drop injected keys. Clear the partial command
        # before retrying so the editor never saves under a truncated name.
        key(guest.qmp_path, 'down', True)
        key(guest.qmp_path, 'down', False)
    else:
        raise RuntimeError('could not deliver the complete editor command')
    text(guest.qmp_path, "\n")
    deadline = time.monotonic()+60
    while True:
        status = guest.command(
            'zc Print("editor_doc_active=%d\\n",bootstrap_gui_terminal_task->put_doc && '
            'bootstrap_gui_terminal_task->put_doc->desc==\'Edit\');', 'editor_doc_active=')
        if b'editor_doc_active=1' in status:
            break
        if time.monotonic() > deadline:
            raise RuntimeError('graphical editor did not open')
        time.sleep(0.25)
    if not reopen:
        text(guest.qmp_path, 'saved42')
    guest.command(
        'zc I64 len; U8 *body=DocSave(bootstrap_gui_terminal_task->put_doc,&len); '
        'Print("editor_doc_contains=%d\\n",body && !!StrMatch("saved42",body)); Free(body);',
        'editor_doc_contains=1')
    qmp(guest.qmp_path, 'screendump', {'filename': str(guest.log.name) + '-editor.ppm'})
    text(guest.qmp_path, '\x1b')
    time.sleep(0.5)
    guest.command('zstatus', 'gui_editor_active=0')
    guest.command(
        'zc I64 size; U8 *saved=FileRead("/Home/Boot.TXT",&size); '
        'Print("editor_saved=%d\\n", saved && !!StrMatch("saved42",saved)); Free(saved);',
        "editor_saved=1")
    print('PASS graphical editor ' + ('reopen after cold reboot' if reopen else 'type/save'), flush=True)
