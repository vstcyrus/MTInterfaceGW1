#include "StdAfx.h"
#include "ManagerIF.h"

byte ManagerIF::m_key[AES::DEFAULT_KEYLENGTH] = {0xad,0xbd,0xc1,0x87,0x9b,0xe2,0xf5,0x60,0x5a,0x47,0x3c,0x9b,0xe2,0x28,0xed,0xc6};

bool ManagerIF::EncryptPassword(string & src,string & sin)
{
	sin.clear();
	string cipher;
	try
	{
		ECB_Mode< AES >::Encryption e;
		e.SetKey(m_key, sizeof(m_key));

		// The StreamTransformationFilter adds padding
		//  as required. ECB and CBC Mode must be padded
		//  to the block size of the cipher.
		StringSource(src, true, 
			new StreamTransformationFilter(e,
				new StringSink(cipher)
			) // StreamTransformationFilter      
		); // StringSource

		
		StringSource(cipher, true,
			new HexEncoder(
				new StringSink(sin)));

		return true;
	}
	catch(const CryptoPP::Exception& ex)
	{
		printf("Encrypt password failed for error:%d!\n",ex.GetErrorType());
		return false;
	}

}

bool ManagerIF::DecryptPassword(string & src,string & sin)
{
	sin.clear();
	string cipher;

	StringSource(src, true,
		new HexDecoder(
			new StringSink(cipher)));

	try
	{
		ECB_Mode< AES >::Decryption d;
		d.SetKey(m_key, sizeof(m_key));

		// The StreamTransformationFilter removes
		//  padding as required.
		StringSource (cipher, true, 
			new StreamTransformationFilter(d,
				new StringSink(sin)
			) // StreamTransformationFilter
		); // StringSource
		return true;
	}
	catch(const CryptoPP::Exception& ex)
	{
		printf("Decrypt password failed for error:%d!\n",ex.GetErrorType());
		return false;
	}
}
