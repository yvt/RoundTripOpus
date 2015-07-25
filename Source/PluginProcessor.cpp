/*
  ==============================================================================

		RoundTripOpus
	 
		Copyright 2015 yvt
	 
	 This file is part of RoundTripOpus.
	 
	 Foobar is free software: you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation, either version 3 of the License, or
	 (at your option) any later version.
	 
	 Foobar is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	 GNU General Public License for more details.
	 
	 You should have received a copy of the GNU General Public License
	 along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <array>
#include <algorithm>
#include <cassert>

template <class T>
static inline void copyWithStride(T *dest, const T *src, std::size_t count,
								  std::size_t destStride, std::size_t srcStride)
{
	while (count--) {
		*dest = *src;
		dest += destStride;
		src += srcStride;
	}
}

template <class T, int N>
class RoundTripOpusAudioProcessor::Fifo
{
	std::vector<T> buffers[N];
	std::size_t capacity;
	std::size_t readCursor;
	std::size_t size;
	
	std::size_t wrapAround(std::size_t i)
	{
		while (i >= capacity)
			i -= capacity;
		assert(i < capacity);
		return i;
	}
	
	std::size_t getReadCursor()
	{
		assert(readCursor < capacity);
		return readCursor;
	}
	
	std::size_t getWriteCursor()
	{
		return wrapAround(readCursor + size);
	}
	
public:
	using BufferSet = std::array<T *, N>;
	using ConstBufferSet = std::array<const T *, N>;
	
	void setCapacity(std::size_t samples)
	{
		capacity = samples;
		for (auto &buffer: buffers)
			buffer.resize(samples);
		readCursor = 0;
		size = 0;
	}
	Fifo(std::size_t capacity)
	{
		setCapacity(capacity);
	}
	bool canDequeueAtLeast(std::size_t samples)
	{
		return size >= samples;
	}
	bool canEnqueueAtLeast(std::size_t samples)
	{
		return samples + size <= capacity;
	}
	std::size_t getNumberOfSamplesEnqueueable()
	{
		return capacity - size;
	}
	std::size_t getNumberOfSamplesDequeueable()
	{
		return size;
	}
	std::size_t getNumberOfSamplesEnqueueableBySingleRun()
	{
		if (size == capacity) {
			return 0;
		} else {
			auto rc = getReadCursor();
			auto wc = getWriteCursor();
			if (wc >= rc) {
				return capacity - wc;
			} else {
				return rc - wc;
			}
		}
	}
	std::size_t getNumberOfSamplesDequeueableBySingleRun()
	{
		if (size == 0) {
			return 0;
		} else {
			auto rc = getReadCursor();
			auto wc = getWriteCursor();
			if (rc >= wc) {
				return capacity - rc;
			} else {
				return wc - rc;
			}
		}
	}
	template <class F>
	std::size_t dequeueSingleCustom(F fn)
	{
		ConstBufferSet bufs;
		auto cursor = getReadCursor();
		for (std::size_t i = 0; i < N; ++i) {
			bufs[i] = buffers[i].data() + cursor;
		}
		
		auto runLength = getNumberOfSamplesDequeueableBySingleRun();
		auto adv = fn(bufs, runLength);
		assert(adv <= runLength);
		
		readCursor = wrapAround(readCursor + adv);
		size -= adv;
		
		return adv;
	}
	template <class F>
	std::size_t enqueueSingleCustom(F fn)
	{
		BufferSet bufs;
		auto cursor = getWriteCursor();
		for (std::size_t i = 0; i < N; ++i) {
			bufs[i] = buffers[i].data() + cursor;
		}
		
		auto runLength = getNumberOfSamplesEnqueueableBySingleRun();
		auto adv = fn(bufs, runLength);
		assert(adv <= runLength);
		
		size += adv;
		
		return adv;
	}
	std::size_t dequeue(BufferSet buffers, std::size_t size, std::size_t stride = 1)
	{
		std::size_t ttl = 0;
		while (size > 0) {
			auto processed =
			dequeueSingleCustom([&](ConstBufferSet qb, std::size_t samples) {
				if (samples > size) {
					samples = size;
				}
				for (std::size_t i = 0; i < N; ++i) {
					if (buffers[i]) {
						copyWithStride(buffers[i], qb[i], samples,
									   stride, 1);
					}
				}
				return samples;
			});
			if (processed == 0) {
				break;
			}
			size -= processed;
			for (std::size_t i = 0; i < N; ++i) {
				if (buffers[i])
					buffers[i] += processed * stride;
			}
			ttl += processed;
		}
		return ttl;
	}
	std::size_t enqueue(ConstBufferSet buffers, std::size_t size, std::size_t stride = 1)
	{
		std::size_t ttl = 0;
		while (size > 0) {
			auto processed =
			enqueueSingleCustom([&](BufferSet qb, std::size_t samples) {
				if (samples > size) {
					samples = size;
				}
				for (std::size_t i = 0; i < N; ++i) {
					if (buffers[i]) {
						copyWithStride(qb[i], buffers[i], samples,
									   1, stride);
					}
				}
				return samples;
			});
			if (processed == 0) {
				break;
			}
			size -= processed;
			for (std::size_t i = 0; i < N; ++i) {
				if (buffers[i])
					buffers[i] += processed * stride;
			}
			ttl += processed;
		}
		return ttl;
	}
	
};

//==============================================================================
RoundTripOpusAudioProcessor::RoundTripOpusAudioProcessor()
{
	opusEncoder = nullptr;
	opusDecoder = nullptr;
	for (auto &s: srcState)
		s = nullptr;
	
	resolvingUnderrun = false;
	resolvingOverrun = false;
	
	fifo1.reset(new AudioFifo(4096));
	fifo2.reset(new AudioFifo(16384));
	fifo3.reset(new AudioFifo(16384));
	fifo4.reset(new AudioFifo(4096));
	
	opusSamplingRate = 48000;
	opusNumChannels = 2;
	opusApplication = Application::Audio;
	opusFrameSizeTime = 400;
	opusBitRate = 64000;
	opusComplexity = 5;
	opusSignal = Signal::Auto;
	
	createOpusCodec();
}

RoundTripOpusAudioProcessor::~RoundTripOpusAudioProcessor()
{
	invalidateOpusCodec();
	invalidateSrc();
}

void RoundTripOpusAudioProcessor::createOpusCodec()
{
	if (!opusEncoder) {
		int err;
		int app;
		switch (opusApplication) {
			case Application::Audio:
				app = OPUS_APPLICATION_AUDIO;
				break;
			case Application::VoiceOverIP:
				app = OPUS_APPLICATION_VOIP;
				break;
			case Application::LowDelay:
				app = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
				break;
		}
		opusEncoder = opus_encoder_create
		(opusSamplingRate, opusNumChannels, app, &err);
	}
	if (!opusDecoder) {
		int err;
		opusDecoder = opus_decoder_create
		(opusSamplingRate, opusNumChannels, &err);
		// FIXME: error check
	}
}
void RoundTripOpusAudioProcessor::invalidateOpusCodec()
{
	if (opusEncoder)
		opus_encoder_destroy(opusEncoder);
	opusEncoder = nullptr;
	if (opusDecoder)
		opus_decoder_destroy(opusDecoder);
	opusDecoder = nullptr;
}

void RoundTripOpusAudioProcessor::invalidateSrc()
{
	for (auto &state: srcState)
		if (state)
			state = src_delete(state);
}

//==============================================================================
const String RoundTripOpusAudioProcessor::getName() const
{
    return JucePlugin_Name;
}



int RoundTripOpusAudioProcessor::getNumParameters()
{
    return 3; // hide last two
}

float RoundTripOpusAudioProcessor::getParameter (int index)
{
	switch ((Parameter)index) {
		case Parameter::SamplingRate:
			return opusSamplingRate / 48000.f;
		case Parameter::FrameSize:
			return opusFrameSizeTime / 600.f;
		case Parameter::Application:
			return (float)opusApplication / 2.f;
		case Parameter::Bitrate:
			return opusBitRate / 512000.f;
		case Parameter::Signal:
			return (float)opusSignal / 2.f;
	}
    return 0.0f;
}

void RoundTripOpusAudioProcessor::setParameter (int index, float newValue)
{
	std::lock_guard<std::mutex> lock(objLock);
	
	int rounded = roundFloatToInt(newValue);
	
	switch ((Parameter)index) {
		case Parameter::SamplingRate:
			// round
			rounded = roundFloatToInt(newValue * 48000.f);
			if (rounded < 10000) {
				rounded = 8000;
			} else if (rounded < 14000) {
				rounded = 12000;
			} else if (rounded < 20000) {
				rounded = 16000;
			} else if (rounded < 32000) {
				rounded = 24000;
			} else {
				rounded = 48000;
			}
			if (rounded != opusSamplingRate) {
				opusSamplingRate = rounded;
				invalidateOpusCodec();
			}
			break;
		case Parameter::Application:
			rounded = roundFloatToInt(newValue * 2.f);
			switch (rounded) {
				case (int)Application::Audio:
				case (int)Application::VoiceOverIP:
				case (int)Application::LowDelay:
					opusApplication = (Application)rounded;
					invalidateOpusCodec();
					break;
				default:
					// invalid value
					break;
			}
			break;
		case Parameter::Bitrate:
			rounded = roundFloatToInt(newValue * 512000.f);
			if (rounded < 600)
				rounded = 600;
			if (rounded > 512000)
				rounded = 512000;
			opusBitRate = rounded;
			break;
		case Parameter::Signal:
			rounded = roundFloatToInt(newValue * 2.f);
			switch (rounded) {
				case (int)Signal::Auto:
				case (int)Signal::Voice:
				case (int)Signal::Music:
					opusSignal = (Signal)rounded;
					break;
				default:
					// invalid value
					break;
			}
			break;
		case Parameter::FrameSize:
			newValue *= 60.f;
			if (newValue < 4.f) {
				rounded = 25;
			} else if (newValue < 7.5f) {
				rounded = 50;
			} else if (newValue < 15.f) {
				rounded = 100;
			} else if (newValue < 30.f) {
				rounded = 200;
			} else if (newValue < 40.f) {
				rounded = 400;
			} else {
				rounded = 600;
			}
			if (rounded != opusFrameSizeTime) {
				opusFrameSizeTime = rounded;
				invalidateOpusCodec();
			}
			break;
	}
}

const String RoundTripOpusAudioProcessor::getParameterName (int index)
{
	switch ((Parameter)index) {
		case Parameter::SamplingRate:
			return "Sampling Rate";
		case Parameter::FrameSize:
			return "Frame Size";
		case Parameter::Application:
			return "Application";
		case Parameter::Bitrate:
			return "Bit Rate";
		case Parameter::Signal:
			return "Signal";
	}
    return String();
}

const String RoundTripOpusAudioProcessor::getParameterText (int index)
{
	switch ((Parameter)index) {
		case Parameter::SamplingRate:
			return String::formatted("%d", opusSamplingRate);
		case Parameter::FrameSize:
			return String::formatted("%.2f", opusFrameSizeTime / 10.f);
		case Parameter::Application:
			switch (opusApplication) {
				case Application::Audio:
					return "Audio";
				case Application::LowDelay:
					return "Low Delay";
				case Application::VoiceOverIP:
					return "VoIP";
			}
			return "Unknown";
		case Parameter::Bitrate:
			return String::formatted("%d", opusBitRate);
		case Parameter::Signal:
			switch (opusSignal) {
				case Signal::Auto:
					return "Auto";
				case Signal::Voice:
					return "Voice";
				case Signal::Music:
					return "Music";
			}
			return "Unknown";
	}
    return String();
}

const String RoundTripOpusAudioProcessor::getInputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

const String RoundTripOpusAudioProcessor::getOutputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

bool RoundTripOpusAudioProcessor::isInputChannelStereoPair (int index) const
{
    return true;
}

bool RoundTripOpusAudioProcessor::isOutputChannelStereoPair (int index) const
{
    return true;
}

bool RoundTripOpusAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool RoundTripOpusAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool RoundTripOpusAudioProcessor::silenceInProducesSilenceOut() const
{
    return false;
}

double RoundTripOpusAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int RoundTripOpusAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int RoundTripOpusAudioProcessor::getCurrentProgram()
{
    return 0;
}

void RoundTripOpusAudioProcessor::setCurrentProgram (int index)
{
}

const String RoundTripOpusAudioProcessor::getProgramName (int index)
{
    return String();
}

void RoundTripOpusAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void RoundTripOpusAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	invalidateSrc();
	
	for (auto &s: srcState) {
		s = src_new(SRC_SINC_FASTEST, 1, nullptr);
	}
	
	inputSamplingRate = sampleRate;
}

void RoundTripOpusAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

void RoundTripOpusAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
	std::lock_guard<std::mutex> lock(objLock);
	
	// make sure number of channel matches
	if (getNumInputChannels() != opusNumChannels) {
		opusNumChannels = getNumInputChannels();
		invalidateOpusCodec();
	}
	createOpusCodec();
	
	opusFrameSize = opusFrameSizeTime * opusSamplingRate / 10000;
	
	// set encoder parameters
	opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(opusBitRate));
	opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(opusComplexity));
	
	int signalType;
	switch (opusSignal) {
		case Signal::Auto:
			signalType = OPUS_AUTO;
			break;
		case Signal::Voice:
			signalType = OPUS_SIGNAL_VOICE;
			break;
		case Signal::Music:
			signalType = OPUS_SIGNAL_MUSIC;
			break;
	}
	//opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(signalType)); // this crashes encoder

	std::size_t numSamples = buffer.getNumSamples();
	std::size_t numChannels = getNumInputChannels();
	
	for (std::size_t i = 0; i < numChannels; ++i) {
		inputBuffer[i].resize(numSamples);
		std::memcpy(inputBuffer[i].data(),
					buffer.getReadPointer(i),
					numSamples * sizeof(float));
		std::memset(buffer.getWritePointer(i), 0,
					numSamples * sizeof(float));
	}
	
	std::size_t writeIndex = 0;
	
	opusInputBuffer.resize(opusFrameSize * opusNumChannels * 4);
	opusOutputBuffer.resize(65536);
	
	std::size_t i = 0;
	
	for (; i < numSamples || writeIndex < numSamples;) {
		bool stall = true;
		
		if (!resolvingOverrun ||
			fifo1->getNumberOfSamplesEnqueueable() > 2048) {
			resolvingOverrun = false;
			AudioFifo::ConstBufferSet inputBufferSet;
			for (std::size_t j = 0; j < 2; ++j) {
				inputBufferSet[j] = j < numChannels ?
				inputBuffer[j].data() + i : nullptr;
			}
			auto inputSunk = fifo1->enqueue(inputBufferSet, numSamples - i);
			i += inputSunk;
			if (inputSunk)
				stall = false;
		} else if (i < numSamples) {
			stall = false;
			i = std::min(i + 2048, numSamples);
		}
		
		// input SRC
		double inputSrcRatio = opusSamplingRate / inputSamplingRate;
		int countLimit = 10000;
		while (fifo1->getNumberOfSamplesDequeueableBySingleRun() &&
			   fifo2->getNumberOfSamplesEnqueueableBySingleRun()) {
			stall = false;
			
			--countLimit;
			assert(countLimit > 0);
			
			fifo1->dequeueSingleCustom
			([&](const AudioFifo::ConstBufferSet& inBuffers, std::size_t inSamples) {
				assert(inSamples);
				
				fifo2->enqueueSingleCustom
				([&](const AudioFifo::BufferSet &outBuffers, std::size_t outSamples) {
					assert(outSamples);
					
					SRC_DATA src;
					for (std::size_t i = 0; i < numChannels; ++i) {
						src.data_in = const_cast<float*>(inBuffers[i]);
						src.data_out = outBuffers[i];
						src.src_ratio = inputSrcRatio;
						src.input_frames = inSamples;
						src.output_frames = outSamples;
						src.input_frames_used = 0;
						src.output_frames_gen = 0;
						src.end_of_input = 0;
						src_process(srcState[i], &src);
						
						// these value must be equal for all channels...
						// (unless libsamplerate uses nondeterministic
						//  process)
						outSamples = src.output_frames_gen;
						inSamples = src.input_frames_used;
					}
					return outSamples;
				});
				return inSamples;
			});
		}
		
		// Opus roundtrip
		while (fifo2->canDequeueAtLeast(opusFrameSize) &&
			   fifo3->canEnqueueAtLeast(opusFrameSize)) {
			stall = false;
			
			AudioFifo::BufferSet writeBufferSet;
			AudioFifo::ConstBufferSet readBufferSet;
			for (std::size_t j = 0; j < 2; ++j) {
				writeBufferSet[j] = j < numChannels ?
				opusInputBuffer.data() + j : nullptr;
				readBufferSet[j] = writeBufferSet[j];
			}
			
			auto count = fifo2->dequeue(writeBufferSet, opusFrameSize, numChannels);
			assert(count == opusFrameSize); (void) count;
			
			int encodedLen = opus_encode_float
			(opusEncoder, opusInputBuffer.data(), opusFrameSize,
			 opusOutputBuffer.data(), opusOutputBuffer.size());
			if (encodedLen < 0) {
				// error...
				encodedLen = 0;
			}
			
			int decodedSamples = opus_decode_float
			(opusDecoder, opusOutputBuffer.data(), encodedLen,
			 opusInputBuffer.data(), opusFrameSize * 4, 0);
			if (decodedSamples < 0) {
				// error...
				decodedSamples = 0;
			}
			
			// output might overrun; don't check the returned value
			fifo3->enqueue(readBufferSet, decodedSamples, numChannels);
		}
		
		// output SRC
		double outputSrcRatio = inputSamplingRate / opusSamplingRate;
		while (fifo3->getNumberOfSamplesDequeueableBySingleRun() &&
			   fifo4->getNumberOfSamplesEnqueueableBySingleRun()) {
			stall = false;
			
			--countLimit;
			assert(countLimit > 0);
			
			fifo3->dequeueSingleCustom
			([&](const AudioFifo::ConstBufferSet& inBuffers, std::size_t inSamples) {
				assert(inSamples);
				
				fifo4->enqueueSingleCustom
				([&](const AudioFifo::BufferSet &outBuffers, std::size_t outSamples) {
					assert(outSamples);
					
					SRC_DATA src;
					for (std::size_t i = 0; i < numChannels; ++i) {
						src.data_in = const_cast<float*>(inBuffers[i]);
						src.data_out = outBuffers[i];
						src.src_ratio = outputSrcRatio;
						src.input_frames = inSamples;
						src.output_frames = outSamples;
						src.input_frames_used = 0;
						src.output_frames_gen = 0;
						src.end_of_input = 0;
						src_process(srcState[i + 2], &src);
						
						// these value must be equal for all channels...
						// (unless libsamplerate uses nondeterministic
						//  process)
						outSamples = src.output_frames_gen;
						inSamples = src.input_frames_used;
					}
					return outSamples;
				});
				return inSamples;
			});
		}
		
		if (!resolvingUnderrun ||
			fifo4->getNumberOfSamplesDequeueable() > 2048) {
			resolvingUnderrun = false;
			
			AudioFifo::BufferSet outputBufferSet;
			for (std::size_t j = 0; j < 2; ++j) {
				outputBufferSet[j] = j < numChannels ?
				buffer.getWritePointer(j) + writeIndex : nullptr;
			}
			auto outputSunk = fifo4->dequeue(outputBufferSet, numSamples - writeIndex);
			writeIndex += outputSunk;
			
			if (outputSunk)
				stall = false;
		} else if (writeIndex < numSamples) {
			stall = false;
			writeIndex = std::min(writeIndex + 2048, numSamples);
		}
		
		if (stall)
			break;
	}
	
	if (i < numSamples) {
		resolvingOverrun = true;
	}
	
	if (writeIndex < numSamples) {
		// underrun occured.
		resolvingUnderrun = true;
	}
	
}

//==============================================================================
bool RoundTripOpusAudioProcessor::hasEditor() const
{
    return false; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* RoundTripOpusAudioProcessor::createEditor()
{
	return nullptr; //new RoundTripOpusAudioProcessorEditor (*this);
}

//==============================================================================
void RoundTripOpusAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void RoundTripOpusAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RoundTripOpusAudioProcessor();
}
