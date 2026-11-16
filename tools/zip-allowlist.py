"""Only Chronicle's own files ship. Reads every entry name of a finished zip and checks it against the allowlist
(tools/zip-allowlist.txt): an exact name, or a folder with the file types allowed under it, some lines for one UI
host only.

    python tools/zip-allowlist.py <zip> --host PrismaUI|Magelight [--list <allowlist>]

Names every entry that matches no line. An entry name with a backslash, a "..", an empty part or a leading "/" never
matches. Exit codes: 0 every entry is allowed, 1 at least one isn't, 2 the zip or the list can't be read, the list
has a bad line, or the arguments are wrong (not a pass).
"""
import os
import sys
import zipfile

HOSTS = ('PrismaUI', 'Magelight')
DEFAULT_LIST = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zip-allowlist.txt')


class BadList(Exception):
    pass


def read_list(path, host):
    """[(name or folder, None or a set of file types)] for the host."""
    rules = []
    with open(path, encoding='utf-8') as file:
        for number, raw in enumerate(file, 1):
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            only = None
            if line.startswith('['):
                end = line.find(']')
                only = line[1:end] if end > 0 else ''
                if only not in HOSTS:
                    raise BadList(f'line {number}: unknown host {line[:end + 1] if end > 0 else line}')
                line = line[end + 1:].strip()
            name, *types = line.split()
            if '\\' in name or name.startswith('/'):
                raise BadList(f'line {number}: {name}: use "/" and no leading "/"')
            if name.endswith('/'):
                if not types or not all(kind.startswith('.') and len(kind) > 1 for kind in types):
                    raise BadList(f'line {number}: folder {name} needs its file types (.html .js ...)')
                kinds = {kind.lower() for kind in types}
            elif types:
                raise BadList(f'line {number}: a single file takes no file types')
            else:
                kinds = None
            if only is None or only == host:
                rules.append((name, kinds))
    return rules


def allowed(entry, rules):
    parts = entry.split('/')
    if '\\' in entry or entry.startswith('/') or '' in parts or '..' in parts or '.' in parts:
        return False
    for name, kinds in rules:
        if kinds is None and entry == name:
            return True
        if kinds is not None and entry.startswith(name) and os.path.splitext(entry)[1].lower() in kinds:
            return True
    return False


def main(argv):
    args = argv[1:]
    options = {}
    for flag in ('--host', '--list'):
        if flag in args:
            at = args.index(flag)
            if at + 1 >= len(args):
                print(__doc__.strip())
                return 2
            options[flag] = args[at + 1]
            del args[at:at + 2]
    if len(args) != 1 or options.get('--host') not in HOSTS:
        print(__doc__.strip())
        return 2
    try:
        rules = read_list(options.get('--list', DEFAULT_LIST), options['--host'])
        with zipfile.ZipFile(args[0]) as archive:
            names = [info.orig_filename for info in archive.infolist()]
    except (OSError, zipfile.BadZipFile, BadList) as error:
        print(f'zip allowlist: {error}')
        return 2
    if not names:
        print(f'zip allowlist: {args[0]} is empty')
        return 2
    stray = [name for name in names if not allowed(name, rules)]
    for name in stray:
        print(f'zip allowlist: not on the allowlist: {name}')
    if stray:
        print(f'zip allowlist: {len(stray)} of {len(names)} entries are not Chronicle\'s own files')
        return 1
    print(f'zip allowlist: all {len(names)} entries are on the {options["--host"]} allowlist')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
