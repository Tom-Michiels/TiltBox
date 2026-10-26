"""Host verification of the exact C helpers used by the firmware. No network."""
import ctypes as C,subprocess,tempfile,random,io,struct,json,math,zlib
from pathlib import Path
import soundfile as sf
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tiltbox-logic-') as d:
    lib=Path(d)/'logic.so'
    subprocess.run(['cc','-shared','-fPIC','-O2','-I'+str(root/'main'),str(root/'tests/logic_bridge.c'),'-o',str(lib)],check=True)
    c=C.CDLL(str(lib));c.index_at.argtypes=[C.c_int,C.c_int]
    c.prepare.argtypes=[C.POINTER(C.c_int32),C.c_size_t,C.POINTER(C.c_int16)];c.prepare.restype=C.c_uint
    c.crc.argtypes=[C.c_char_p,C.c_size_t];c.crc.restype=C.c_uint
    assert sorted(c.index_at(x,y) for y in range(8) for x in range(8))==list(range(64))
    assert c.index_at(0,0)==0 and c.index_at(7,1)==8 and c.index_at(0,7)==63
    assert c.index_at(-1,2)==-1 and c.index_at(8,0)==-1
    rng=random.Random(91);frames=0
    for usb in [False,True]:
        for brightness in [0,20,180,255]:
            for data in [[0]*192,[255]*192]+[[rng.randrange(256) for _ in range(192)] for _ in range(100)]:
                inp=(C.c_uint8*192)(*data);out=(C.c_uint8*192)();c.limit(inp,out,C.c_uint(brightness),C.c_bool(usb))
                assert 64+15*sum(out)/255<=(200 if usb else 400)+1e-9
                assert all(v<=x*brightness//255 for x,v in zip(data,out));frames+=1
    # WAV header: canonical 44-byte PCM16 mono 16 kHz
    w=(C.c_uint8*44)()
    for n in [2,16000,1920000]:
        c.wav(w,n);b=bytes(w);assert b[:4]==b'RIFF' and b[8:16]==b'WAVEfmt ' and struct.unpack_from('<I',b,4)[0]==36+n
        assert struct.unpack_from('<HHIIHH',b,20)==(1,1,16000,32000,2,16) and struct.unpack_from('<I',b,40)[0]==n
        decoded,rate=sf.read(io.BytesIO(b+bytes(n)),dtype='int16');assert rate==16000 and len(decoded)==n//2 and not decoded.any()
    # Audio conditioning: 24-bit-in-32-bit words with DC offset and quiet speech -> normalised PCM16
    n=16000  # raw I2S word = 24-bit value << 8; DC offset 300000, speech-like tone at -26 dBFS (24-bit)
    raw=[(300000+int(400000*math.sin(2*math.pi*440*i/16000)))<<8 for i in range(n)]
    arr=(C.c_int32*n)(*raw);out=(C.c_int16*n)();gain=c.prepare(arr,n,out)
    pcm=list(out);peak=max(abs(v) for v in pcm[2000:]);mean=sum(pcm[2000:])/len(pcm[2000:])
    assert 20000<peak<=32767,peak            # normalised near -3 dBFS
    assert abs(mean)<300,mean                 # DC removed
    assert 256<gain<=32*256,gain
    quiet=[(int(20000*math.sin(2*math.pi*440*i/16000)))<<8 for i in range(n)]  # -52 dBFS: hits the 32x cap
    arr=(C.c_int32*n)(*quiet);gain=c.prepare(arr,n,out);assert gain==32*256,gain;assert 2000<max(abs(v) for v in list(out)[2000:])<3000
    loud=[(int(7000000*math.sin(2*math.pi*300*i/16000)))<<8 for i in range(n)]
    arr=(C.c_int32*n)(*loud);gain=c.prepare(arr,n,out);assert gain==256 and max(abs(v) for v in list(out)[2000:])<=32767
    silent=[0]*n;arr=(C.c_int32*n)(*silent);gain=c.prepare(arr,n,out);assert gain==256 and not any(out)
    for blob in [b'',b'a',b'TiltBox',bytes(range(256))*7]:
        assert c.crc(blob,len(blob))==zlib.crc32(blob),blob[:8]
    print(json.dumps({'result':'PASS','pixel_mapping':64,'current_limiter_frames':frames,'wav_headers':3,'audio_prepare':'normalise+hpf','crc32':4,'scope':'Host C logic only; not a hardware or paid-cloud test.'},indent=2))
