#include "cipher.hpp"

#include "random.hpp"

#include <exception>

#include <openssl/aes.h>

#include <iostream>
#include <iomanip>

namespace sse
{

namespace crypto
{

class Cipher::CipherImpl
{
public:
		
	// We use 48 bits IV. 
	// This is enough to encrypt 2^48 blocks which is the security bound of counter mode
	static constexpr uint8_t kIVSize = 6; 

	CipherImpl();
	
	CipherImpl(const std::array<uint8_t,kKeySize>& k);
	
	~CipherImpl();

	void gen_subkeys(const unsigned char *userKey);
	void reset_iv();

	void encrypt(const unsigned char* in, const size_t &len, unsigned char* out);
	void encrypt(const std::string &in, std::string &out);
	void decrypt(const unsigned char* in, const size_t &len, unsigned char* out);
	void decrypt(const std::string &in, std::string &out);


private:	
	AES_KEY aes_enc_key_;
	
	unsigned char iv_[kIVSize];
	uint64_t remaining_block_count_;
};
	

Cipher::Cipher() : cipher_imp_(new CipherImpl())
{
}

Cipher::Cipher(const std::array<uint8_t,kKeySize>& k) : cipher_imp_(new CipherImpl(k))
{	
}

Cipher::~Cipher() 
{ 
	delete cipher_imp_;
}

void Cipher::encrypt(const std::string &in, std::string &out)
{
	cipher_imp_->encrypt(in, out);
}
void Cipher::decrypt(const std::string &in, std::string &out)
{
	cipher_imp_->decrypt(in, out);
}

// Cipher implementation

Cipher::CipherImpl::CipherImpl()
{
	unsigned char k[kKeySize];
	random_bytes(kKeySize, k);
	gen_subkeys(k);
	reset_iv();
}

Cipher::CipherImpl::CipherImpl(const std::array<uint8_t,kKeySize>& k)
{	
	gen_subkeys(k.data());
	reset_iv();
}

Cipher::CipherImpl::~CipherImpl() 
{ 
	// erase subkeys
	memset(&aes_enc_key_, 0x00, sizeof(AES_KEY));
}

#define MIN(a,b) (((a) > (b)) ? (b) : (a))
void Cipher::CipherImpl::gen_subkeys(const unsigned char *userKey)
{
	if (AES_set_encrypt_key(userKey, 128, &aes_enc_key_) != 0)
	{
		// throw an exception
		throw std::runtime_error("Unable to init AES subkeys");
	}
	
	// Compute maximum number of blocks that can be encrypted with the same key
	// This number comes from the security reduction of CTR mode (2^48 blocks at most to retain 32 bits of security)
	// and from the IV length (not more than 2^(8*kIVSize) different IVs) 
	remaining_block_count_ = ((uint64_t) 1) << MIN(48,8*kIVSize); 
}

void Cipher::CipherImpl::reset_iv()
{
	memset(iv_, 0x00, kIVSize);
}

void Cipher::CipherImpl::encrypt(const unsigned char* in, const size_t &len, unsigned char* out)
{
	if(remaining_block_count_ < len/16){
		// throw an exception
		throw std::runtime_error("Too many blocks were encrypted with the same key. Encrypting using this key is now insecure.");
	}
	
    unsigned char enc_iv[AES_BLOCK_SIZE];
    unsigned char ecount[AES_BLOCK_SIZE];
    memset(ecount, 0x00, AES_BLOCK_SIZE);
	
	unsigned int num = 0;
	
	memcpy(out, iv_, kIVSize); // copy iv first

	if(kIVSize != AES_BLOCK_SIZE)
		memset(enc_iv, 0, AES_BLOCK_SIZE);
	
	memcpy(enc_iv+AES_BLOCK_SIZE-kIVSize, iv_, kIVSize);
	
	// now append the ciphertext
    AES_ctr128_encrypt(in, out+kIVSize, len, &aes_enc_key_, enc_iv, ecount, &num);
	
	// erase ecount to avoid (partial) recovery of the last block
	memset(ecount, 0x00, AES_BLOCK_SIZE);
	
	// decrement the block counter
	remaining_block_count_ -= len/16;
}

void Cipher::CipherImpl::encrypt(const std::string &in, std::string &out)
{
	unsigned int len = in.size();
	out.resize(len+kIVSize);
	encrypt((unsigned char*)in.data(), len, (unsigned char*)out.data());
}

void Cipher::CipherImpl::decrypt(const unsigned char* in, const size_t &len, unsigned char* out)
{
    unsigned char ecount[AES_BLOCK_SIZE];
    unsigned char dec_iv[AES_BLOCK_SIZE];
    memset(ecount, 0x00, AES_BLOCK_SIZE);
	
	unsigned int num = 0;
	
	if(kIVSize != AES_BLOCK_SIZE)
		memset(dec_iv, 0, AES_BLOCK_SIZE);
	
	memcpy(dec_iv+AES_BLOCK_SIZE-kIVSize, in, kIVSize); // copy iv first
	
	// now append the ciphertext
    AES_ctr128_encrypt(in+kIVSize, out, len, &aes_enc_key_, dec_iv, ecount, &num);
	
	// erase ecount to avoid (partial) recovery of the last block
	memset(ecount, 0x00, AES_BLOCK_SIZE);
}

void Cipher::CipherImpl::decrypt(const std::string &in, std::string &out)
{
	unsigned int len = in.size();
	out.resize(len-kIVSize);
	decrypt((unsigned char*)in.data(), len, (unsigned char*)out.data());
}
	

}
}