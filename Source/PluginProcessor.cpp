/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CircularBufferDelayAudioProcessor::CircularBufferDelayAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), params (*this, nullptr, "Params", createParameters())
#endif
{
}

CircularBufferDelayAudioProcessor::~CircularBufferDelayAudioProcessor()
{
}

//==============================================================================
const juce::String CircularBufferDelayAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CircularBufferDelayAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CircularBufferDelayAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CircularBufferDelayAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CircularBufferDelayAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CircularBufferDelayAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int CircularBufferDelayAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CircularBufferDelayAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String CircularBufferDelayAudioProcessor::getProgramName (int index)
{
    return {};
}

void CircularBufferDelayAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void CircularBufferDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    auto delayBufferSize = sampleRate * 2.0;
    delayBuffer.setSize (getTotalNumOutputChannels(), (int)delayBufferSize);
    
    for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
    {
        delayInMillis[ch].reset (sampleRate, 0.05f);
        feedback[ch].reset (sampleRate, 0.05f);
    }
}

void CircularBufferDelayAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CircularBufferDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void CircularBufferDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    auto bufferSize = buffer.getNumSamples();
    auto delayBufferSize = delayBuffer.getNumSamples();
    auto* wet = params.getRawParameterValue ("WET");
    
    buffer.applyGain (0, bufferSize, 1.0f - (wet->load() / 100.0f));
    
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        fillBuffer (channel, bufferSize, delayBufferSize, channelData);
        readFromBuffer (channel, writePosition, buffer, delayBuffer);
        feedbackBuffer (channel, bufferSize, delayBufferSize, channelData);
    }
            
    writePosition += bufferSize;
    writePosition %= delayBufferSize;
}

// Copy contents of main buffer to the circular delay buffer
void CircularBufferDelayAudioProcessor::fillBuffer (int channel, int bufferSize, int delayBufferSize, float* channelData)
{
    // Check to see if main buffer copies to delay buffer without needing to wrap...
    if (delayBufferSize > bufferSize + writePosition)
    {
        // copy main buffer contents to delay buffer
        delayBuffer.copyFrom (channel, writePosition, channelData, bufferSize);
    }
    // if no
    else
    {
        // Determine how much space is left at the end of the delay buffer
        auto numSamplesToEnd = delayBufferSize - writePosition;
        
        // Copy that amount of contents to the end...
        delayBuffer.copyFrom (channel, writePosition, channelData, numSamplesToEnd);
        
        // Calculate how much contents is remaining to copy
        auto numSamplesAtStart = bufferSize - numSamplesToEnd;
        
        // Copy remaining amount to beginning of delay buffer
        delayBuffer.copyFrom (channel, 0, channelData + numSamplesToEnd, numSamplesAtStart);
    }
}

// Read data from the past in the circular delay buffer and add it back to the main buffer
void CircularBufferDelayAudioProcessor::readFromBuffer (int channel, int writePosition, juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& delayBuffer)
{
    auto bufferSize = buffer.getNumSamples();
    auto delayBufferSize = delayBuffer.getNumSamples();
    auto wet = params.getRawParameterValue ("WET");
    auto gain = wet->load() / 100.0f;
    auto* delay = params.getRawParameterValue ("DELAYAMOUNT");
    
    delayInMillis[channel].setTargetValue (delay->load());
    
    auto readPosition = std::floor (writePosition - (getSampleRate() * delayInMillis[channel].getNextValue() / 1000.0f));
    
    if (readPosition < 0)
        readPosition += delayBufferSize;
    
    // Copy delayed data from the past to main buffer
    if (readPosition + bufferSize <= delayBufferSize)
    {
        buffer.addFromWithRamp (channel, 0, delayBuffer.getReadPointer (channel, readPosition), bufferSize, gain, gain);
    }
    else
    {
        auto numSamplesToEnd = delayBufferSize - readPosition;
        buffer.addFromWithRamp (channel, 0, delayBuffer.getReadPointer (channel, readPosition), numSamplesToEnd, gain, gain);
        
        auto numSamplesAtStart = bufferSize - numSamplesToEnd;
        buffer.addFromWithRamp (channel, numSamplesToEnd, delayBuffer.getReadPointer (channel, 0), numSamplesAtStart, gain, gain);
    }
}

// Feed some of the main buffer (that now has delayed data added in) back into the delayed buffer to create an echo effect
void CircularBufferDelayAudioProcessor::feedbackBuffer (int channel, int bufferSize, int delayBufferSize, float* channelData)
{
    auto* fb = params.getRawParameterValue ("FEEDBACK");
    
    feedback[channel].setTargetValue (fb->load());
    
    float gain = juce::Decibels::decibelsToGain (feedback[channel].getNextValue());
    
    // Check to see if main buffer copies to delay buffer without needing to wrap...
    if (delayBufferSize > bufferSize + writePosition)
    {
        // copy main buffer contents to delay buffer
        delayBuffer.addFromWithRamp (channel, writePosition, channelData, bufferSize, gain, gain);
    }
    // if no
    else
    {
        // Determine how much space is left at the end of the delay buffer
        auto numSamplesToEnd = delayBufferSize - writePosition;
        
        // Copy that amount of contents to the end...
        delayBuffer.addFromWithRamp (channel, writePosition, channelData, numSamplesToEnd, gain, gain);
        
        // Calculate how much contents is remaining to copy
        auto numSamplesAtStart = bufferSize - numSamplesToEnd;
        
        // Copy remaining amount to beginning of delay buffer
        delayBuffer.addFromWithRamp (channel, 0, channelData + numSamplesToEnd, numSamplesAtStart, gain, gain);
    }
}

//==============================================================================
bool CircularBufferDelayAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* CircularBufferDelayAudioProcessor::createEditor()
{
    //return new CircularBufferDelayAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

//==============================================================================
void CircularBufferDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void CircularBufferDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CircularBufferDelayAudioProcessor();
}

juce::AudioProcessorValueTreeState::ParameterLayout CircularBufferDelayAudioProcessor::createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back (std::make_unique<juce::AudioParameterFloat>("DELAYAMOUNT", "Delay Amount", 0.0f, 2000.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("FEEDBACK", "Feedback", -60.0f, 6.0f, -60.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("WET", "Wet", 0.0f, 100.0f, 0.0f));

    return { params.begin(), params.end() };
}
