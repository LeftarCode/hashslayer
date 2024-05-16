#include "simple/feeder.hpp"
#include <xf_security/sha1.hpp>
#include "kernel_config.hpp"

template <unsigned int _burstLength, unsigned int _channelNumber>
static void readIn(ap_uint<512>* ptr,
                   hls::stream<ap_uint<512> >& textInStrm,
                   hls::stream<ap_uint<64> >& textLengthStrm,
                   hls::stream<ap_uint<64> >& textNumStrm) {
    ap_uint<64> textLength;
    ap_uint<64> textNum;

LOOP_READ_CONFIG:
    for (unsigned char i = 0; i < _channelNumber; i++) {
#pragma HLS pipeline II = 1
        ap_uint<512> axiBlock = ptr[i];
        textLength = axiBlock.range(511, 448);
        textNum = axiBlock.range(447, 384);
        if (i == 0) {
            textLengthStrm.write(textLength);
            textNumStrm.write(textNum);
        }
    }

    ap_uint<64> totalAxiBlock = textNum * textLength * _channelNumber / 64;

LOOP_READ_DATA:
    for (ap_uint<64> i = 0; i < totalAxiBlock; i += _burstLength) {
        ap_uint<16> readLen, nextStop;

        nextStop = i + _burstLength;
        if (nextStop < totalAxiBlock) {
            readLen = _burstLength;
        } else {
            readLen = totalAxiBlock - i;
        }

        for (ap_uint<16> j = 0; j < readLen; j++) {
#pragma HLS pipeline II = 1
            ap_uint<512> axiBlock = ptr[_channelNumber + i + j];
            textInStrm.write(axiBlock);
        }
    }
}

template <unsigned int _channelNumber>
void splitText(hls::stream<ap_uint<512> >& textStrm, hls::stream<ap_uint<32> > msgStrm[_channelNumber]) {
#pragma HLS inline off
    ap_uint<512> axiWord = textStrm.read();
    for (unsigned int i = 0; i < _channelNumber; i++) {
#pragma HLS unroll
        for (unsigned int j = 0; j < (512 / _channelNumber / 32); j++) {
#pragma HLS pipeline II = 1
            msgStrm[i].write(axiWord(i * GRP_WIDTH + j * 32 + 31, i * GRP_WIDTH + j * 32));
        }
    }
}

template <unsigned int _channelNumber, unsigned int _burstLength>
void splitInput(hls::stream<ap_uint<512> >& textInStrm,
                hls::stream<ap_uint<64> >& textLengthStrm,
                hls::stream<ap_uint<64> >& textNumStrm,
                hls::stream<ap_uint<32> > msgStrm[_channelNumber],
                hls::stream<ap_uint<64> > msgLenStrm[_channelNumber],
                hls::stream<bool> eMsgLenStrm[_channelNumber]) {
    ap_uint<64> textLength = textLengthStrm.read();
    ap_uint<64> textLengthInGrpSize = textLength / GRP_SIZE;
    ap_uint<64> textNum = textNumStrm.read();

LOOP_TEXTNUM:
    for (ap_uint<64> i = 0; i < textNum; i++) {
        for (unsigned int j = 0; j < _channelNumber; j++) {
#pragma HLS unroll
            eMsgLenStrm[j].write(false);
            msgLenStrm[j].write(textLength);
        }
        for (int j = 0; j < textLengthInGrpSize; j++) {
            splitText<_channelNumber>(textInStrm, msgStrm);
        }
    }
    for (unsigned int i = 0; i < _channelNumber; i++) {
        eMsgLenStrm[i].write(true);
    }
}

extern "C" void sha1Kernel(ap_uint<512>* inputData, ap_uint<512>* outputData) {
#pragma HLS dataflow

    const unsigned int fifobatch = 4;
    const unsigned int _channelNumber = CH_NM;
    const unsigned int _burstLength = BURST_LEN;
    const unsigned int fifoDepth = _burstLength * fifobatch;
    const unsigned int msgDepth = fifoDepth * (512 / 32 / CH_NM);
// clang-format off
#pragma HLS INTERFACE m_axi offset = slave latency = 64 \
	num_write_outstanding = 16 num_read_outstanding = 16 \
	max_write_burst_length = 64 max_read_burst_length = 64 \
	bundle = gmem0_0 port = inputData

#pragma HLS INTERFACE m_axi offset = slave latency = 64 \
	num_write_outstanding = 16 num_read_outstanding = 16 \
	max_write_burst_length = 64 max_read_burst_length = 64 \
	bundle = gmem0_1 port = outputData
// clang-format on

#pragma HLS INTERFACE s_axilite port = inputData bundle = control
#pragma HLS INTERFACE s_axilite port = outputData bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

    hls::stream<ap_uint<512> > textInStrm;
#pragma HLS stream variable = textInStrm depth = fifoDepth
#pragma HLS resource variable = textInStrm core = FIFO_BRAM
    hls::stream<ap_uint<64> > textLengthStrm;
#pragma HLS stream variable = textLengthStrm depth = fifobatch
#pragma HLS resource variable = textLengthStrm core = FIFO_LUTRAM
    hls::stream<ap_uint<64> > textNumStrm;
#pragma HLS stream variable = textNumStrm depth = fifobatch
#pragma HLS resource variable = textNumStrm core = FIFO_LUTRAM

    hls::stream<ap_uint<32> > msgStrm[_channelNumber];
#pragma HLS stream variable = msgStrm depth = msgDepth
#pragma HLS resource variable = msgStrm core = FIFO_BRAM
    hls::stream<ap_uint<64> > msgLenStrm[_channelNumber];
#pragma HLS stream variable = msgLenStrm depth = 128
#pragma HLS resource variable = msgLenStrm core = FIFO_BRAM
    hls::stream<bool> eMsgLenStrm[_channelNumber];
#pragma HLS stream variable = eMsgLenStrm depth = 128
#pragma HLS resource variable = eMsgLenStrm core = FIFO_LUTRAM

    hls::stream<ap_uint<160> > hshStrm[_channelNumber];
#pragma HLS stream variable = hshStrm depth = fifobatch
#pragma HLS resource variable = hshStrm core = FIFO_LUTRAM
    hls::stream<bool> eHshStrm[_channelNumber];
#pragma HLS stream variable = eHshStrm depth = fifobatch
#pragma HLS resource variable = eHshStrm core = FIFO_LUTRAM

    // TODO: Extract target hash from feeder
    readIn<_burstLength, _channelNumber>(inputData, textInStrm, textLengthStrm, textNumStrm);
    splitInput<_channelNumber, _burstLength>(textInStrm, textLengthStrm, textNumStrm, msgStrm, msgLenStrm, eMsgLenStrm);

    for (int i = 0; i < _channelNumber; i++) {
        ap_uint<32> message1 = msgStrm[i].read();
        ap_uint<32> message2 = msgStrm[i].read();
        if (message1 == message2) {
        	outputData[0].range(31, 0) = message1;
        }
    }
    //xf::security::sha1<msgWidth>(msgStrm, msgLenStrm, eMsgLenStrm, hshStrm, eHshStrm);

    //bool isFinish = eHshStrm.read();
    //while(!isFinish) {
    //	ap_uint<128> hash = hshStrm.read();
    //	int firstByte = hash.range(7, 0);
    //	int secondByte = hash.range(15, 8);
    //	hls::print("Hash: %d ", (int)firstByte);
    //	hls::print("%d\n", (int)secondByte);

    //	isFinish = eHshStrm.read();
    //}
}
