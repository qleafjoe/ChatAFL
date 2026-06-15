import hashlib
import clr
clr.AddReferenceByPartialName('Peach.Core')
clr.AddReferenceByPartialName('Peach.Pro')
#clr.AddReferenceByPartialName('BouncyCastle.Crypto')
import code


import Peach.Core
import System
from System import Byte, Array
from Peach.Core.IO import BitwiseStream
times=0
def convertBlobToStr(b):
    print("abcasdgdfhfhcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")
    stream = clr.Convert(b, BitwiseStream)
    byteArray = Array.CreateInstance(Byte, stream.Length)
    stream.Read(byteArray, 0, stream.Length)
    byte = bytes(byteArray)
    data = str(byte)
    return data


def convertStrToBlob(str):
    bytes = System.Array[System.Byte](buffer(str))
    BitStream=Peach.Core.IO.BitStream(bytes)
    val = Peach.Core.Variant(BitStream)
    return val

def convertNumToStr(num):
    #print num
    num_b = hex(num)
    #0 x and L which are 4 bits respectively are discarded 
    lb = len(num_b)
    if num_b[lb-1:lb] == "L" :
        num_c = num_b[2 : -1]
    else:
        num_c = num_b[2:]
    lc = len(num_c)
    if lc%2 != 0 :
        num_c = "0" + num_c
    num_d = binascii.unhexlify(num_c)
    return num_d  

def convertStrToNum(s):
  hexstring = binascii.hexlify(s)
  return int(hexstring, 16)


def fast_power(base,power,mod):
    ans=1
    while(power>0):
        if(power&1):
            power=power-1
            ans=(ans*base)%mod
        else:
            power=power>>1
            base=(base*base)%mod
    return ans


def byte_ord(c):
        # In case we're handed a string instead of an int.
        if not isinstance(c, int):
            c = ord(c)
        return c
def deflate_long(n, add_sign_padding=True):
    """turns a long-int into a normalized byte string
    (adapted from Crypto.Util.number)
    convert long to mpint type for p e g f k """
    # after much testing, this algorithm was deemed to be the fastest
    s = bytes()
    n = long(n)
    while (n != 0) and (n != -1):
        s = struct.pack(">I", n & 0xffffffff) + s
        n >>= 32
    # strip off leading zeros, FFs
    for i in enumerate(s):
        if (n == 0) and (i[1] != b'\x00'):
            break
        if (n == -1) and (i[1] != b'\xff'):
            break
    else:
        # degenerate case, n was either 0 or -1
        i = (0,)
        if n == 0:
            s = b'\x00'
        else:
            s = b'\xff'
    s = s[i[0] :]
    if add_sign_padding:
        if (n == 0) and (byte_ord(s[0]) >= 0x80):
            s = b'\x00' + s
        if (n == -1) and (byte_ord(s[0]) < 0x80):
            s = b'\xff' + s
    return s



def fast_power(base,power,mod):
    ans=1
    while(power>0):
        if(power&1):
            power=power-1
            ans=(ans*base)%mod
        else:
            power=power>>1
            base=(base*base)%mod
    return ans


#ssh-rsa signature verification algo
def os2ip(X): #first step
    val = 0 
    for i in range(len(X)):
        byt = X[len(X)-1-i]
        byt = ord(byt)
        val += byt << (8 *i)
    #print(x)
    return val

def i2osp(x, xLen): #third step
    #second step is using host key (e,n) to fast_power(long_from_step1,e,n), 
    # the result is input x, xLen=signature length-15
    if x >= (1 << (8 * xLen)):
        print "fail!!!!"
    ret = [0] * xLen
    val_ = x
    for idx in reversed(range(0, xLen)):
        ret[idx] = val_ & 0xff
        val_ = val_ >> 8
    #print ret
    ret = struct.pack("=" + "B" * xLen, *ret)
    return ret 

def calculate_key(sha_type,k,H,b,id,mlen):
    hm=hashlib.new(sha_type,k+H+b+id).digest()
    while(len(hm)<mlen):
        hm=hm+hashlib.new(sha_type,k+H+hm).digest()
    return hm[:mlen]





import binascii
import random
import struct
import hashlib
supports_algo={
    'kex':['diffie-hellman-group14-sha1','diffie-hellman-group14-sha256','diffie-hellman-group-exchange-sha1','diffie-hellman-group-exchange-sha256'],
    'en_algo':['aes128-cbc','aes256-cbc','aes128-ctr','aes256-ctr'],
    'hmac':['hmac-sha1-96','hmac-sha1','hmac-sha2-256','hmac-md5','hmac-md5-96'],
}

def parser_kex(c,s):
    kex=convertBlobToStr(s.dataModel.find('kex-algo-str').InternalValue).split(",")
    hk=convertBlobToStr(s.dataModel.find('server-host-key-str').InternalValue).split(",")
    en_c2s=convertBlobToStr(s.dataModel.find('encrypt-algo-c2s-str').InternalValue).split(",")
    #en_s2c=str(s.dataModel.find('encrypt-algo-s2c-str').InternalValue).split(",")
    mac_c2s=convertBlobToStr(s.dataModel.find('mac-algo-c2s-str').InternalValue).split(",")
    #mac_s2c=str(s.dataModel.find('mac-algo-s2c-str').InternalValue).split(",")
    c.iterationStateStore['fault']=False

    for i in supports_algo['kex']:
        if i in kex:
            c.iterationStateStore['kex']=i.split('-')
            if i.split("-")[-2]=='group14':
                c.iterationStateStore['group_flag']=True
            else:
                c.iterationStateStore['group_flag']=False
            break
    if 'ssh-rsa' not in hk:
        c.iterationStateStore['fault']=True
    for i in supports_algo['en_algo']:
        if i in en_c2s:
            c.iterationStateStore['en_algo']=i.split('-')
            break
        
    for i in supports_algo['hmac']:
        if i in mac_c2s:
            c.iterationStateStore['hmac']=i.split('-')
            break

    try:
        if  not (c.iterationStateStore['kex'] and c.iterationStateStore['en_algo'] and c.iterationStateStore['hmac']):
            pass
    except:
        c.iterationStateStore['fault']=True


def ex_init(c,s):
    p=convertBlobToStr(c.iterationStateStore['p'])
    g=convertBlobToStr(c.iterationStateStore['g'])
    
    p=long(binascii.hexlify(p),16)
    g=long(binascii.hexlify(g),16)
    x=random.randint(2,(p-1)/2)
    c.iterationStateStore['x']=x
    e=fast_power(g,x,p)
    #print len(hex(e)),p_len
    #zero_pad_p=p_len-(len(hex(e))-2)/2
    #e=b'\x00'*zero_pad_p+convertNumToStr(e)
    c.iterationStateStore['p']=deflate_long(p)
    c.iterationStateStore['g']=deflate_long(g)
    c.iterationStateStore['e']=deflate_long(e)
    
    s.dataModel.find('dh-client-e').DefaultValue=convertStrToBlob(c.iterationStateStore['e'])

group_p=0xFFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF
group_g=2
def ex_init_group(c,s):
    p=group_p
    g=group_g
    x=random.randint(2,(p-1)/2)
    c.iterationStateStore['x']=x
    e=fast_power(g,x,p)
    #print len(hex(e)),p_len
    #zero_pad_p=p_len-(len(hex(e))-2)/2
    #e=b'\x00'*zero_pad_p+convertNumToStr(e)
    c.iterationStateStore['p']=deflate_long(p)
    c.iterationStateStore['g']=deflate_long(g)
    c.iterationStateStore['e']=deflate_long(e)
    
    s.dataModel.find('dh-client-e').DefaultValue=convertStrToBlob(c.iterationStateStore['e'])

def gen_K(c,s):
    x=c.iterationStateStore['x']
    p=c.iterationStateStore['p']
    f=convertBlobToStr(c.iterationStateStore['f'])
    p=long(binascii.hexlify(p),16)
    f=long(binascii.hexlify(f),16)
    k=fast_power(f,x,p)
    c.iterationStateStore['f']=deflate_long(f)
    c.iterationStateStore['k']=deflate_long(k)
    #print(hex(x))
    #print hex((k))

    #start calculate hash H
    #key gex H=hash(v_c+v_s+i_c+i_s+k_s+min_n_max+p+g+e+f+k)
    v_c=convertBlobToStr(c.iterationStateStore['v_c'])
    v_c=struct.pack('!I',len(v_c))+v_c
    v_s=convertBlobToStr(c.iterationStateStore['v_s'])
    if v_s[-1]==b'\x0d' or v_s[-1]==b'\x0a':
        v_s=v_s[:-1]#judge if the last byte is 0d or 0a, if true removed
    v_s=struct.pack('!I',len(v_s))+v_s
    i_c=convertBlobToStr(c.iterationStateStore['i_c'])
    i_c=struct.pack('!I',len(i_c))+i_c
    i_s=convertBlobToStr(c.iterationStateStore['i_s'])
    i_s=struct.pack('!I',len(i_s))+i_s
    k_s=convertBlobToStr(c.iterationStateStore['k_s'])
    if not c.iterationStateStore['group_flag']:
        min=struct.pack('!I',int(c.iterationStateStore['min']))
        num=struct.pack('!I',int(c.iterationStateStore['num']))
        max=struct.pack('!I',int(c.iterationStateStore['max']))
        
    p=c.iterationStateStore['p']
    p=struct.pack('!I',len(p))+p
    g=c.iterationStateStore['g']
    g=struct.pack('!I',len(g))+g
    e=c.iterationStateStore['e']
    e=struct.pack('!I',len(e))+e
    f=c.iterationStateStore['f']
    f=struct.pack('!I',len(f))+f
    k=c.iterationStateStore['k']
    k=struct.pack('!I',len(k))+k

    sha=c.iterationStateStore['kex'][-1]
    en_algo=c.iterationStateStore['en_algo'][0][3:]

    mac_algo=c.iterationStateStore['hmac']
    if mac_algo[1]=='sha1':
        mac_algo=str('20')
    elif mac_algo[1]=='sha2':
        mac_algo=str('32')
    if not c.iterationStateStore['group_flag']:
        H=hashlib.new(sha,v_c+v_s+i_c+i_s+k_s+min+num+max+p+g+e+f+k).digest()
    else:#group14
        H=hashlib.new(sha,v_c+v_s+i_c+i_s+k_s+e+f+k).digest()
    #gex algo is sha1, optional should have sha256
    #print sha

    #verify the signature H
    sig=convertBlobToStr(c.iterationStateStore['H-sign'])[15:]#only for ssh-rsa
    rsa_e=convertBlobToStr(c.iterationStateStore['rsa_e'])
    rsa_e=long(binascii.hexlify(rsa_e),16)
    rsa_n=convertBlobToStr(c.iterationStateStore['rsa_n'])
    rsa_n=long(binascii.hexlify(rsa_n),16)
    osh=os2ip(sig)
    m=fast_power(osh,rsa_e,rsa_n)
    if hashlib.new('sha1',H).digest()==i2osp(m,len(sig))[-20:]:
        print "verification done!!!!"
        c.iterationStateStore['H']=H
        session_id=H
        #sha algo is the same as the gex sha(should have 256)
        c.iterationStateStore['iv_c']=calculate_key(sha,k,H,'A',session_id,16)#most iv size is block size, 16
        c.iterationStateStore['iv_s']=calculate_key(sha,k,H,'B',session_id,16)
        c.iterationStateStore['key_c']=calculate_key(sha,k,H,'C',session_id,int(en_algo,10)/8)# key size is same as bit/8
        c.iterationStateStore['key_s']=calculate_key(sha,k,H,'D',session_id,int(en_algo,10)/8)
        c.iterationStateStore['mac_c']=calculate_key(sha,k,H,'E',session_id,int(mac_algo,10))
        c.iterationStateStore['mac_s']=calculate_key(sha,k,H,'F',session_id,int(mac_algo,10))
        if not c.iterationStateStore['group_flag']:
            c.iterationStateStore['seq']=4
        else:
            c.iterationStateStore['seq']=3
        #for hmac uint32, why this value is 4? 
        #the number of packets client sent minus 2 is the sequence of first encryption
        #kegex is 4, but others may be not. e.g. ecdhkex is 3.
        #different algo has different key length, here is aes128cbc
    else:
        c.iterationStateStore['fault']=True
        
    
import pyaes
import hmac    
def en(c,s):
    enc=convertBlobToStr(s.dataModel.InternalValue)
    """if c.iterationStateStore['seq']>4:
        enc=binascii.unhexlify('0000003c09320000000575736572310000000e7373682d636f6e6e656374696f6e0000000870617373776f726400000000057573657231130812cae3552b2650')
    if c.iterationStateStore['seq']==4:
        enc=binascii.unhexlify('0000001c0a050000000c7373682d7573657261757468cd94400b5eb0522292da')"""
    mac_c=c.iterationStateStore['mac_c']
    en_algo=c.iterationStateStore['en_algo'][-1]
    if en_algo=='cbc':
        iv_c=c.iterationStateStore['iv_c']
        key_c=c.iterationStateStore['key_c']
    
        aescipher=pyaes.AESModeOfOperationCBC(key_c,iv=iv_c)
        text=""
        for i in range(len(enc)/16):
            text = text+aescipher.encrypt(enc[i*16:(i+1)*16])
    elif en_algo=='ctr':
        iv_c=c.iterationStateStore['iv_c']
        key_c=c.iterationStateStore['key_c']

        #if type(c.iterationStateStore['iv_c'])==type('s'):
        iv_c=long(binascii.hexlify(iv_c),16)
        #print hex(c.iterationStateStore['iv_c'])
        counter = pyaes.Counter(initial_value = iv_c)
        aescipher=pyaes.AESModeOfOperationCTR(key_c,counter=counter)
        text=aescipher.encrypt(enc)
        #c.iterationStateStore['iv_c']=c.iterationStateStore['iv_c']+2
    
    mac_algo=c.iterationStateStore['hmac']
    if mac_algo[1]=='sha1':
        mac_type='sha1'
        mac_len=20
    elif mac_algo[1]=='sha2':
        mac_type='sha256'
        mac_len=32
    if len(mac_algo)>2:
        mac_len=int(mac_algo[-1],10)/8

    hmac_sha=hmac.new(mac_c,struct.pack('!I',c.iterationStateStore['seq'])+enc,lambda:hashlib.new(mac_type))
    #??add 4 why?
    print binascii.hexlify(struct.pack('!I',c.iterationStateStore['seq'])+enc)
    c.iterationStateStore['seq']=c.iterationStateStore['seq']+1
    c.iterationStateStore['enc']=text+hmac_sha.digest()[:mac_len]
    

def den(c,s):
    mac_algo=c.iterationStateStore['hmac']
    if mac_algo[1]=='sha1':
        mac_type='sha1'
        mac_len=20
    elif mac_algo[1]=='sha2':
        mac_len=32
    if len(mac_algo)>2:
        mac_len=int(mac_algo[-1],10)/8
    enc=convertBlobToStr(s.dataModel.find('m').InternalValue)[:-mac_len]

    en_algo=c.iterationStateStore['en_algo'][-1]
    if en_algo=='cbc':
        iv_s=c.iterationStateStore['iv_s']
        key_s=c.iterationStateStore['key_s']
        aescipher=pyaes.AESModeOfOperationCBC(key_s,iv=iv_s)
        text=""
        for i in range(len(enc)/16):
            text = text+aescipher.decrypt(enc[i*16:(i+1)*16])
    elif en_algo=='ctr':
        iv_s=c.iterationStateStore['iv_s']
        key_s=c.iterationStateStore['key_s']
        counter = pyaes.Counter(initial_value = long(binascii.hexlify(iv_s),16))
        aescipher=pyaes.AESModeOfOperationCTR(key_s,counter=counter)
        text=aescipher.decrypt(enc)

    print binascii.hexlify(text)
    print text
    mac_s=c.iterationStateStore['mac_s']
    hmac_sha=hmac.new(mac_s,struct.pack('!I',c.iterationStateStore['seq']-1)+text,lambda:hashlib.new(mac_type))
    print binascii.hexlify(hmac_sha.digest())




def send(c,s):
    s.dataModel.find('Payload').DefaultValue=convertStrToBlob(c.iterationStateStore['enc'])

    
    


