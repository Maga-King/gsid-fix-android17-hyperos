#!/usr/bin/env python3
"""Build the loop fallback and patch a locally extracted GSID; run in WSL.

This is a narrowly scoped binary port, not an AOSP 17 source rebuild.
No Android version or firmware whitelist is applied by the module.
"""
import hashlib
import json
import os
import pathlib
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

ROOT = pathlib.Path(__file__).resolve().parent
BUILD = ROOT / 'build'
BUILD.mkdir(exist_ok=True)
SOURCE = ROOT / 'evidence/gsid.android17'
NDK_ROOT = pathlib.Path(os.environ.get('ANDROID_NDK_HOME', pathlib.Path.home() / 'android-ndk-r29'))
NDK = NDK_ROOT / 'toolchains/llvm/prebuilt/linux-x86_64/bin'
raw = bytearray(SOURCE.read_bytes())
with SOURCE.open('rb') as f:
    elf = ELFFile(f)
    assert elf['e_machine'] == 'EM_AARCH64'
    # Validate the instructions whose live register/stack layout entry.S uses.
    assert raw[0x6ac20:0x6ac30].hex() == 'f50303aaf60302aaf30301aaf40300aa'
    assert raw[0x6ad74:0x6ad7c].hex() == 'e833403968040036'
    assert raw[0x6ae14:0x6ae18].hex() == 'a9835e38'
    rels = elf.get_section_by_name('.rela.plt')
    symtab = elf.get_section(rels['sh_link'])
    got_symbols = {r['r_offset']: symtab.get_symbol(r['r_info_sym']).name for r in rels.iter_relocations()}
    plt = elf.get_section_by_name('.plt')
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
    ins = list(md.disasm(plt.data(), plt['sh_addr']))
    symbols = {s.name: s['st_value'] for s in symtab.iter_symbols() if s['st_value'] and s['st_info']['type'] == 'STT_FUNC'}
    for i, op in enumerate(ins[:-1]):
        if op.mnemonic != 'adrp' or op.op_str.split(',')[0] != 'x16': continue
        nxt = ins[i+1]
        if nxt.mnemonic != 'ldr' or nxt.op_str.split(',')[0] != 'x17': continue
        got = op.operands[1].imm + nxt.operands[1].mem.disp
        if got in got_symbols:
            symbols[got_symbols[got]] = ins[i-1].address if i and ins[i-1].mnemonic == 'bti' else op.address
    rename = {
        'stock_set_property': '_ZN7android4base11SetPropertyERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_',
        'stock_delete': '_ZdlPvm',
        'stock_log': '__android_log_print',
    }
    for name, orig in rename.items(): symbols[name] = symbols[orig]
    # Reuse the success destructor path so w21 retains our boolean result.
    symbols['stock_cleanup'] = 0x6ad2c
    phoff, phentsize = elf['e_phoff'], elf['e_phentsize']
    # The final PT_NOTE only duplicates the GNU property note, still described
    # by PT_GNU_PROPERTY. Reuse it, preserving every original segment address.
    segment = elf.get_segment(13)
    assert segment['p_type'] == 'PT_NOTE' and segment['p_vaddr'] == 0x3a0
    end = max(s['p_vaddr'] + s['p_memsz'] for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD')
    offset = (len(raw) + 0x3fff) & ~0x3fff
    address = (max(offset, end) + 0x3fff) & ~0x3fff

flags = ['--target=aarch64-linux-android37', '-O2', '-fPIC', '-fvisibility=hidden',
         '-fno-stack-protector', '-fno-builtin', '-ffreestanding', '-fno-unwind-tables',
         '-fno-asynchronous-unwind-tables', '-mbranch-protection=standard', '-Wall', '-Wextra', '-Werror']
for name in ['loop_fallback.c', 'entry.S']:
    subprocess.run([str(NDK/'clang'), *flags, '-c', str(ROOT/'native'/name), '-o', str(BUILD/(name+'.o'))], check=True)
script = BUILD/'payload.ld'
assignments = '\n'.join(f'{name} = 0x{value:x};' for name, value in symbols.items())
script.write_text(f'''ENTRY(loop_entry)
SECTIONS {{
  . = 0x{address:x};
  .text : {{ *(.text.entry) *(.text .text.*) }}
  .rodata : {{ *(.rodata .rodata.*) }}
  .data : {{ *(.data .data.*) *(.bss .bss.*) *(.got*) }}
  /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }}
}}
{assignments}
''')
payload_elf = BUILD/'payload.elf'
subprocess.run([str(NDK/'ld.lld'), '-T', str(script), '--no-undefined', '-o', str(payload_elf),
                str(BUILD/'entry.S.o'), str(BUILD/'loop_fallback.c.o')], check=True)
with payload_elf.open('rb') as f:
    payload = ELFFile(f)
    assert not payload.get_section_by_name('.data') or payload.get_section_by_name('.data')['sh_size'] == 0
    assert not any(s.name.startswith('.rel') and s['sh_size'] for s in payload.iter_sections())
payload_bin = BUILD/'payload.bin'
subprocess.run([str(NDK/'llvm-objcopy'), '-O', 'binary', str(payload_elf), str(payload_bin)], check=True)
code = payload_bin.read_bytes()
struct.pack_into('<IIQQQQQQ', raw, phoff + 13 * phentsize,
                 1, 5, offset, address, address, len(code), len(code), 0x4000)
delta = address - 0x6ad78
assert delta % 4 == 0 and -0x100000 <= delta < 0x100000
# w8 is the bool loaded from the stack: CBZ reaches farther than the old TBZ.
struct.pack_into('<I', raw, 0x6ad78, 0x34000008 | (((delta // 4) & 0x7ffff) << 5))
raw.extend(b'\0' * (offset-len(raw)))
raw.extend(code)
output = BUILD/'gsid.android17.loopfix'
output.write_bytes(raw)
report = {'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
          'output_sha256': hashlib.sha256(raw).hexdigest(), 'patch_offset': '0x6ad78',
          'payload_address': hex(address), 'payload_size': len(code),
          'note': 'No Android/firmware install whitelist. The build checks the target instructions.'}
(BUILD/'patch-report.json').write_text(json.dumps(report, indent=2)+'\n')
(ROOT/'patch-report.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
