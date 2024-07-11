#pragma once
#include <string>

enum HashType {
	eSha1, eSha256, eSha512,
	eSha3512, eSha3256,
	eHmacSha1, eHmacSha256, eHmacSha512,
	ePbkdfHmacSha256
};

struct KernelConfig {
	int coresCount;
	int messageSize;
	bool isSalted;
	int kernelsCount;
	std::string name;
};

KernelConfig getKernelConfig(HashType hashType);
