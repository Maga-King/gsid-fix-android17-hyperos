import sys
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from capstone.arm64 import ARM64_OP_IMM, ARM64_OP_REG

path = sys.argv[1]
with open(path, 'rb') as f:
    elf = ELFFile(f)
    text = elf.get_section_by_name('.text')
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    insns = list(md.disasm(text.data(), text['sh_addr']))
    if len(sys.argv) > 2:
        start = int(sys.argv[2], 16)
        end = int(sys.argv[3], 16)
        for i in insns:
            if start <= i.address < end:
                print(f'{i.address:08x}: {i.mnemonic:8s} {i.op_str}')
        sys.exit()
    targets = {}
    for section in elf.iter_sections():
        if section.name != '.rodata':
            continue
        data = section.data()
        for pattern in [b'Cannot map image:', b'Could not determine block device', b'Backing image ', b'Created loop device', b'Could not get image file list', b'ImageManager', b'gsiservice']:
            offset = 0
            while (offset := data.find(pattern, offset)) >= 0:
                addr = section['sh_addr'] + offset
                targets[addr] = data[offset:data.find(b'\0', offset)].decode(errors='replace')
                offset += len(pattern)
    registers = {}
    xrefs = []
    for n, i in enumerate(insns):
        ops = i.operands
        if i.mnemonic == 'adrp' and len(ops) == 2:
            registers[ops[0].reg] = ops[1].imm
        elif i.mnemonic == 'add' and len(ops) == 3 and ops[2].type == ARM64_OP_IMM:
            base = registers.get(ops[1].reg)
            if base is not None:
                addr = base + ops[2].imm
                if addr in targets:
                    xrefs.append((i.address, targets[addr]))
            registers.pop(ops[0].reg, None)
        elif i.mnemonic == 'adr' and len(ops) == 2 and ops[1].imm in targets:
            xrefs.append((i.address, targets[ops[1].imm]))
    for addr, name in xrefs:
        print(f'{addr:08x}: {name}')
    for section in elf.iter_sections():
        if section.name in ['.data.rel.ro', '.rela.dyn']:
            print(section.name, hex(section['sh_addr']), hex(section['sh_size']))
