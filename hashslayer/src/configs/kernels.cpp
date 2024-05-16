#include "kernels.hpp"

KernelConfig getKernelConfig(HashType hashType) {
	switch(hashType) {
	case eSha1:
		return {16, 32, false, "sha1Kernel"};
	}
}
