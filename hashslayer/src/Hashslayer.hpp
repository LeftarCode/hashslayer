#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <ap_int.h>
#include "configs/kernels.hpp"
#define CL_HPP_CL_1_2_DEFAULT_BUILD
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#define CL_HPP_ENABLE_PROGRAM_CONSTRUCTION_FROM_ARRAY_COMPATIBILITY 1

#include <CL/cl2.hpp>

enum AttackType {
	eDictionary, eBruteforce
};

struct HashslayerSettings {
	AttackType attackType;
	HashType hashType;
	std::string xclbinPath;
	uint32_t maxPasswordLength;
	uint32_t passwordCount;
};

const int MemoryBanks[] = {
		XCL_MEM_DDR_BANK0,
		XCL_MEM_DDR_BANK1,
		XCL_MEM_DDR_BANK2,
		XCL_MEM_DDR_BANK3
};

class Hashslayer {
public:
	Hashslayer(HashslayerSettings settings);
	void transferWordlist(std::vector<std::string> wordlist);
	void start();
	void wait();
	std::string getResult();
private:
	HashslayerSettings m_settings;
	KernelConfig m_kernelConfig;
    std::vector<cl::Device> m_devices;
	cl::Device m_device;
	cl::Context m_context;
	cl::CommandQueue m_queue;
	cl::Program::Binaries m_xclBins;
	cl::Program m_program;
	std::vector<cl::Kernel> m_kernel;
	std::vector<cl::Buffer> m_inBuffer, m_outBuffer;

	void initDevice();
	void loadXCLBinary();
	void createBuffers(std::vector<std::vector<ap_int<512>>> kernelInData, std::vector<ap_int<512>> kernelOutData);
	std::vector<std::string> transformWordlist(const std::vector<std::string>& wordlist);
	std::vector<ap_int<512>> packWordlist(std::vector<std::string> wordlist);
	std::vector<std::vector<ap_int<512>>> splitBlocks(std::vector<ap_int<512>> axiBlocks, int kernelsCount);
	std::vector<std::vector<ap_int<512>>> convertWordlist2AXI(const std::vector<std::string>& wordlist);
};
