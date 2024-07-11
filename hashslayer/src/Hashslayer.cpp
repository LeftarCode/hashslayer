#include "Hashslayer.hpp"


Hashslayer::Hashslayer(HashslayerSettings settings) : m_settings(settings) {
    initDevice();

	cl_int err;
    m_context = cl::Context(m_device, NULL, NULL, NULL, &err);
    m_queue = cl::CommandQueue (m_context, m_device, CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);

    std::string devName = m_device.getInfo<CL_DEVICE_NAME>();
    std::cout << "[+] Selected Device: " << devName << std::endl;

    loadXCLBinary();

    m_kernelConfig = getKernelConfig(settings.hashType);
    for(int i = 0; i < m_kernelConfig.kernelsCount; i++) {
    	std::string kernelName = m_kernelConfig.name + "_" + std::to_string(i+1);
    	m_kernel.push_back(cl::Kernel(m_program, kernelName.c_str(), &err));
    }
}

void Hashslayer::transferWordlist(std::vector<std::string> wordlist) {
	cl_int err;
	if (m_settings.passwordCount % m_kernelConfig.coresCount != 0) {
		std::cout << "[-] Password Count must be divisible by " << m_kernelConfig.coresCount << std::endl;
	}

	// TODO: Copy data from host to FPGA
	// TODO: These data should be available all the time
	auto inAxiBlocks = convertWordlist2AXI(wordlist);
	std::vector<ap_int<512>> outAxiBlocks(m_kernelConfig.kernelsCount);
    std::cout << "[+] Creating buffers..." << std::endl;
	createBuffers(inAxiBlocks, outAxiBlocks);

    std::vector<cl::Memory> inputData;
    for (int i = 0; i < m_kernelConfig.kernelsCount; i++) {
    	inputData.push_back(m_inBuffer[i]);
    }

    std::cout << "[+] Transferring data..." << std::endl;
    m_queue.enqueueMigrateMemObjects(inputData, 0);
}

void Hashslayer::start() {
	for(auto kernel : m_kernel) {
		m_queue.enqueueTask(kernel);
	}
}

void Hashslayer::wait() {
	m_queue.finish();
}

std::string Hashslayer::getResult() {
//	cl_int err;
//	ap_int<512> block;
//
//	std::cout << "[+] Mapping FPGA buffer to host VA space..." << std::endl;
//	auto fpgaMemory = (ap_int<512>*)m_queue.enqueueMapBuffer(m_outBuffer, CL_TRUE, CL_MAP_READ, 0, sizeof(ap_int<512>), NULL, NULL, &err);
//	std::cout << "[+] Transferring buffer from FPGA..." << std::endl;
//	m_queue.enqueueMigrateMemObjects({m_outBuffer},CL_MIGRATE_MEM_OBJECT_HOST);
//	std::cout << "[+] Copying data to local buffer" << std::endl;
//	wait();
//	std::cout << "[+] Unpacking AXI block..." << std::endl;
//
//	std::string result = "";
//	for(int i = 0; i < m_settings.maxPasswordLength; i++) {
//		result += (char)fpgaMemory[0].range(i*8+7,i*8);
//	}
//	result += '\0';

	return "";
}

void Hashslayer::initDevice() {
	bool foundDevice = false;
	std::vector<cl::Platform> platforms;

    cl::Platform::get(&platforms);
    for(size_t i = 0; (i < platforms.size() ) & (foundDevice == false) ;i++){
        cl::Platform platform = platforms[i];
        std::string platformName = platform.getInfo<CL_PLATFORM_NAME>();
        if ( platformName == "Xilinx"){
            m_devices.clear();
            platform.getDevices(CL_DEVICE_TYPE_ACCELERATOR, &m_devices);
			if (m_devices.size()){
				m_device = m_devices[0];
				foundDevice = true;
				break;
			}
        }
    }
    if (foundDevice == false){
       std::cout << "Error: Unable to find Target Device "
           << m_device.getInfo<CL_DEVICE_NAME>() << std::endl;
       return;
    }
}

void Hashslayer::loadXCLBinary() {
	cl_int err;
    std::ifstream bin_file(m_settings.xclbinPath, std::ifstream::binary);
    bin_file.seekg (0, bin_file.end);
    unsigned nb = bin_file.tellg();
    bin_file.seekg (0, bin_file.beg);
    char *buf = new char [nb];
    bin_file.read(buf, nb);

    // Creating Program from Binary File
    cl::Program::Binaries bins;
    bins.push_back({buf,nb});

    m_devices.resize(1);
    m_program = cl::Program(m_context, m_devices, bins, NULL, &err);
}

void Hashslayer::createBuffers(std::vector<std::vector<ap_int<512>>> kernelInData, std::vector<ap_int<512>> kernelOutData) {
	cl_int err;
    int passwordsInBlock = sizeof(ap_int<512>)/m_settings.maxPasswordLength;
    int blocksCount = m_settings.passwordCount/passwordsInBlock;
    blocksCount += m_kernelConfig.coresCount;
    for(int i = 0; i < m_kernelConfig.kernelsCount; i++) {
    	cl_mem_ext_ptr_t extPtrIn = {MemoryBanks[i], kernelInData[i].data(), 0};
    	cl_mem_ext_ptr_t extPtrOut = {MemoryBanks[i], &kernelOutData[i], 0};
        auto inBuffer = cl::Buffer(m_context, CL_MEM_EXT_PTR_XILINX | CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(ap_int<512>)*kernelInData[i].size(), &extPtrIn, &err);
        auto outBuffer = cl::Buffer(m_context, CL_MEM_EXT_PTR_XILINX | CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, sizeof(ap_int<512>), &extPtrOut, &err);

    	m_inBuffer.push_back(inBuffer);
    	m_outBuffer.push_back(outBuffer);
    }
    m_queue.flush();
    m_queue.finish();

    for(int i = 0; i < m_kernelConfig.kernelsCount; i++) {
        m_kernel[i].setArg(0, m_inBuffer[i]);
        m_kernel[i].setArg(1, m_outBuffer[i]);
    }
}

std::vector<std::string> padAndShuffle(std::string first, std::string second, int messageSize, int passwordLength) {
	int messageBytes = messageSize / 8;
	std::vector<std::string> res(passwordLength/messageBytes);

	first.resize(passwordLength);
	second.resize(passwordLength);

	for (int i = 0; i < passwordLength/messageBytes; i++) {
		std::string newString = "";
		newString += first.substr(i*messageBytes, messageBytes);
		newString += second.substr(i*messageBytes, messageBytes);
		res[i] = newString;
	}

	return res;
}

std::vector<std::string> Hashslayer::transformWordlist(const std::vector<std::string>& wordlist) {
	std::vector<std::string> transformedWordlist(wordlist.size());
	for (size_t i = 0; i < wordlist.size()/m_kernelConfig.coresCount; i++) {
		for (size_t j = 0; j < m_kernelConfig.coresCount/2; j++) {
			size_t firstIdx = i*m_kernelConfig.coresCount + j;
			size_t secondIdx = i*m_kernelConfig.coresCount + j + m_kernelConfig.coresCount/2;
			std::vector<std::string> shuffled =
					padAndShuffle(wordlist[firstIdx], wordlist[secondIdx], m_kernelConfig.messageSize, m_settings.maxPasswordLength);
			transformedWordlist[firstIdx] = shuffled[0];
			transformedWordlist[secondIdx] = shuffled[1];
		}
	}

	return transformedWordlist;
}

std::vector<ap_int<512>> Hashslayer::packWordlist(std::vector<std::string> wordlist) {
	int passwordsInOneBlock = 512 / (m_settings.maxPasswordLength*8);
	int singlePasswordBits = m_settings.maxPasswordLength * 8;
	std::vector<ap_int<512>> packedWordlist;

	int i = 0;
	ap_int<512> block;
	block.range(511,0) = 0;
	for(const auto& password : wordlist) {
		if (password.length() > m_settings.maxPasswordLength) {
			std::cout << "[-] Password " << password << " skipped due to it's length" << std::endl;
			continue;
		}
		int offset = i * singlePasswordBits;
		for (int j = 0; j < password.length(); j++) {
			block.range(offset + 7, offset) = password[j];
			offset += 8;
		}

		if (i == passwordsInOneBlock - 1) {
			packedWordlist.push_back(block);
			block.range(511,0) = 0;
		}
		i = ++i % passwordsInOneBlock;
	}

	return packedWordlist;
}

std::vector<std::vector<ap_int<512>>> Hashslayer::splitBlocks(std::vector<ap_int<512>> axiBlocks, int kernelsCount) {
	int blocksCount = axiBlocks.size();
	int blocksPerKernel = blocksCount / kernelsCount;

	ap_int<512> zero = 0;

	std::vector<std::vector<ap_int<512>>> res;
	for (int i = 0; i < kernelsCount; i++) {
		std::vector<ap_int<512>> blocks;
		for (int j = 0; j < m_kernelConfig.coresCount; j++) {
			blocks.push_back(zero);
		}
		for (int j = 0; j < blocksPerKernel; j++) {
			blocks.push_back(axiBlocks[i*blocksPerKernel + j]);
		}
		res.push_back(blocks);
	}

	return res;
}

std::vector<std::vector<ap_int<512>>> Hashslayer::convertWordlist2AXI(const std::vector<std::string>& wordlist) {
	auto transformedWordlist = transformWordlist(wordlist);

	std::cout << "[+] Packing wordlist to AXI blocks..." << std::endl;
	auto axiBlocks = packWordlist(transformedWordlist);
	std::cout << "[+] Spliting AXI blocks..." << std::endl;
	auto finalBlocks = splitBlocks(axiBlocks, m_kernelConfig.kernelsCount);
	std::cout << "[+] Redistribute blocks..." << std::endl;

	int passwordsInBlock = 64/m_settings.maxPasswordLength;

	for(auto& kernelBlocks : finalBlocks) {
		int passwordsForCore = ((kernelBlocks.size()-m_kernelConfig.coresCount)*passwordsInBlock) / m_kernelConfig.coresCount;
		ap_int<512> configBlock;
		configBlock.range(511, 0) = 0;
		configBlock.range(511, 448) = m_settings.maxPasswordLength;
		configBlock.range(447, 384) = passwordsForCore;

		for (int i = 0; i < m_kernelConfig.coresCount; i++) {
			kernelBlocks[i] = configBlock;
		}
	}

	return finalBlocks;
}
