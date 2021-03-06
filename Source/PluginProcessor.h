/*
  ==============================================================================

    This file was auto-generated by the Introjucer!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#ifndef PLUGINPROCESSOR_H_INCLUDED
#define PLUGINPROCESSOR_H_INCLUDED

#include "../JuceLibraryCode/JuceHeader.h"
#include <opus/opus.h>
#include <samplerate.h>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>


//==============================================================================
/**
*/
class RoundTripOpusAudioProcessor  : public AudioProcessor
{
public:
	enum class Parameter
	{
		SamplingRate,
		Bitrate,
		FrameSize,
		Application,
		Signal,
	};
	enum class Application
	{
		Audio,
		VoiceOverIP,
		LowDelay
	};
	enum class Signal
	{
		Auto,
		Voice,
		Music
	};
	
private:
	template <class T, int N>
	class Fifo;
	
	SRC_STATE *srcState[4];
	
	std::mutex objLock;
	
	OpusEncoder *opusEncoder;
	OpusDecoder *opusDecoder;
	
	int opusSamplingRate;
	int opusNumChannels;
	Application opusApplication;
	int opusFrameSizeTime; // 0.1ms
	int opusFrameSize;
	int opusBitRate;
	int opusComplexity;
	Signal opusSignal;
	
	bool resolvingOverrun;
	bool resolvingUnderrun;
	
	using AudioFifo = Fifo<float, 2>;
	
	std::unique_ptr<Fifo<float, 2>> fifo1; // input -> SRC
	std::unique_ptr<Fifo<float, 2>> fifo2; // SRC   -> Opus
	std::unique_ptr<Fifo<float, 2>> fifo3; // Opus  -> SRC
	std::unique_ptr<Fifo<float, 2>> fifo4; // SRC   -> output
	
	std::vector<float> inputBuffer[2];
	
	std::vector<float> opusInputBuffer;
	std::vector<unsigned char> opusOutputBuffer;
	
	double inputSamplingRate;
	
	void createOpusCodec();
	void invalidateOpusCodec();
	
	void invalidateSrc();
	
public:
	
	
    //==============================================================================
    RoundTripOpusAudioProcessor();
    ~RoundTripOpusAudioProcessor();

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock (AudioSampleBuffer&, MidiBuffer&) override;

    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const String getName() const override;

    int getNumParameters() override;
    float getParameter (int index) override;
    void setParameter (int index, float newValue) override;

    const String getParameterName (int index) override;
    const String getParameterText (int index) override;

    const String getInputChannelName (int channelIndex) const override;
    const String getOutputChannelName (int channelIndex) const override;
    bool isInputChannelStereoPair (int index) const override;
    bool isOutputChannelStereoPair (int index) const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool silenceInProducesSilenceOut() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoundTripOpusAudioProcessor)
};


#endif  // PLUGINPROCESSOR_H_INCLUDED
