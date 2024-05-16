#pragma once
#include <string>

enum HashType {
	eSha1
};

struct KernelConfig {
	int coresCount;
	bool isSalted;
	std::string name;
};

KernelConfig getKernelConfig(HashType hashType);
