#include "pch.h"
#include <span>
#include <stdio.h>
#include <opus/opus.h>
#include <opus/opus_custom.h>
#include <cassert>
#ifndef IVOICECODEC_H
#define IVOICECODEC_H
#pragma once

#define BYTES_PER_SAMPLE    2


// This interface is for voice codecs to implement.

// Codecs are guaranteed to be called with the exact output from Compress into Decompress (ie:
// data won't be stuck together and sent to Decompress).

// Decompress is not guaranteed to be called in any specific order relative to Compress, but 
// Codecs maintain state between calls, so it is best to call Compress with consecutive voice data
// and decompress likewise. If you call it out of order, it will sound wierd. 

// In the same vein, calling Decompress twice with the same data is a bad idea since the state will be
// expecting the next block of data, not the same block.

class IVoiceCodec
{
protected:
	virtual            ~IVoiceCodec() {}

public:
	// Initialize the object. The uncompressed format is always 8-bit signed mono.
	virtual bool    Init(int quality, unsigned int nSamplesPerSec) = 0;

	// Use this to delete the object.
	virtual void    Release() = 0;


	// Compress the voice data.
	// pUncompressed        -    16-bit signed mono voice data.
	// maxCompressedBytes    -    The length of the pCompressed buffer. Don't exceed this.
	// bFinal                -    Set to true on the last call to Compress (the user stopped talking).
	//                            Some codecs like big block sizes and will hang onto data you give them in Compress calls.
	//                            When you call with bFinal, the codec will give you compressed data no matter what.
	// Return the number of bytes you filled into pCompressed.
	virtual int        Compress(const char* pUncompressed, int nSamples, char* pCompressed, int maxCompressedBytes/*, bool bFinal*/) = 0;

	// Decompress voice data. pUncompressed is 16-bit signed mono.
	virtual int        Decompress(const char* pCompressed, int compressedBytes, char* pUncompressed, int maxUncompressedBytes) = 0;

	// Some codecs maintain state between Compress and Decompress calls. This should clear that state.
	virtual bool    ResetState() = 0;
};


#endif // IVOICECODEC_H
class IFrameEncoder
{
public:
	virtual			~IFrameEncoder() {}

	// This is called by VoiceCodec_Frame to see if it can initialize..
	// Fills in the uncompressed and encoded frame size (both are in BYTES).
	virtual bool	Init(int quality, int samplerate, int& rawFrameSize, int& encodedFrameSize) = 0;

	virtual void	Release() = 0;

	// pUncompressed is 8-bit signed mono sound data with 'rawFrameSize' bytes.
	// pCompressed is the size of encodedFrameSize.
	virtual void EncodeFrame(const char* pUncompressedBytes, char* pCompressed) = 0;

	// pCompressed is encodedFrameSize.
	// pDecompressed is where the 8-bit signed mono samples are stored and has space for 'rawFrameSize' bytes.
	virtual void DecodeFrame(const char* pCompressed, char* pDecompressedBytes) = 0;

	// Some codecs maintain state between Compress and Decompress calls. This should clear that state.
	virtual bool	ResetState() = 0;
};



#define CHANNELS 1

struct opus_options
{
	int iSampleRate;
	int iRawFrameSize;
	int iPacketSize;
};
int g_iSampleRate = 0;
opus_options g_OpusOpts[] =
{
        {48000, 256, 120},
        {48000, 120, 60},
        {48000, 256, 60},
        {48000, 256, 120},
};

class VoiceEncoder_Opus : public IFrameEncoder
{
public:
	VoiceEncoder_Opus();
	virtual ~VoiceEncoder_Opus();

	// Fix Init to match interface
	bool Init(int quality, int samplerate, int& rawFrameSize, int& encodedFrameSize) override;
	void Release() override;

	// Fix Encode/Decode to match interface
	void EncodeFrame(const char* pUncompressedBytes, char* pCompressed) override;
	void DecodeFrame(const char* pCompressed, char* pDecompressedBytes) override;
	bool ResetState() override;
	void PreprocessFrame(int16_t* samples, int numSamples, int sampleRate)
	{
		float smoothingCoeff = 1.0f - exp(-1.0f / (sampleRate * RELEASE_TIME));

		for (int i = 0; i < numSamples; i++)
		{
			// Get sample amplitude
			float amplitude = std::abs(samples[i]);

			// Calculate target gain (0 or 1)
			float targetGain = (amplitude > NOISE_THRESHOLD) ? 1.0f : 0.0f;

			// Smooth the gain changes to avoid artifacts
			m_smoothedGain += (targetGain - m_smoothedGain) * smoothingCoeff;

			// Apply gain
			samples[i] = static_cast<int16_t>(samples[i] * m_smoothedGain);
		}
	}

private:
	bool InitStates();
	void TermStates();
	static constexpr float NOISE_THRESHOLD = 2000.0f;  // Adjust based on your input levels
	static constexpr float RELEASE_TIME = 0.1f;  // Seconds
	float m_smoothedGain = 1.0f;

	OpusCustomEncoder* m_EncoderState;
	OpusCustomDecoder* m_DecoderState;
	OpusCustomMode* m_Mode;
	int m_iVersion;
};


// VoiceCodec_Frame can be used to wrap a frame encoder for the engine. As it gets sound data, it will queue it
// until it has enough for a frame, then it will compress it. Same thing for decompression.
class VoiceCodec_Frame : public IVoiceCodec
{
public:
	enum { MAX_FRAMEBUFFER_SAMPLES = 1024 };

	VoiceCodec_Frame(IFrameEncoder* pEncoder)
	{
		m_nEncodeBufferSamples = 0;
		m_nRawBytes = m_nRawSamples = m_nEncodedBytes = 0;
		m_pFrameEncoder = pEncoder;
	}

	virtual	~VoiceCodec_Frame()
	{
		if (m_pFrameEncoder)
			m_pFrameEncoder->Release();
	}

	virtual bool Init(int quality, unsigned int nSamplesPerSec) override
	{
		if (m_pFrameEncoder && m_pFrameEncoder->Init(quality, nSamplesPerSec, m_nRawBytes, m_nEncodedBytes))
		{
			m_nRawSamples = m_nRawBytes / BYTES_PER_SAMPLE;
			assert(m_nRawBytes <= MAX_FRAMEBUFFER_SAMPLES && m_nEncodedBytes <= MAX_FRAMEBUFFER_SAMPLES);
			return true;
		}
		else
		{
			if (m_pFrameEncoder)
				m_pFrameEncoder->Release();

			m_pFrameEncoder = NULL;
			return false;
		}
	}

	virtual void	Release() override
	{
		delete this;
	}

	virtual int	Compress(const char* pUncompressedBytes, int nSamples, char* pCompressed, int maxCompressedBytes) override
	{
		if (!m_pFrameEncoder)
			return 0;
		if (m_pFrameEncoder == nullptr)
			return 0;

		const int16_t* pUncompressed = (const int16_t*)pUncompressedBytes;

		int nCompressedBytes = 0;
		while ((nSamples + m_nEncodeBufferSamples) >= m_nRawSamples && (maxCompressedBytes - nCompressedBytes) >= m_nEncodedBytes)
		{
			// Get the data block out.
			int16_t samples[MAX_FRAMEBUFFER_SAMPLES];
			memcpy(samples, m_EncodeBuffer, m_nEncodeBufferSamples * BYTES_PER_SAMPLE);
			memcpy(&samples[m_nEncodeBufferSamples], pUncompressed, (m_nRawSamples - m_nEncodeBufferSamples) * BYTES_PER_SAMPLE);
			nSamples -= m_nRawSamples - m_nEncodeBufferSamples;
			pUncompressed += m_nRawSamples - m_nEncodeBufferSamples;
			m_nEncodeBufferSamples = 0;

			// Compress it.
			m_pFrameEncoder->EncodeFrame((const char*)samples, &pCompressed[nCompressedBytes]);
			nCompressedBytes += m_nEncodedBytes;
		}

		// Store the remaining samples.
		int nNewSamples = min(nSamples, min(m_nRawSamples - m_nEncodeBufferSamples, m_nRawSamples));
		if (nNewSamples) {
			memcpy(&m_EncodeBuffer[m_nEncodeBufferSamples], &pUncompressed[nSamples - nNewSamples], nNewSamples * BYTES_PER_SAMPLE);
			m_nEncodeBufferSamples += nNewSamples;
		}

	//	printf("VoiceCodec_Frame[VoiceEncoder_Opus]::Compress nCompressedBytes:%i nSamples:%i\n",
		//	nCompressedBytes, nSamples);
		
		return nCompressedBytes;
	}
	virtual int Decompress(const char* pCompressed, int compressedBytes, char* pUncompressed, int maxUncompressedBytes) override
	{
		// Basic validation
		if (!m_pFrameEncoder || !pCompressed || !pUncompressed) {
			return 0;
		}

		// Validate minimum buffer sizes
		if (compressedBytes < m_nEncodedBytes || maxUncompressedBytes < m_nRawBytes) {
			return 0;
		}

		int nDecompressedBytes = 0;
		int curCompressedByte = 0;

		while (curCompressedByte + m_nEncodedBytes <= compressedBytes &&
			nDecompressedBytes + m_nRawBytes <= maxUncompressedBytes)
		{
			m_pFrameEncoder->DecodeFrame(
				&pCompressed[curCompressedByte],
				&pUncompressed[nDecompressedBytes]
			);

			curCompressedByte += m_nEncodedBytes;
			nDecompressedBytes += m_nRawBytes;
		}

		return nDecompressedBytes / BYTES_PER_SAMPLE;
	}
	virtual bool ResetState() override
	{
		if (m_pFrameEncoder)
			return m_pFrameEncoder->ResetState();
		else
			return false;
	}


public:
	// The codec encodes and decodes samples in fixed-size blocks, so we queue up uncompressed and decompressed data 
	// until we have blocks large enough to give to the codec.
	short				m_EncodeBuffer[MAX_FRAMEBUFFER_SAMPLES];
	int					m_nEncodeBufferSamples;

	IFrameEncoder* m_pFrameEncoder;
	int					m_nRawBytes, m_nRawSamples;
	int					m_nEncodedBytes;
};


IVoiceCodec* CreateVoiceCodec_Frame(IFrameEncoder* pEncoder)
{
	return new VoiceCodec_Frame(pEncoder);
}


class VoiceCodec_Uncompressed : public IVoiceCodec
{
public:
	VoiceCodec_Uncompressed() {}
	virtual ~VoiceCodec_Uncompressed() {}
	virtual bool Init(int quality, unsigned int nSamplesPerSec) { return true; }
	virtual void Release() { delete this; }
	virtual bool ResetState() { return true; }
	virtual int Compress(const char* pUncompressed, int nSamples, char* pCompressed, int maxCompressedBytes/*, bool bFinal*/)
	{
		int nCompressedBytes = nSamples * BYTES_PER_SAMPLE;
		memcpy_s(pCompressed, maxCompressedBytes, pUncompressed, nCompressedBytes);
		return nCompressedBytes;
	}
	virtual int Decompress(const char* pCompressed, int compressedBytes, char* pUncompressed, int maxUncompressedBytes)
	{
		int nDecompressedBytes = compressedBytes;
		memcpy_s(pUncompressed, maxUncompressedBytes, pCompressed, compressedBytes);
		return nDecompressedBytes / BYTES_PER_SAMPLE;
	}
};


extern "C" __declspec(dllexport) void* CreateInterface(const char* pName, int* pReturnCode)
{
	//return new VoiceCodec_Uncompressed;
	assert(strcmp(pName, "vaudio_speex") == 0);

	IFrameEncoder* pEncoder = new VoiceEncoder_Opus;
	return CreateVoiceCodec_Frame(pEncoder);

	//return new VoiceCodec_Uncompressed;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

VoiceEncoder_Opus::VoiceEncoder_Opus()
{
	m_EncoderState = NULL;
	m_DecoderState = NULL;
	m_Mode = NULL;
	m_iVersion = 0;
}

VoiceEncoder_Opus::~VoiceEncoder_Opus()
{
	TermStates();
}



void VoiceEncoder_Opus::Release()
{
	delete this;
}
bool VoiceEncoder_Opus::Init(int quality, int samplerate, int& rawFrameSize, int& encodedFrameSize)
{
	m_iVersion = quality;

	printf("VoiceEncoder_Opus::Init quality:%i samplerate param:%i, struct rate: %i\n", quality, samplerate, samplerate * 2);
	g_iSampleRate = samplerate * 2;
	rawFrameSize = g_OpusOpts[m_iVersion].iRawFrameSize * BYTES_PER_SAMPLE;

	int iError = 0;
	m_Mode = opus_custom_mode_create(samplerate*2, g_OpusOpts[m_iVersion].iRawFrameSize, &iError);

	if (iError != 0) {
		printf("opus_custom_mode_create failed: %d samplerate: %d, opus opts: %d\n", iError, samplerate, g_OpusOpts[m_iVersion].iRawFrameSize);
		return false;
	}

	m_EncoderState = opus_custom_encoder_create(m_Mode, CHANNELS, &iError);
	if( iError != 0 )
	{
		printf("opus_custom_encoder_create failed: %d\n", iError);
		return false;
	}
	m_DecoderState = opus_custom_decoder_create(m_Mode, CHANNELS, &iError);
	if( iError != 0 )
	{
		printf("opus_custom_decoder_create failed: %d\n", iError);
		return false;
	}

	if (!InitStates()) {
		printf("InitStates failed\n");
		return false;
	}

	encodedFrameSize = g_OpusOpts[m_iVersion].iPacketSize;
	return true;
}

void VoiceEncoder_Opus::EncodeFrame(const char* pUncompressedBytes, char* pCompressed)
{
	// Create a buffer for preprocessing
	int16_t* samples = (int16_t*)pUncompressedBytes;

	// Apply preprocessing
	PreprocessFrame(samples, g_OpusOpts[m_iVersion].iRawFrameSize, g_iSampleRate);  // Assuming 48kHz

	unsigned char output[1024];

	opus_custom_encode(m_EncoderState, (opus_int16*)samples, g_OpusOpts[m_iVersion].iRawFrameSize, output, g_OpusOpts[m_iVersion].iPacketSize);

	for (int i = 0; i < g_OpusOpts[m_iVersion].iPacketSize; i++)
	{
		*pCompressed = (char)output[i];
		pCompressed++;
	}
}


// In VoiceEncoder_Opus::DecodeFrame
void VoiceEncoder_Opus::DecodeFrame(const char* pCompressed, char* pDecompressedBytes)
{
	// Validate inputs
	if (!pCompressed || !pDecompressedBytes || !m_DecoderState) {
		return;
	}

	if (m_iVersion >= sizeof(g_OpusOpts) / sizeof(g_OpusOpts[0])) {
		return;
	}

	const int packetSize = g_OpusOpts[m_iVersion].iPacketSize;
	const int frameSize = g_OpusOpts[m_iVersion].iRawFrameSize;

	// Use stack buffer with size checking
	constexpr int MAX_PACKET_SIZE = 1024;
	if (packetSize > MAX_PACKET_SIZE) {
		return;
	}

	unsigned char output[MAX_PACKET_SIZE];
	const char* in = pCompressed;

	// Convert input bytes to unsigned
	for (int i = 0; i < packetSize; i++) {
		output[i] = static_cast<unsigned char>(in[i]);
	}

	// Decode the frame
	int result = opus_custom_decode(
		m_DecoderState,
		output,
		packetSize,
		reinterpret_cast<opus_int16*>(pDecompressedBytes),
		frameSize
	);

	// Check for decode errors
	if (result < 0) {
		// Handle error - could log or set an error state
		// For now, we'll zero out the output
		memset(pDecompressedBytes, 0, frameSize * BYTES_PER_SAMPLE);
	}
}

bool VoiceEncoder_Opus::ResetState()
{
	opus_custom_encoder_ctl(m_EncoderState, OPUS_RESET_STATE );
	opus_custom_decoder_ctl(m_DecoderState, OPUS_RESET_STATE );

	return true;
}

bool VoiceEncoder_Opus::InitStates()
{
	if ( !m_EncoderState || !m_DecoderState )
		return false;
	// Add these controls:
	opus_custom_encoder_ctl(m_EncoderState, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	opus_custom_encoder_ctl(m_EncoderState, OPUS_SET_VBR(1));
	opus_custom_encoder_ctl(m_EncoderState, OPUS_SET_COMPLEXITY(10)); // Maximum complexity
	opus_custom_encoder_ctl(m_EncoderState, OPUS_SET_DTX(1));      // Enable discontinuous transmission


	opus_custom_encoder_ctl(m_EncoderState, OPUS_RESET_STATE );
	opus_custom_decoder_ctl(m_DecoderState, OPUS_RESET_STATE );

	return true;
}

void VoiceEncoder_Opus::TermStates()
{
	if( m_EncoderState )
	{
		opus_custom_encoder_destroy( m_EncoderState );
		m_EncoderState = NULL;
	}

	if( m_DecoderState )
	{
		opus_custom_decoder_destroy( m_DecoderState );
		m_DecoderState = NULL;
	}

	opus_custom_mode_destroy( m_Mode );
}
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	return TRUE;
}
