#pragma once
#include <string>


enum HashType {
	eSha1
};

struct KernelConfig {
	int coresCount;
	bool isSalted;
	std::string kernelName;
};

KernelConfig getKernelConfig(HashType hashType) {
	switch(hashType) {
	case eSha1:
		return {16, false, "sha1Kernel"};
	}
}
