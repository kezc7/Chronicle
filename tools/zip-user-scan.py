"""Nothing shipped may name the person who built it. A debug file (.pdb) holds the paths the compiler read, and a
tool installed under a user folder leaves that folder's path, and so the Windows user name, in it.

    python tools/zip-user-scan.py <zip>...

Reads every entry of each zip, binaries included, and fails if any holds
  - a Windows user-folder path: C:\\Users\\ (either slash, any case, as plain text or as UTF-16), or
  - the name of the user running the build, read from the USERNAME environment variable at that moment.
The name is never written anywhere and never printed: a hit is reported as the entry and what kind of hit it is.
Exit codes: 0 clean, 1 a hit, 2 a file couldn't be read, the arguments are wrong, or USERNAME isn't set (the scan
would be half blind, so that is a failure too).
"""
import os
import sys
import zipfile

SHORTEST_NAME = 3  # a shorter name would turn up by chance in any binary


def forms(text):
    """The bytes a piece of text can be stored as: plain and UTF-16, lower-cased."""
    lower = text.lower()
    return {lower.encode('utf-8'), lower.encode('utf-16-le')}


def needles(user):
    found = []
    for drive in 'abcdefghijklmnopqrstuvwxyz':
        for slash in ('\\', '/'):
            for form in forms(f'{drive}:{slash}users{slash}'):
                found.append(('a user-folder path', form))
    for form in forms(user):
        found.append(('the name of the user who built it', form))
    return found


def scan(path, user):
    """[(entry, kind, count)] and how many entries were read."""
    hits, count = [], 0
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            count += 1
            data = archive.read(info).lower()
            names = (info.orig_filename.lower().encode('utf-8'),)
            totals = {}
            for kind, needle in needles(user):
                total = data.count(needle) + sum(name.count(needle) for name in names)
                if total:
                    totals[kind] = totals.get(kind, 0) + total
            hits.extend((info.orig_filename, kind, total) for kind, total in totals.items())
    return hits, count


def main(argv):
    if len(argv) < 2 or any(arg.startswith('--') for arg in argv[1:]):
        print(__doc__.strip())
        return 2
    user = os.environ.get('USERNAME', '')
    if len(user) < SHORTEST_NAME:
        print('user scan: USERNAME is not set (or is too short to look for); nothing was scanned')
        return 2
    failed = False
    try:
        for path in argv[1:]:
            hits, count = scan(path, user)
            for entry, kind, total in hits:
                print(f'user scan: {entry} holds {kind} ({total} time(s))')
            failed = failed or bool(hits)
            print(f'user scan: {count - len({entry for entry, _, _ in hits})} of {count} entries clean in {path}')
    except (OSError, zipfile.BadZipFile) as error:
        print(f'user scan: {error}')
        return 2
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
