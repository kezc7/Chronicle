"""Which compilers made the objects inside a DLL. The linker writes a "Rich" header between the DOS stub and the PE
header: one entry per tool (a product id and that tool's build number) with a count of the objects it made. A DLL
linked by one Visual Studio can still carry objects, from a prebuilt package, compiled by a newer one, and those
need the newer VC++ runtime. The linker version alone doesn't show that; this does.

    python tools/rich-header.py <dll> [--below N]

Prints every entry. With --below N it fails if any entry's build number is N or higher.
Exit codes: 0 fine, 1 an entry at or above the limit, 2 the file can't be read or has no Rich header (not a pass).
"""
import struct
import sys


def entries(data):
    """[(product id, build number, object count)], or None when there is no Rich header."""
    if data[:2] != b'MZ' or len(data) < 0x40:
        return None
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    end = data.find(b'Rich', 0x40, pe)
    if end < 0 or end + 8 > len(data):
        return None
    key = struct.unpack_from('<I', data, end + 4)[0]
    words = []
    at = end - 4
    while at >= 0x40:
        word = struct.unpack_from('<I', data, at)[0] ^ key
        if word == 0x536E6144:  # "DanS", where the header starts
            break
        words.append(word)
        at -= 4
    else:
        return None
    words.reverse()
    words = words[3:]  # three words of padding follow the start marker
    if len(words) % 2:
        return None
    return [(words[i] >> 16, words[i] & 0xFFFF, words[i + 1]) for i in range(0, len(words), 2)]


def main(argv):
    args = argv[1:]
    limit = None
    if '--below' in args:
        at = args.index('--below')
        try:
            limit = int(args[at + 1])
        except (IndexError, ValueError):
            print(__doc__.strip())
            return 2
        del args[at:at + 2]
    if len(args) != 1 or args[0].startswith('--'):
        print(__doc__.strip())
        return 2
    try:
        with open(args[0], 'rb') as file:
            found = entries(file.read())
    except OSError as error:
        print(f'rich header: {error}')
        return 2
    if not found:
        print(f'rich header: none found in {args[0]}; nothing was checked')
        return 2
    over = [entry for entry in found if limit is not None and entry[1] >= limit]
    for product, build, count in found:
        mark = '  <- too new' if (product, build, count) in over else ''
        print(f'rich header: product {product:#06x} build {build:5d} x{count}{mark}')
    highest = max(build for _, build, _ in found)
    if over:
        print(f'rich header: {len(over)} of {len(found)} entries have a build number of {limit} or higher')
        return 1
    print(f'rich header: {len(found)} entries, highest build {highest}' + (f', all below {limit}' if limit is not None else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
