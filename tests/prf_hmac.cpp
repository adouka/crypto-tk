#include "prf_hmac.hpp"
#include "../src/prf.hpp"


#include <iostream>
#include <iomanip>
#include <string>

using namespace std;

typedef sse::crypto::Prf<64> Prf_64;

bool hmac_tests()
{
	return hmac_test_case_1() && hmac_test_case_2() && hmac_test_case_3() && hmac_test_case_4();
}

bool hmac_test_case_1() {

	array<uint8_t,Prf_64::kKeySize> k;
	k.fill(0x0b);
	
	
	Prf_64 key_64(k.data(),20);	
	string in = "Hi There";
	
	array<uint8_t,64> result_64 = key_64.prf(in);
	
	array<uint8_t,64> reference = {{
							0x87, 0xaa, 0x7c, 0xde, 0xa5, 0xef, 0x61, 0x9d, 0x4f, 0xf0, 0xb4, 0x24, 0x1a, 0x1d, 0x6c, 0xb0,
							0x23, 0x79, 0xf4, 0xe2, 0xce, 0x4e, 0xc2, 0x78, 0x7a, 0xd0, 0xb3, 0x05, 0x45, 0xe1, 0x7c, 0xde,
							0xda, 0xa8, 0x33, 0xb7, 0xd6, 0xb8, 0xa7, 0x02, 0x03, 0x8b, 0x27, 0x4e, 0xae, 0xa3, 0xf4, 0xe4,
							0xbe, 0x9d, 0x91, 0x4e, 0xeb, 0x61, 0xf1, 0x70, 0x2e, 0x69, 0x6c, 0x20, 0x3a, 0x12, 0x68, 0x54
								}};
	
	
	if(result_64 != reference){
		cout << "HMAC Test case 1 failed!\n";
		cout << "Reference: \n";
		for(uint8_t c : reference)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
		
		cout << "Computed: \n";
		for(uint8_t c : result_64)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
	
		return false;
	}
	// cout << "HMAC Test case 1 succeeded!\n";
	return true;	
}

bool hmac_test_case_2() {

	array<uint8_t,4> k = {{ 0x4a, 0x65, 0x66, 0x65}};
	
	
	Prf_64 key_64(k.data(),4);	

	unsigned char in [28] = 	{
							0x77, 0x68, 0x61, 0x74, 0x20, 0x64, 0x6f, 0x20, 0x79, 0x61, 0x20, 0x77, 0x61, 0x6e, 0x74, 0x20,
		                   	0x66, 0x6f, 0x72, 0x20, 0x6e, 0x6f, 0x74, 0x68, 0x69, 0x6e, 0x67, 0x3f
							};
		
	array<uint8_t,64> result_64 = key_64.prf(in, 28);
	
	array<uint8_t,64> reference = 	{{
							0x16, 0x4b, 0x7a, 0x7b, 0xfc, 0xf8, 0x19, 0xe2, 0xe3, 0x95, 0xfb, 0xe7, 0x3b, 0x56, 0xe0, 0xa3,
							0x87, 0xbd, 0x64, 0x22, 0x2e, 0x83, 0x1f, 0xd6, 0x10, 0x27, 0x0c, 0xd7, 0xea, 0x25, 0x05, 0x54,
							0x97, 0x58, 0xbf, 0x75, 0xc0, 0x5a, 0x99, 0x4a, 0x6d, 0x03, 0x4f, 0x65, 0xf8, 0xf0, 0xe6, 0xfd,
							0xca, 0xea, 0xb1, 0xa3, 0x4d, 0x4a, 0x6b, 0x4b, 0x63, 0x6e, 0x07, 0x0a, 0x38, 0xbc, 0xe7, 0x37
									}};
	
	
	if(result_64 != reference){
		cout << "HMAC Test case 2 failed!\n";
		cout << "Reference: \n";
		for(uint8_t c : reference)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
		
		cout << "Computed: \n";
		for(uint8_t c : result_64)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
	
		return false;
	}
	// cout << "HMAC Test case 2 succeeded!\n";
	return true;	
}

bool hmac_test_case_3() {

	array<uint8_t,Prf_64::kKeySize> k;
	k.fill(0xaa);
	
	
	Prf_64 key_64(k.data(),20);	
	unsigned char in [50];
	memset(in,0xdd,50);
		
	array<uint8_t,64> result_64 = key_64.prf(in, 50);
	
	array<uint8_t,64> reference = 	{{
							0xfa, 0x73, 0xb0, 0x08, 0x9d, 0x56, 0xa2, 0x84, 0xef, 0xb0, 0xf0, 0x75, 0x6c, 0x89, 0x0b, 0xe9,
							0xb1, 0xb5, 0xdb, 0xdd, 0x8e, 0xe8, 0x1a, 0x36, 0x55, 0xf8, 0x3e, 0x33, 0xb2, 0x27, 0x9d, 0x39,
							0xbf, 0x3e, 0x84, 0x82, 0x79, 0xa7, 0x22, 0xc8, 0x06, 0xb4, 0x85, 0xa4, 0x7e, 0x67, 0xc8, 0x07,
							0xb9, 0x46, 0xa3, 0x37, 0xbe, 0xe8, 0x94, 0x26, 0x74, 0x27, 0x88, 0x59, 0xe1, 0x32, 0x92, 0xfb
									}};
	
	
	if(result_64 != reference){
		cout << "HMAC Test case 3 failed!\n";
		cout << "Reference: \n";
		for(uint8_t c : reference)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
		
		cout << "Computed: \n";
		for(uint8_t c : result_64)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
	
		return false;
	}
	// cout << "HMAC Test case 3 succeeded!\n";
	return true;	
}

bool hmac_test_case_4() {

	array<uint8_t,25> k = {{ 	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 
								0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19}};
	
	
	Prf_64 key_64(k.data(),25);	
	unsigned char in [50];
	memset(in,0xcd,50);
		
	array<uint8_t,64> result_64 = key_64.prf(in, 50);
	
	array<uint8_t,64> reference = 	{{
								0xb0, 0xba, 0x46, 0x56, 0x37, 0x45, 0x8c, 0x69, 0x90, 0xe5, 0xa8, 0xc5, 0xf6, 0x1d, 0x4a, 0xf7,
								0xe5, 0x76, 0xd9, 0x7f, 0xf9, 0x4b, 0x87, 0x2d, 0xe7, 0x6f, 0x80, 0x50, 0x36, 0x1e, 0xe3, 0xdb,
								0xa9, 0x1c, 0xa5, 0xc1, 0x1a, 0xa2, 0x5e, 0xb4, 0xd6, 0x79, 0x27, 0x5c, 0xc5, 0x78, 0x80, 0x63,
								0xa5, 0xf1, 0x97, 0x41, 0x12, 0x0c, 0x4f, 0x2d, 0xe2, 0xad, 0xeb, 0xeb, 0x10, 0xa2, 0x98, 0xdd
									}};
	
	
	if(result_64 != reference){
		cout << "HMAC Test case 4 failed!\n";
		cout << "Reference: \n";
		for(uint8_t c : reference)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
		
		cout << "Computed: \n";
		for(uint8_t c : result_64)
		{
			cout << hex << setw(2) << setfill('0') << (uint) c;
		}
		cout << endl;
	
		return false;
	}
	// cout << "HMAC Test case 4 succeeded!\n";
	return true;	
}

