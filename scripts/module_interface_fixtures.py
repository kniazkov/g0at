# Copyright 2026 Ivan Kniazkov
"""Give independent semantic fixtures real external declaration instructions.

The appended entry block keeps all existing FUNC and branch instruction indices
unchanged. This is fixture construction, never production image mutation.
"""
import struct


def with_external_declarations(metadata, image):
    exports = metadata.get('externals', {})
    if not exports:
        return image
    h = list(struct.unpack_from('<8s14I', image))
    assert h[6] == 1 and metadata['main'] == 0
    instructions = image[h[9]:h[10]]
    descriptors = image[h[10]:h[11]]
    files = list(struct.unpack_from('<4I', image, h[11]))
    functions = image[h[12]:h[13]]
    locations = image[h[13]:h[14]]
    payload = bytearray(image[h[14]:])
    extra = bytearray()
    descriptor_count = h[5]
    for name, external in sorted(exports.items()):
        # Every block remains aligned for UTF-32 and declaration records.
        while len(payload) % 8:
            payload.append(0)
        text = name.encode('utf-32-le') + b'\0' * 4
        descriptors += struct.pack('<4I', len(payload), len(text), 1, len(text)//4 - 1)
        payload.extend(text)
        while len(payload) % 8:
            payload.append(0)
        declaration = struct.pack('<4I', 0x4445434c, descriptor_count, 0, 2)
        descriptors += struct.pack('<4I', len(payload), len(declaration), 3, 1)
        payload.extend(declaration)
        extra.extend(struct.pack('<BBHI', 7, 0, 0, 0))  # NIL
        extra.extend(struct.pack('<BBHI', 69 if external['mutable'] else 70,
                                 0, 0, descriptor_count + 1))
        descriptor_count += 2
    extra.extend(struct.pack('<BBHI', 3, 0, 0, files[2]))  # JUMP original entry
    entry = h[4]
    files[1] += len(extra)//8
    files[2] = entry
    metadata['files'][0]['entry'] = entry
    instructions += extra
    locations += struct.pack('<6I', entry, len(extra)//8, 0, 0, 0, 0)
    h[4] += len(extra)//8
    h[5] = descriptor_count
    h[8] += 1
    h[10] = h[9] + len(instructions)
    h[11] = h[10] + len(descriptors)
    h[12] = h[11] + 16
    h[13] = h[12] + len(functions)
    h[14] = h[13] + len(locations)
    h[3] = h[14] + len(payload)
    return (struct.pack('<8s14I', *h) + instructions + descriptors
            + struct.pack('<4I', *files) + functions + locations + payload)
