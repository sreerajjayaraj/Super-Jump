// Read-only TLS validator. A nonempty CRT TLS template must remain intact.
const fs=require('fs');const p=process.argv[2];if(!p)throw Error('ASI path required');const b=fs.readFileSync(p);
const pe=b.readUInt32LE(0x3c),optional=pe+24,base=Number(b.readBigUInt64LE(optional+24));
if(b.readUInt16LE(optional)!==0x20b)throw Error('PE32+ required');
const sections=pe+24+b.readUInt16LE(pe+20),count=b.readUInt16LE(pe+6);
function off(r,n){for(let i=0;i<count;i++){const s=sections+40*i,a=b.readUInt32LE(s+12),z=b.readUInt32LE(s+16);if(r>=a&&r+n<=a+z){const x=b.readUInt32LE(s+20)+r-a;if(x+n>b.length)throw Error('raw bounds');return x;}}throw Error('unmapped RVA');}
const dir=optional+112+9*8,rva=b.readUInt32LE(dir),size=b.readUInt32LE(dir+4);if(!rva||size!==40)throw Error('intact TLS64 directory required');
const t=off(rva,40),start=Number(b.readBigUInt64LE(t)),end=Number(b.readBigUInt64LE(t+8)),index=Number(b.readBigUInt64LE(t+16)),callbacks=Number(b.readBigUInt64LE(t+24));
if(start<base||end<=start||end-start>4096||index<base||callbacks<base)throw Error('invalid real TLS');
const raw=off(start-base,end-start);let writableIndex=false;for(let i=0;i<count;i++){const s=sections+40*i,a=b.readUInt32LE(s+12),z=Math.max(b.readUInt32LE(s+8),b.readUInt32LE(s+16));if(index-base>=a&&index-base+4<=a+z&&(b.readUInt32LE(s+36)&0x80000000))writableIndex=true;}if(!writableIndex)throw Error('TLS index must be loader-writable');const cb=off(callbacks-base,8);if(b.readBigUInt64LE(cb)!==0n)throw Error('unexpected TLS callback');
console.log('retained real TLS template '+(end-start)+' bytes '+b.subarray(raw,raw+end-start).toString('hex')+'; callbacks empty; artifact unchanged');
