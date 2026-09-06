"""Build a compact resource index from the user's exact version-147 OBB.

This metadata is bundled in the VPK so the Vita does not scan 1780 RSGs at boot.
The runtime independently validates archive binding, checksums and bounds.
"""
from pathlib import Path
import hashlib,struct,zlib,json,argparse

ROOT=Path(__file__).resolve().parents[1]
OBB=ROOT/'.codex_tmp_vita_data/zip/com.ea.game.pvz2_row/main.147.com.ea.game.pvz2_row.obb'
OUT=ROOT/'vita/direct/extras/rsb452.idx'
HASH='aa76069dd3f5120cdf733de5e9b5946eab53d8bfdaccb4cb06bdd92bc0aa3abe'
MAGIC=0x32495a50

def main():
    global OBB
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--obb', type=Path, help='Path to the exact version-147 OBB')
    args=parser.parse_args()
    if args.obb:
        OBB=args.obb
    elif (ROOT/'game/game.obb').is_file():
        OBB=ROOT/'game/game.obb'
    if not OBB.is_file():
        parser.error('Supply your version-147 archive at game/game.obb or pass --obb PATH')
    with OBB.open('rb') as f:
        assert hashlib.file_digest(f,'sha256').hexdigest()==HASH,'Wrong OBB'
        f.seek(0);head=f.read(256);length=OBB.stat().st_size
        assert head[:8]==b'1bsr\x04\0\0\0'
        count,begin,each=struct.unpack_from('<3I',head,0x28)
        f.seek(begin);info=f.read(count*each)
        entries={}
        for i in range(count):
            offset=struct.unpack_from('<I',info,i*each+0x80)[0]
            if not offset:continue
            f.seek(offset);h=f.read(0x60)
            def u(pos):return struct.unpack_from('<I',h,pos)[0]
            flags,data,packed,unpacked=u(0x10),u(0x18),u(0x1c),u(0x20)
            n,start=u(0x48),u(0x4c)
            if not n or offset+start+n>length:continue
            f.seek(offset+start);table=f.read(n)
            stack=[];name=b'';pos=0;steps=0
            while pos+4<=n:
                steps+=1;assert steps<=n,'Cyclic resource names'
                ch=table[pos];branch=int.from_bytes(table[pos+1:pos+4],'little')
                if ch:
                    if branch:stack.append((name,branch*4))
                    name+=bytes([ch]);pos+=4
                else:
                    if pos+16>n:break
                    typ,within,size=struct.unpack_from('<3I',table,pos+4)
                    if name:
                        block=bool(flags&2) and typ==0
                        record=(offset+data+within,size,int(typ==1),offset+data if block else 0,
                                packed if block else 0,unpacked if block else 0,within if block else 0)
                        entries.setdefault(name,record)
                    pos+=16+(20 if typ==1 else 0)
                    if stack:name,pos=stack.pop()
                    elif not name:break
                    else:name=b''
    payload=bytearray()
    for name,record in sorted(entries.items()):
        assert 0<len(name)<=1024 and b'\0' not in name
        payload+=struct.pack('<Q7I',*record,len(name))+name
    header=struct.pack('<8I',MAGIC,1,length,zlib.crc32(head),len(entries),len(payload),zlib.crc32(payload),0)
    data=header+payload
    OUT.parent.mkdir(parents=True,exist_ok=True)
    if not OUT.exists() or OUT.read_bytes()!=data:OUT.write_bytes(data)
    print(json.dumps({'path':str(OUT),'entries':len(entries),'bytes':len(data),'rsgs':count}))
if __name__=='__main__':main()
