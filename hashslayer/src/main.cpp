#include <ap_int.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sys/time.h>
#include <new>
#include <cstdlib>
#include <vector>

#include "Hashslayer.hpp"

template<typename T>
T* aligned_alloc(std::size_t num) {
	void* ptr = nullptr;
	if (posix_memalign(&ptr, 4096, num * sizeof(T)))
		throw std::bad_alloc();
	return reinterpret_cast<T*>(ptr);
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		std::cout << "Invalid arguments configuration!" << std::endl;
	}

	// TODO: It depends on hash type and wordlist content
	std::vector<std::string> wordlist;
	for (int i = 0; i < 1; i++) {
		wordlist.push_back("AAAA1111");
		wordlist.push_back("BBBB2222");
		wordlist.push_back("CCCC3333");
		wordlist.push_back("DDDD4444");
		wordlist.push_back("EEEE5555");
		wordlist.push_back("FFFF6666");
		wordlist.push_back("GGGG7777");
		wordlist.push_back("HHHH8888");
		wordlist.push_back("IIII9999");
		wordlist.push_back("JJJJ0000");
		wordlist.push_back("KKKKZZZZ");
		wordlist.push_back("LLLLYYYY");
		wordlist.push_back("MMMMXXXX");
		wordlist.push_back("NNNNVVVV");
		wordlist.push_back("OOOOUUUU");
		wordlist.push_back("PPPPSSSS");
		wordlist.push_back("AAAA!111");
		wordlist.push_back("BBBB@222");
		wordlist.push_back("CCCC#333");
		wordlist.push_back("DDDD$444");
		wordlist.push_back("EEEE%555");
		wordlist.push_back("FFFF^666");
		wordlist.push_back("GGGG&777");
		wordlist.push_back("HHHH*888");
		wordlist.push_back("IIII(999");
		wordlist.push_back("JJJJ)000");
		wordlist.push_back("KKKK-ZZZ");
		wordlist.push_back("LLLL_YYY");
		wordlist.push_back("MMMM+XXX");
		wordlist.push_back("NNNN=VVV");
		wordlist.push_back("OOOO`UUU");
		wordlist.push_back("PPPP>SSS");
	}

	// TODO: Parse target hash from string
	// TODO: Pass target hash to Hashslayer
	HashslayerSettings settings;
	settings.attackType = eDictionary;
	settings.hashType = HashType::eSha1;
	settings.xclbinPath = argv[1];
	settings.maxPasswordLength = 8;
	settings.passwordCount = wordlist.size();
	Hashslayer app(settings);

	std::cout << "[+] Transfering wordlist to FPGA..." << std::endl;
	app.transferWordlist(wordlist);
	std::cout << "[+] Starting kernel..." << std::endl;
	app.start();
	std::cout << "[+] Waiting for kernel..." << std::endl;
	app.wait();
	std::cout << "[+] Wordlist exhausted!" << std::endl;
	std::string foundPassword = app.getResult();
	std::cout << "[+] Found password: " << foundPassword << std::endl;
}
