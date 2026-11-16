"""Whether a PDB belongs to a DLL: both must come from the same link. The linker writes a CodeView record into the
DLL's debug directory with the PDB's GUID and age; a debugger or crash logger loads a PDB only when its own GUID (PDB
info stream) and age (DBI stream) are the same. A PDB from another build of the same source never matches.

    python tools/pdb-match.py <dll> <pdb>

Prints the GUID and age of each. Exit codes: 0 they match, 1 they differ, 2 a file can't be read, the DLL has no
CodeView record or the PDB isn't an MSF 7.0 file (not a pass).
"""
import struct
import sys
import uuid

MSF_MAGIC = b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\x00\x00\x00'
IMAGE_DEBUG_TYPE_CODEVIEW = 2


class Unreadable(Exception):
    pass


def dll_codeview(data):
    """(GUID, age) from the DLL's CodeView (RSDS) debug record."""
    if data[:2] != b'MZ' or len(data) < 0x40:
        raise Unreadable('not a PE file')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\x00\x00':
        raise Unreadable('no PE signature')
    sections, optional_size = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    directories = optional + {0x10B: 96, 0x20B: 112}.get(magic, -optional - 1)
    if directories < optional:
        raise Unreadable('unknown optional header')
    debug_rva, debug_size = struct.unpack_from('<II', data, directories + 6 * 8)
    if not debug_rva:
        raise Unreadable('no debug directory')
    table = optional + optional_size

    def offset(rva):
        for index in range(sections):
            size, address, raw_size, raw = struct.unpack_from('<IIII', data, table + index * 40 + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise Unreadable(f'RVA {rva:#x} is in no section')

    start = offset(debug_rva)
    for entry in range(start, start + debug_size, 28):
        kind, size, _, pointer = struct.unpack_from('<IIII', data, entry + 12)
        if kind == IMAGE_DEBUG_TYPE_CODEVIEW and data[pointer:pointer + 4] == b'RSDS':
            return uuid.UUID(bytes_le=data[pointer + 4:pointer + 20]), struct.unpack_from('<I', data, pointer + 20)[0]
    raise Unreadable('no CodeView record')


def pdb_identity(data):
    """(GUID from the PDB info stream, age from the DBI stream)."""
    if data[:32] != MSF_MAGIC:
        raise Unreadable('not an MSF 7.0 PDB')
    block, _, blocks, directory_bytes, _, map_block = struct.unpack_from('<IIIIII', data, 32)
    if block not in (512, 1024, 2048, 4096) or len(data) < blocks * block:
        raise Unreadable('bad MSF header')

    def read_blocks(numbers, size):
        return b''.join(data[n * block:(n + 1) * block] for n in numbers)[:size]

    directory_blocks = -(-directory_bytes // block)
    directory = read_blocks(struct.unpack_from(f'<{directory_blocks}I', data, map_block * block), directory_bytes)
    streams = struct.unpack_from('<I', directory, 0)[0]
    sizes = [size if size != 0xFFFFFFFF else 0 for size in struct.unpack_from(f'<{streams}I', directory, 4)]
    at, lists = 4 + 4 * streams, []
    for size in sizes:
        count = -(-size // block)
        lists.append(struct.unpack_from(f'<{count}I', directory, at))
        at += 4 * count

    def stream(index):
        if index >= streams or sizes[index] == 0:
            raise Unreadable(f'stream {index} is missing')
        return read_blocks(lists[index], sizes[index])

    info = stream(1)
    guid = uuid.UUID(bytes_le=info[12:28])
    dbi = stream(3)
    if struct.unpack_from('<i', dbi, 0)[0] != -1:
        raise Unreadable('unknown DBI stream')
    return guid, struct.unpack_from('<I', dbi, 8)[0]


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip())
        return 2
    found = []
    for path, read in ((argv[1], dll_codeview), (argv[2], pdb_identity)):
        name = path.replace('\\', '/').rsplit('/', 1)[-1]
        try:
            with open(path, 'rb') as file:
                guid, age = read(file.read())
        except (OSError, Unreadable, struct.error) as error:
            print(f'pdb match: {name}: {error}')
            return 2
        print(f'pdb match: {name}: GUID {guid} age {age}')
        found.append((guid, age))
    if found[0] != found[1]:
        print('pdb match: the PDB is from another link than the DLL')
        return 1
    print('pdb match: the PDB belongs to the DLL')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
