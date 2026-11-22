"""No folder on the build machine may be named in a released DLL or PDB (CHRON-40).

    python -I tools/pdb-paths.py <file.dll | file.pdb | archive.zip>...

The compiler records the source files and headers it read (the log macros' file names are in the DLL itself), and
the linker records every object, library and its own folder in the PDB. package.ps1 builds a release through a drive
made for the build (build.ps1 -Neutral): <drive>:\\src\\Chronicle is the repository, and vcpkg builds the
dependencies inside it, so every path the tools record reads <drive>:\\src\\Chronicle\\..., whatever the letter, and
the DLL names its PDB by file name alone.

This reads each DLL and PDB given (or inside each zip given), as plain text and as UTF-16, and lists the root of every
drive-letter path it finds (the drive and two folders). It fails on any root that is not one of these:
  - Visual Studio's and the Windows SDK's install folders (the toolchain's own libraries and headers, and the vcpkg
    that ships with Visual Studio), also in their short 8.3 form;
  - Microsoft's build folders inside its prebuilt libraries (D:\\a\\_work, d:\\os\\obj, d:\\os\\tools);
  - the build drive's layout, on whichever letter the build had: <drive>:\\src\\Chronicle.
A drive letter followed by a single path character is binary data, not a path, and isn't counted.
Modelled on DovahHub's tools/pdb-paths.py (DHUB-142, 1d9813b), with Chronicle's layout.
Exit codes: 0 no other path, 1 another path, 2 a file couldn't be read or none was given.
"""
import collections
import os
import re
import sys
import zipfile

ALLOWED = [
    r'c:\program files (x86)\microsoft visual studio',
    r'c:\program files (x86)\windows kits',
    r'c:\program files\microsoft visual studio',
    r'd:\a\_work',
    r'd:\os\obj',
    r'd:\os\tools',
]
# The same install folders as Windows' short names write them (C:\PROGRA~2 is "Program Files (x86)").
SHORT = re.compile(r'^c:\\progra~[12](\\micros~\d|\\window~\d)?$')
# The build drive's layout, after its letter.
LAYOUT = [r'\src\chronicle']
# A path: a drive letter not inside a word, a slash, then at least two path characters.
PATH = re.compile(rb'(?<![A-Za-z0-9])[A-Za-z]:[\\/][\x20-\x7e]{2,160}')
PATH16 = re.compile(rb'(?<![A-Za-z0-9]\x00)[A-Za-z]\x00:\x00[\\/]\x00(?:[\x20-\x7e]\x00){2,160}')
FIRST = re.compile(r'^[A-Za-z0-9_.$() -][A-Za-z0-9_.$() \\/-]')


def roots(data):
    found = collections.Counter()
    for pattern, encoding in ((PATH, 'ascii'), (PATH16, 'utf-16-le')):
        for match in pattern.finditer(data):
            text = match.group().decode(encoding, 'replace')
            if not FIRST.match(text[3:]):
                continue
            # On a command line a path ends at a quote, or at the '=' of an option like /PATHMAP:<from>=<to>.
            text = re.split(r'["=;]', text, maxsplit=1)[0]
            parts = [part for part in re.split(r'[\\/]+', text) if part]
            found['\\'.join(parts[:3])] += 1
    return found


def allowed(root):
    """A root is the drive and the first two folders, so the allowed ones match it exactly. The PDB keeps a long
    command line in pieces, so a piece can end inside an allowed root ("C:\\Program Files"): that is the start of the
    same one."""
    lower = root.lower()
    rest = lower[2:]
    if lower in ALLOWED or SHORT.match(lower) or any(rest == layout or rest.startswith(layout + '\\') for layout in LAYOUT):
        return True
    # A piece of an allowed root ("C:\\Program Files (x86)\\Microsoft" of Visual Studio's) is that root cut short.
    if any(a.startswith(lower) for a in ALLOWED):
        return True
    if len(lower.split('\\')) < 3:
        return any(layout.startswith(rest) for layout in LAYOUT)
    return False


def entries(path):
    """(name, bytes) for each DLL and PDB: the file itself, or those inside a zip."""
    if path.lower().endswith('.zip'):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if name.lower().endswith(('.dll', '.pdb')):
                    yield name, archive.read(name)
        return
    with open(path, 'rb') as stream:
        yield os.path.basename(path), stream.read()


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    bad = 0
    for path in paths:
        print('==', path)
        try:
            for name, data in entries(path):
                found = roots(data)
                print('  ', name, sum(found.values()), 'path strings')
                for root, count in sorted(found.items(), key=lambda kv: -kv[1]):
                    ok = allowed(root)
                    print('     ', count, root, '' if ok else '<- not allowed')
                    if not ok:
                        bad += count
        except (OSError, zipfile.BadZipFile) as error:
            print('could not read', path, error)
            return 2
    print('paths outside the allowed roots:', bad, '(PASS)' if bad == 0 else '(FAIL)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
