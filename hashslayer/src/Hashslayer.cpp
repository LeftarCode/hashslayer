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
    m_kernel = cl::Kernel(m_program, m_kernelConfig.name.c_str(), &err);

    // TODO: Select explicit interfacess
    int passwordsInBlock = sizeof(ap_int<512>)/m_settings.maxPasswordLength;
    int blocksCount = m_settings.passwordCount/passwordsInBlock;
    blocksCount += m_kernelConfig.coresCount;
    m_inBuffer = cl::Buffer(m_context, CL_MEM_READ_ONLY, sizeof(ap_int<512>)*blocksCount, NULL, &err);
    m_outBuffer = cl::Buffer(m_context, CL_MEM_WRITE_ONLY, sizeof(ap_int<512>), NULL, &err);
    m_queue.flush();
    m_queue.finish();

    m_kernel.setArg(0, m_inBuffer);
    m_kernel.setArg(1, m_outBuffer);
}

void Hashslayer::transferWordlist(std::vector<std::string> wordlist) {
	cl_int err;
	if (m_settings.passwordCount % m_kernelConfig.coresCount != 0) {
		std::cout << "[-] Password Count must be divisible by " << m_kernelConfig.coresCount << std::endl;
	}

	wordlist = transformWordlist(wordlist);

	ap_int<512> configBlock;
	configBlock.range(511, 0) = 0;
	configBlock.range(511, 448) = m_settings.maxPasswordLength;
	configBlock.range(447, 384) = m_settings.passwordCount / m_kernelConfig.coresCount;

	std::cout << "[+] Packing wordlist to AXI blocks..." << std::endl;
	auto axiBlocks = packWordlist(wordlist);
	for(int i = 0; i < m_kernelConfig.coresCount; i++) {
		axiBlocks.insert(axiBlocks.begin(), 1, configBlock);
	}

	std::cout << "[+] Mapping FPGA buffer to host VA space..." << std::endl;
	auto fpgaMemory = (ap_int<512>*)m_queue.enqueueMapBuffer(m_inBuffer, CL_TRUE, CL_MAP_WRITE, 0, sizeof(ap_int<512>) * axiBlocks.size(), NULL, NULL, &err);
	std::cout << "[+] Copying data to buffer..." << std::endl;
	memcpy(fpgaMemory, axiBlocks.data(), sizeof(ap_int<512>) * axiBlocks.size());
	std::cout << "[+] Transferring buffer to FPGA..." << std::endl;
	m_queue.enqueueMigrateMemObjects({m_inBuffer},0);
	std::cout << "[+] Transfer done!" << std::endl;
}

void Hashslayer::start() {
	m_queue.enqueueTask(m_kernel);
}

void Hashslayer::wait() {
	m_queue.finish();
}

std::string Hashslayer::getResult() {
	cl_int err;
	ap_int<512> block;

	std::cout << "[+] Mapping FPGA buffer to host VA space..." << std::endl;
	auto fpgaMemory = (ap_int<512>*)m_queue.enqueueMapBuffer(m_outBuffer, CL_TRUE, CL_MAP_READ, 0, sizeof(ap_int<512>), NULL, NULL, &err);
	std::cout << "[+] Transferring buffer from FPGA..." << std::endl;
	m_queue.enqueueMigrateMemObjects({m_outBuffer},CL_MIGRATE_MEM_OBJECT_HOST);
	std::cout << "[+] Copying data to local buffer" << std::endl;
	wait();
	std::cout << "[+] Unpacking AXI block..." << std::endl;

	std::string result = "";
	for(int i = 0; i < m_settings.maxPasswordLength; i++) {
		result += (char)fpgaMemory[0].range(i*8+7,i*8);
	}
	result += '\0';

	return result;
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
