#include <ap_int.h>
#include <hls_stream.h>

#include "config.hpp"
#include "xf_security/sha512_t.hpp"
#include "xf_security/hmac.hpp"

static void test_sha512(hls::stream<ap_uint<64> >& msgStrm,
                           hls::stream<ap_uint<128> >& lenStrm,
                           hls::stream<bool>& eLenStrm,
                           hls::stream<ap_uint<512> >& hshStrm,
                           hls::stream<bool>& eHshStrm) {
    xf::security::sha512<64>(msgStrm, lenStrm, eLenStrm, hshStrm, eHshStrm);
}

template <unsigned int _burstLength, unsigned int _channelNumber>
static void readIn(ap_uint<512>* ptr,
                   hls::stream<ap_uint<512> >& textInStrm,
                   hls::stream<ap_uint<128> >& textLengthStrm,
                   hls::stream<ap_uint<64> >& textNumStrm) {
    // number of message blocks in Byte
    ap_uint<128> textLength;
    // number of messages for single PU
    ap_uint<64> textNum;

// scan for configurations, _channelNumber in total
// actually the same textLength, msgNum
// key is also the same to simplify input table
// and key is treated as different when process next message
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
void splitText(hls::stream<ap_uint<512> >& textStrm, hls::stream<ap_uint<64> > msgStrm[_channelNumber]) {
#pragma HLS inline off
    ap_uint<512> axiWord = textStrm.read();
    for (unsigned int i = 0; i < _channelNumber; i++) {
#pragma HLS unroll
        for (unsigned int j = 0; j < (512 / _channelNumber / 64); j++) {
#pragma HLS pipeline II = 1
            msgStrm[i].write(axiWord(i * GRP_WIDTH + j * 64 + 63, i * GRP_WIDTH + j * 64));
        }
    }
}

template <unsigned int _channelNumber, unsigned int _burstLength>
void splitInput(hls::stream<ap_uint<512> >& textInStrm,
                hls::stream<ap_uint<128> >& textLengthStrm,
                hls::stream<ap_uint<64> >& textNumStrm,
                hls::stream<ap_uint<64> > msgStrm[_channelNumber],
                hls::stream<ap_uint<128> > msgLenStrm[_channelNumber],
                hls::stream<bool> eMsgLenStrm[_channelNumber]) {
    // number of message blocks in 128 bits
    ap_uint<128> textLength = textLengthStrm.read();
    // transform to message length in 32bits
    ap_uint<64> textLengthInGrpSize = textLength / GRP_SIZE;
    // number of messages for single PU
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

template <unsigned int _channelNumber>
static void sha512Parallel(hls::stream<ap_uint<64> > msgStrm[_channelNumber],
                             hls::stream<ap_uint<128> > msgLenStrm[_channelNumber],
                             hls::stream<bool> eMsgLenStrm[_channelNumber],
                             hls::stream<ap_uint<512> > hshStrm[_channelNumber],
                             hls::stream<bool> eHshStrm[_channelNumber]) {
#pragma HLS dataflow
    for (int i = 0; i < _channelNumber; i++) {
#pragma HLS unroll
        test_sha512(msgStrm[i], msgLenStrm[i], eMsgLenStrm[i], hshStrm[i], eHshStrm[i]);
    }
}

template <unsigned int _channelNumber, unsigned int _burstLen>
static void mergeResult(hls::stream<ap_uint<512> > hshStrm[_channelNumber],
                        hls::stream<bool> eHshStrm[_channelNumber],
                        hls::stream<ap_uint<512> >& outStrm,
                        hls::stream<unsigned int>& burstLenStrm) {
    ap_uint<_channelNumber> unfinish;
    for (int i = 0; i < _channelNumber; i++) {
#pragma HLS unroll
        unfinish[i] = 1;
    }

    unsigned int counter = 0;

LOOP_WHILE:
    while (unfinish != 0) {
    LOOP_CHANNEL:
        for (int i = 0; i < _channelNumber; i++) {
#pragma HLS pipeline II = 1
            bool e = eHshStrm[i].read();
            if (!e) {
                ap_uint<512> hsh = hshStrm[i].read();
                ap_uint<512> tmp = 0;
                tmp.range(511, 0) = hsh.range(511, 0);
                outStrm.write(tmp);
                counter++;
                if (counter == _burstLen) {
                    counter = 0;
                    burstLenStrm.write(_burstLen);
                }
            } else {
                unfinish[i] = 0;
            }
        }
    }
    if (counter != 0) {
        burstLenStrm.write(counter);
    }
    burstLenStrm.write(0);
}

template <unsigned int _burstLength, unsigned int _channelNumber>
static void writeOut(hls::stream<ap_uint<512> >& outStrm, hls::stream<unsigned int>& burstLenStrm, ap_uint<512>* ptr) {
    unsigned int burstLen = burstLenStrm.read();
    unsigned int counter = 0;
    while (burstLen != 0) {
        for (unsigned int i = 0; i < burstLen; i++) {
#pragma HLS pipeline II = 1
            ptr[counter] = outStrm.read();
            counter++;
        }
        burstLen = burstLenStrm.read();
    }
}
// @brief top of kernel
extern "C" void sha512Kernel(ap_uint<512> inputData[(1 << 20) + 100], ap_uint<512> outputData[1 << 20]) {
#pragma HLS dataflow

    const unsigned int fifobatch = 4;
    const unsigned int _channelNumber = CH_NM;
    const unsigned int _burstLength = BURST_LEN;
    const unsigned int fifoDepth = _burstLength * fifobatch;
    const unsigned int msgDepth = fifoDepth * (512 / 64 / CH_NM);

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
    hls::stream<ap_uint<128> > textLengthStrm;
#pragma HLS stream variable = textLengthStrm depth = fifobatch
#pragma HLS resource variable = textLengthStrm core = FIFO_LUTRAM
    hls::stream<ap_uint<64> > textNumStrm;
#pragma HLS stream variable = textNumStrm depth = fifobatch
#pragma HLS resource variable = textNumStrm core = FIFO_LUTRAM

    hls::stream<ap_uint<64> > msgStrm[_channelNumber];
#pragma HLS stream variable = msgStrm depth = msgDepth
#pragma HLS resource variable = msgStrm core = FIFO_BRAM
    hls::stream<ap_uint<128> > msgLenStrm[_channelNumber];
#pragma HLS stream variable = msgLenStrm depth = 128
#pragma HLS resource variable = msgLenStrm core = FIFO_BRAM
    hls::stream<bool> eMsgLenStrm[_channelNumber];
#pragma HLS stream variable = eMsgLenStrm depth = 128
#pragma HLS resource variable = eMsgLenStrm core = FIFO_LUTRAM

    hls::stream<ap_uint<512> > hshStrm[_channelNumber];
#pragma HLS stream variable = hshStrm depth = fifobatch
#pragma HLS resource variable = hshStrm core = FIFO_LUTRAM
    hls::stream<bool> eHshStrm[_channelNumber];
#pragma HLS stream variable = eHshStrm depth = fifobatch
#pragma HLS resource variable = eHshStrm core = FIFO_LUTRAM

    hls::stream<ap_uint<512> > outStrm;
#pragma HLS stream variable = outStrm depth = fifoDepth
#pragma HLS resource variable = outStrm core = FIFO_BRAM
    hls::stream<unsigned int> burstLenStrm;
#pragma HLS stream variable = burstLenStrm depth = fifobatch
#pragma HLS resource variable = burstLenStrm core = FIFO_LUTRAM

    readIn<_burstLength, _channelNumber>(inputData, textInStrm, textLengthStrm, textNumStrm);

    splitInput<_channelNumber, _burstLength>(textInStrm, textLengthStrm, textNumStrm, msgStrm,
                                             msgLenStrm, eMsgLenStrm);

    sha512Parallel<_channelNumber>(msgStrm, msgLenStrm, eMsgLenStrm, hshStrm, eHshStrm);

    mergeResult<_channelNumber, _burstLength>(hshStrm, eHshStrm, outStrm, burstLenStrm);

    writeOut<_burstLength, _channelNumber>(outStrm, burstLenStrm, outputData);
}
