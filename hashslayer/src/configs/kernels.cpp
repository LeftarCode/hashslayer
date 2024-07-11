#include "kernels.hpp"

KernelConfig getKernelConfig(HashType hashType) {
	switch(hashType) {
	case eSha1:
		return {16, 32, false, 1, "sha1Kernel"};
	case eSha256:
		return {16, 32, false, 4, "sha256Kernel"};
	case eSha512:
		return {16, 32, false, 2, "sha512Kernel"};
	case eSha3512:
		return {16, 32, false, 1, "sha3512Kernel"};
	case eSha3256:
		return {16, 32, false, 1, "sha3256Kernel"};
	case eHmacSha1:
		return {16, 32, true, 1, "hmacSha1Kernel_1"};
	case eHmacSha256:
		return {16, 32, true, 1, "hmacSha256Kernel_1"};
	case eHmacSha512:
		return {16, 32, true, 1, "hmacSha512Kernel_1"};
	case ePbkdfHmacSha256:
		return {16, 32, true, 1, "pbkdfHmacSha256Kernel"};
	}
}
