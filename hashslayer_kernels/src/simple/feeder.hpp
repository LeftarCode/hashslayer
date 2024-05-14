#pragma once
#include <ap_int.h>
#include <hls_stream.h>
#include <hls_print.h>

template <unsigned int burstLength, unsigned int passwordLength, unsigned int hashWidth>
static void readIn(ap_uint<512>* ptr,
					hls::stream<ap_uint<hashWidth>>& targetHshStrm,
                   hls::stream<ap_uint<512> >& textInStrm,
                   hls::stream<ap_uint<64> >& textLengthStrm,
                   hls::stream<ap_uint<64> >& textNumStrm) {
	ap_uint<512> configBlock = ptr[0];
	ap_uint<512> targetHash = ptr[1];
	ap_uint<64> textNum = configBlock.range(511, 448);

    textLengthStrm.write(passwordLength);
    textNumStrm.write(textNum);

    ap_uint<64> totalAxiBlock = textNum * passwordLength / 64;

LOOP_READ_DATA:
    for (ap_uint<64> i = 0; i < totalAxiBlock; i += burstLength) {
        ap_uint<16> readLen, nextStop;

        nextStop = i + burstLength;
        if (nextStop < totalAxiBlock) {
            readLen = burstLength;
        } else {
            readLen = totalAxiBlock - i;
        }

        for (ap_uint<16> j = 0; j < readLen; j++) {
#pragma HLS pipeline II = 1
            ap_uint<512> axiBlock = ptr[i + j + 1];
            textInStrm.write(axiBlock);
        }
    }
}

template <unsigned int passwordLength, unsigned int messageWidth>
static void splitInput(hls::stream<ap_uint<512>>& textInStrm,
                hls::stream<ap_uint<64>>& textLengthStrm,
                hls::stream<ap_uint<64>>& textNumStrm,
                hls::stream<ap_uint<messageWidth>>& msgStrm,
                hls::stream<ap_uint<64>>& msgLenStrm,
                hls::stream<bool>& eMsgLenStrm) {

    ap_uint<64> textLength = textLengthStrm.read();
    ap_uint<64> textLengthInGrpSize = textLength / 4;
    ap_uint<64> textNum = textNumStrm.read();
    ap_uint<64> totalAxiBlock = textNum * passwordLength / 64;

    for (ap_uint<64> i = 0; i < totalAxiBlock; i++) {
        ap_uint<512> axiWord = textInStrm.read();
        int offset = 0;
    	for (ap_uint<64> j = 0; j < 64/passwordLength; j++) {
        	eMsgLenStrm.write(false);
        	msgLenStrm.write(textLength);

        	for (ap_uint<64> k = 0; k < textLength/(messageWidth/8); k++) {
        		msgStrm.write(axiWord(offset + messageWidth - 1, offset));
        		offset += messageWidth;
        	}
    	}
    }
    eMsgLenStrm.write(true);
}

template <unsigned int burstLength, unsigned int passwordLength, unsigned int messageWidth, unsigned int hashWidth>
void simpleFeeder(ap_uint<512>* inputData,
		hls::stream<ap_uint<hashWidth>>& targetHshStrm,
        hls::stream<ap_uint<messageWidth>>& msgStrm,
        hls::stream<ap_uint<64>>& msgLenStrm,
        hls::stream<bool>& eMsgLenStrm) {
    const unsigned int fifoBatch = 4;
    const unsigned int fifoDepth = burstLength * fifoBatch;

    hls::stream<ap_uint<512>> textInStrm;
#pragma HLS stream variable = textInStrm depth = fifoDepth
#pragma HLS resource variable = textInStrm core = FIFO_BRAM
    hls::stream<ap_uint<64>> textLengthStrm;
#pragma HLS stream variable = textLengthStrm depth = fifoBatch
#pragma HLS resource variable = textLengthStrm core = FIFO_LUTRAM
    hls::stream<ap_uint<64>> textNumStrm;
#pragma HLS stream variable = textNumStrm depth = fifoBatch
#pragma HLS resource variable = textNumStrm core = FIFO_LUTRAM

    readIn<burstLength, passwordLength, hashWidth>(inputData, targetHshStrm, textInStrm, textLengthStrm, textNumStrm);
    splitInput<passwordLength, messageWidth>(textInStrm, textLengthStrm, textNumStrm, msgStrm, msgLenStrm, eMsgLenStrm);
}
