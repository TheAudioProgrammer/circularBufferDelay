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
                       ), params (*this, nullptr, "Parameters", createParameters())
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
    
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        fillBuffer (buffer, channel);
        readFromBuffer (buffer, delayBuffer, channel);
        feedbackBuffer (buffer, channel);
    }
        
    updateBufferPositions (buffer, delayBuffer);
}

void CircularBufferDelayAudioProcessor::fillBuffer (juce::AudioBuffer<float>& buffer, int channel)
{
    auto bufferSize = buffer.getNumSamples();
    auto delayBufferSize = delayBuffer.getNumSamples();
    auto* wet = params.getRawParameterValue ("DRYWET");
    
    buffer.applyGain (0, bufferSize, 1.0f - (wet->load() / 100.0f));
    
    // Check to see if main buffer copies to delay buffer without needing to wrap...
    if (delayBufferSize >= bufferSize + writePosition)
    {
        // copy main buffer contents to delay buffer
        delayBuffer.copyFrom (channel, writePosition, buffer.getWritePointer (channel), bufferSize);
    }
    // if no
    else
    {
        // Determine how much space is left at the end of the delay buffer
        auto numSamplesToEnd = delayBufferSize - writePosition;
        
        // Copy that amount of contents to the end...
        delayBuffer.copyFrom (channel, writePosition, buffer.getWritePointer (channel), numSamplesToEnd);
        
        // Calculate how much contents is remaining to copy
        auto numSamplesAtStart = bufferSize - numSamplesToEnd;
        
        // Copy remaining amount to beginning of delay buffer
        delayBuffer.copyFrom (channel, 0, buffer.getWritePointer (channel, numSamplesToEnd), numSamplesAtStart);
    }
}

void CircularBufferDelayAudioProcessor::feedbackBuffer (juce::AudioBuffer<float>& buffer, int channel)
{
    auto bufferSize = buffer.getNumSamples();
    auto delayBufferSize = delayBuffer.getNumSamples();
    // feedback
    auto fb = params.getRawParameterValue ("FEEDBACK")->load();
    
    // Check to see if main buffer copies to delay buffer without needing to wrap...
    if (delayBufferSize >= bufferSize + writePosition)
    {
        // copy main buffer contents to delay buffer
        delayBuffer.addFromWithRamp (channel, writePosition, buffer.getWritePointer (channel), bufferSize, fb, fb);
    }
    // if no
    else
    {
        // Determine how much space is left at the end of the delay buffer
        auto numSamplesToEnd = delayBufferSize - writePosition;
        
        // Copy that amount of contents to the end...
        delayBuffer.addFromWithRamp (channel, writePosition, buffer.getWritePointer (channel), numSamplesToEnd, fb, fb);
        
        // Calculate how much contents is remaining to copy
        auto numSamplesAtStart = bufferSize - numSamplesToEnd;
        
        // Copy remaining amount to beginning of delay buffer
        delayBuffer.addFromWithRamp (channel, 0, buffer.getWritePointer (channel, numSamplesToEnd), numSamplesAtStart, fb, fb);
    }
}

void CircularBufferDelayAudioProcessor::readFromBuffer (juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& delayBuffer, int channel)
{
    auto bufferSize = buffer.getNumSamples();
    auto delayBufferSize = delayBuffer.getNumSamples();
    
    auto percent = params.getRawParameterValue ("DRYWET")->load();
    auto g = juce::jmap (percent, 0.0f, 100.0f, 0.0f, 1.0f);
    auto dryGain = 1.0f - g;
    
    auto delayTime = params.getRawParameterValue ("DELAYMS")->load();
    
    // delayMs
    auto readPosition = std::round (writePosition - (getSampleRate() * delayTime / 1000.0f));
    
    if (readPosition < 0)
        readPosition += delayBufferSize;
    
    buffer.applyGainRamp (0, bufferSize, dryGain, dryGain);
    
    if (readPosition + bufferSize < delayBufferSize)
    {
        buffer.addFromWithRamp (channel, 0, delayBuffer.getReadPointer (channel, readPosition), bufferSize, g, g);
    }
    else
    {
        auto numSamplesToEnd = delayBufferSize - readPosition;
        buffer.addFromWithRamp (channel, 0, delayBuffer.getReadPointer (channel, readPosition), numSamplesToEnd, g, g);
        
        auto numSamplesAtStart = bufferSize - numSamplesToEnd;
        buffer.addFromWithRamp (channel, numSamplesToEnd, delayBuffer.getReadPointer (channel, 0), numSamplesAtStart, g, g);
    }
}

void CircularBufferDelayAudioProcessor::updateBufferPositions (juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& delayBuffer)
{
    auto bufferSize = buffer.getNumSamples();
    auto delayBufferSize = delayBuffer.getNumSamples();
    
    writePosition += bufferSize;
    writePosition %= delayBufferSize;
}

//==============================================================================
bool CircularBufferDelayAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* CircularBufferDelayAudioProcessor::createEditor()
{
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
    
    params.push_back (std::make_unique<juce::AudioParameterFloat>("DELAYMS", "Delay Ms", 0.0f, 2000.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("FEEDBACK", "Feedback", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("DRYWET", "Dry/Wet", 0.0f, 100.0f, 0.0f));
    
    return { params.begin(), params.end() };
}
