#include "simple/feeder.hpp"
#include <xf_security/sha1.hpp>

template <unsigned int hashLength>
void simpleEater(hls::stream<ap_uint<hashLength>> targetHshStrm, hls::stream<ap_uint<hashLength>>& hshStrm, hls::stream<bool> eHshStrm, ap_uint<512>* foundPassword) {
	ap_uint<hashLength> targetHash = targetHshStrm.read();

    bool isFinish = eHshStrm.read();
    int messageIdx = 0;
    while(!isFinish) {
    	ap_uint<hashLength> hash = hshStrm.read();
    	if(targetHash == hash) {
    		foundPassword[0].range(511, 448) = messageIdx;
    		break;
    	}

    	isFinish = eHshStrm.read();
    	messageIdx++;
    }
}

extern "C" void sha1Kernel(ap_uint<512>* inputData, ap_uint<512>* foundPassword) {
#pragma HLS dataflow
	// Note: Dataflow config
    const unsigned int fifoBatch = 4;
    const unsigned int burstLength = 128;
    const unsigned int fifoDepth = burstLength * fifoBatch;
    // Note: Dataflow message config
    // TODO: Change password len to bits?
    const unsigned int pwdLen = 8;
    const unsigned int msgWidth = 32;
    const unsigned int msgDepth = fifoDepth * (512 / msgWidth);
    // Note: Hash algorithm config
    const unsigned int hashLen = 160;


#pragma HLS INTERFACE m_axi offset = slave latency = 64 \
	num_write_outstanding = 16 num_read_outstanding = 16 \
	max_write_burst_length = 64 max_read_burst_length = 64 \
	bundle = gmem0_0 port = inputData

#pragma HLS INTERFACE m_axi offset = slave latency = 64 \
	num_write_outstanding = 16 num_read_outstanding = 16 \
	max_write_burst_length = 64 max_read_burst_length = 64 \
	bundle = gmem0_0 port = foundPassword

    // Note: FEEDER OUTPUT
    hls::stream<ap_uint<msgWidth>> msgStrm;
#pragma HLS stream variable = msgStrm depth = msgDepth
#pragma HLS resource variable = msgStrm core = FIFO_BRAM
    hls::stream<ap_uint<64> > msgLenStrm;
#pragma HLS stream variable = msgLenStrm depth = 128
#pragma HLS resource variable = msgLenStrm core = FIFO_BRAM
    hls::stream<bool> eMsgLenStrm;
#pragma HLS stream variable = eMsgLenStrm depth = 128
#pragma HLS resource variable = eMsgLenStrm core = FIFO_LUTRAM
    hls::stream<ap_uint<hashLen>> targetHshStrm;
#pragma HLS stream variable = targetHshStrm depth = fifoBatch
#pragma HLS resource variable = targetHshStrm core = FIFO_LUTRAM
    // Note: HASH OUTPUT
    hls::stream<ap_uint<hashLen>> hshStrm;
#pragma HLS stream variable = hshStrm depth = fifoBatch
#pragma HLS resource variable = hshStrm core = FIFO_LUTRAM
    hls::stream<bool> eHshStrm;
#pragma HLS stream variable = eHshStrm depth = fifoBatch
#pragma HLS resource variable = eHshStrm core = FIFO_LUTRAM

    // TODO: Extract target hash from feeder

    simpleFeeder<burstLength, pwdLen, msgWidth, hashLen>(inputData, targetHshStrm, msgStrm, msgLenStrm, eMsgLenStrm);
    xf::security::sha1<msgWidth>(msgStrm, msgLenStrm, eMsgLenStrm, hshStrm, eHshStrm);

    bool isFinish = eHshStrm.read();
    while(!isFinish) {
    	ap_uint<128> hash = hshStrm.read();
    	int firstByte = hash.range(7, 0);
    	int secondByte = hash.range(15, 8);
    	hls::print("Hash: %d ", (int)firstByte);
    	hls::print("%d\n", (int)secondByte);

    	isFinish = eHshStrm.read();
    }
}
