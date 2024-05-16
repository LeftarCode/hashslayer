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
	for (int i = 0; i < 16; i++) {
		wordlist.push_back("AAAAAAAA");
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
