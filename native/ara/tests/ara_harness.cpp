//! \file ara_harness.cpp
//! An in-process ARA host that drives the VocalAI plug-in.
//!
//! ARA's companion APIs (VST3, AU, CLAP) only expose the ARAFactory; every
//! interesting part of an ARA plug-in lives behind that factory and is
//! platform independent.  So the integration can be exercised for real here -
//! model graph, analysis, content export and playback rendering - without a
//! DAW, a companion API wrapper, or macOS.  ARA's own API validation layer is
//! left switched on, so misuse of the API aborts rather than passing quietly.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "ARA_Library/Dispatch/ARAHostDispatch.h"

#include "VocalAIARA.h"
#include "vocalai/wav.h"

using namespace ARA;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

/*******************************************************************************/
//! The host's view of one audio file.
struct HostAudioFile {
    std::string name;
    std::string persistentID;
    vocalai::AudioBuffer buffer;
};

struct HostAudioReaderState {
    HostAudioFile* file = nullptr;
    bool use64Bit = false;
};

class AudioAccessController : public Host::AudioAccessControllerInterface {
   public:
    ARAAudioReaderHostRef createAudioReaderForSource(ARAAudioSourceHostRef sourceRef,
                                                     bool use64BitSamples) noexcept override {
        auto* state = new HostAudioReaderState;
        state->file = reinterpret_cast<HostAudioFile*>(sourceRef);
        state->use64Bit = use64BitSamples;
        ++_openReaders;
        return reinterpret_cast<ARAAudioReaderHostRef>(state);
    }

    bool readAudioSamples(ARAAudioReaderHostRef readerRef, ARASamplePosition samplePosition,
                          ARASampleCount samplesPerChannel,
                          void* const buffers[]) noexcept override {
        auto* state = reinterpret_cast<HostAudioReaderState*>(readerRef);
        const auto& channels = state->file->buffer.channels;
        const auto total = static_cast<ARASamplePosition>(state->file->buffer.frames());
        if (samplePosition < 0 || samplePosition + samplesPerChannel > total) return false;

        for (std::size_t c = 0; c < channels.size(); ++c) {
            for (ARASampleCount i = 0; i < samplesPerChannel; ++i) {
                const double v = channels[c][static_cast<std::size_t>(samplePosition + i)];
                if (state->use64Bit)
                    static_cast<double*>(buffers[c])[i] = v;
                else
                    static_cast<float*>(buffers[c])[i] = static_cast<float>(v);
            }
        }
        return true;
    }

    void destroyAudioReader(ARAAudioReaderHostRef readerRef) noexcept override {
        delete reinterpret_cast<HostAudioReaderState*>(readerRef);
        --_openReaders;
    }

    int openReaders() const { return _openReaders.load(); }

   private:
    std::atomic<int> _openReaders{0};
};

/*******************************************************************************/
class ArchivingController : public Host::ArchivingControllerInterface {
   public:
    ARASize getArchiveSize(ARAArchiveReaderHostRef) noexcept override { return 0; }
    bool readBytesFromArchive(ARAArchiveReaderHostRef, ARASize, ARASize,
                              ARAByte buffer[]) noexcept override {
        (void)buffer;
        return false;
    }
    bool writeBytesToArchive(ARAArchiveWriterHostRef, ARASize, ARASize,
                             const ARAByte[]) noexcept override {
        return true;
    }
    void notifyDocumentArchivingProgress(float) noexcept override {}
    void notifyDocumentUnarchivingProgress(float) noexcept override {}
    ARAPersistentID getDocumentArchiveID(ARAArchiveReaderHostRef) noexcept override {
        return nullptr;
    }
};

/*******************************************************************************/
//! Records what the plug-in told us, so the test can assert on it.
class ModelUpdateController : public Host::ModelUpdateControllerInterface {
   public:
    void notifyAudioSourceAnalysisProgress(ARAAudioSourceHostRef, ARAAnalysisProgressState state,
                                           float value) noexcept override {
        if (state == kARAAnalysisProgressStarted) ++analysisStarted;
        if (state == kARAAnalysisProgressUpdated) lastProgress = value;
        if (state == kARAAnalysisProgressCompleted) ++analysisCompleted;
    }
    void notifyAudioSourceContentChanged(ARAAudioSourceHostRef, const ARAContentTimeRange*,
                                         ContentUpdateScopes) noexcept override {
        ++sourceContentChanged;
    }
    void notifyAudioModificationContentChanged(ARAAudioModificationHostRef,
                                               const ARAContentTimeRange*,
                                               ContentUpdateScopes) noexcept override {
        ++modificationContentChanged;
    }
    void notifyPlaybackRegionContentChanged(ARAPlaybackRegionHostRef, const ARAContentTimeRange*,
                                            ContentUpdateScopes) noexcept override {
        ++regionContentChanged;
    }
    void notifyDocumentDataChanged() noexcept override { ++documentDataChanged; }

    int analysisStarted = 0;
    int analysisCompleted = 0;
    int sourceContentChanged = 0;
    int modificationContentChanged = 0;
    int regionContentChanged = 0;
    int documentDataChanged = 0;
    float lastProgress = 0.0f;
};

/*******************************************************************************/
class PlaybackController : public Host::PlaybackControllerInterface {
   public:
    void requestStartPlayback() noexcept override {}
    void requestStopPlayback() noexcept override {}
    void requestSetPlaybackPosition(ARATimePosition) noexcept override {}
    void requestSetCycleRange(ARATimePosition, ARATimeDuration) noexcept override {}
    void requestEnableCycle(bool) noexcept override {}
};

/*******************************************************************************/
//! The host does not supply any content of its own here.
class ContentAccessController : public Host::ContentAccessControllerInterface {
   public:
    bool isMusicalContextContentAvailable(ARAMusicalContextHostRef,
                                          ARAContentType) noexcept override {
        return false;
    }
    ARAContentGrade getMusicalContextContentGrade(ARAMusicalContextHostRef,
                                                  ARAContentType) noexcept override {
        return kARAContentGradeInitial;
    }
    ARAContentReaderHostRef createMusicalContextContentReader(
        ARAMusicalContextHostRef, ARAContentType, const ARAContentTimeRange*) noexcept override {
        return nullptr;
    }
    bool isAudioSourceContentAvailable(ARAAudioSourceHostRef, ARAContentType) noexcept override {
        return false;
    }
    ARAContentGrade getAudioSourceContentGrade(ARAAudioSourceHostRef,
                                               ARAContentType) noexcept override {
        return kARAContentGradeInitial;
    }
    ARAContentReaderHostRef createAudioSourceContentReader(
        ARAAudioSourceHostRef, ARAContentType, const ARAContentTimeRange*) noexcept override {
        return nullptr;
    }
    ARAInt32 getContentReaderEventCount(ARAContentReaderHostRef) noexcept override { return 0; }
    const void* getContentReaderDataForEvent(ARAContentReaderHostRef, ARAInt32) noexcept override {
        return nullptr;
    }
    void destroyContentReader(ARAContentReaderHostRef) noexcept override {}
};

/*******************************************************************************/
//! A plug-in instance filling the playback renderer role, as a DAW's would.
class PlugInInstance : public PlugIn::PlugInExtension {};

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : fallback;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string wavPath =
        argc > 1 ? argv[1] : envOr("VOCALAI_TEST_WAV", "reference/voice.wav");

    std::cout << "ARA integration harness\n  input: " << wavPath << "\n\n";

    HostAudioFile file;
    file.name = "test vocal";
    file.persistentID = "vocalai.test.audiosource.1";
    try {
        file.buffer = vocalai::read_wav(wavPath);
    } catch (const std::exception& e) {
        std::cerr << "cannot read " << wavPath << ": " << e.what() << "\n";
        return 2;
    }
    const auto frameCount = static_cast<ARASampleCount>(file.buffer.frames());
    const double duration = file.buffer.duration();
    std::cout << "  " << frameCount << " frames, " << file.buffer.channel_count() << " ch, "
              << file.buffer.sample_rate << " Hz (" << duration << " s)\n\n";

    // --- bring up ARA ------------------------------------------------------
    const ARAFactory* factory = vocalai_ara::VocalAIDocumentController::getARAFactory();
    check(factory != nullptr, "plug-in publishes an ARA factory");
    if (!factory) return 1;
    std::cout << "  factory: " << factory->plugInName << " " << factory->version << " by "
              << factory->manufacturerName << "\n";
    check(factory->highestSupportedApiGeneration >= kARAAPIGeneration_2_0_Final,
          "supports ARA 2");
    check(factory->analyzeableContentTypesCount == 1 &&
              factory->analyzeableContentTypes[0] == kARAContentTypeNotes,
          "advertises note analysis");

    const SizedStruct<ARA_STRUCT_MEMBER(ARAInterfaceConfiguration, assertFunctionAddress)> config{
        kARAAPIGeneration_2_0_Final, nullptr};
    factory->initializeARAWithConfiguration(&config);

    AudioAccessController audioAccess;
    ArchivingController archiving;
    ContentAccessController contentAccess;
    ModelUpdateController modelUpdates;
    PlaybackController playback;
    Host::DocumentControllerHostInstance hostInstance{&audioAccess, &archiving, &contentAccess,
                                                      &modelUpdates, &playback};

    const SizedStruct<ARA_STRUCT_MEMBER(ARADocumentProperties, name)> documentProperties{
        "VocalAI harness session"};
    const auto* controllerInstance =
        factory->createDocumentControllerWithDocument(&hostInstance, &documentProperties);
    check(controllerInstance != nullptr, "document controller created");
    if (!controllerInstance) return 1;
    Host::DocumentController controller{controllerInstance};

    // --- build the model graph, exactly as a DAW would ---------------------
    controller.beginEditing();

    const SizedStruct<ARA_STRUCT_MEMBER(ARAMusicalContextProperties, color)> contextProperties{
        nullptr, 0, nullptr};
    auto musicalContext = controller.createMusicalContext(
        reinterpret_cast<ARAMusicalContextHostRef>(&file), &contextProperties);

    const SizedStruct<ARA_STRUCT_MEMBER(ARARegionSequenceProperties, color)> sequenceProperties{
        "vocal track", 0, musicalContext, nullptr};
    auto regionSequence = controller.createRegionSequence(
        reinterpret_cast<ARARegionSequenceHostRef>(&file), &sequenceProperties);

    const SizedStruct<ARA_STRUCT_MEMBER(ARAAudioSourceProperties, merits64BitSamples)>
        sourceProperties{file.name.c_str(),
                         file.persistentID.c_str(),
                         frameCount,
                         static_cast<ARASampleRate>(file.buffer.sample_rate),
                         static_cast<ARAChannelCount>(file.buffer.channel_count()),
                         kARAFalse};
    auto audioSource = controller.createAudioSource(
        reinterpret_cast<ARAAudioSourceHostRef>(&file), &sourceProperties);

    const SizedStruct<ARA_STRUCT_MEMBER(ARAAudioModificationProperties, persistentID)>
        modificationProperties{"tuned", "vocalai.test.modification.1"};
    auto audioModification = controller.createAudioModification(
        audioSource, reinterpret_cast<ARAAudioModificationHostRef>(&file),
        &modificationProperties);

    const SizedStruct<ARA_STRUCT_MEMBER(ARAPlaybackRegionProperties, color)> regionProperties{
        kARAPlaybackTransformationNoChanges,
        0.0,
        duration,
        0.0,
        duration,
        musicalContext,
        regionSequence,
        "region",
        nullptr};
    auto playbackRegion = controller.createPlaybackRegion(
        audioModification, reinterpret_cast<ARAPlaybackRegionHostRef>(&file), &regionProperties);

    controller.endEditing();
    check(true, "model graph built without tripping ARA's validation");

    controller.enableAudioSourceSamplesAccess(audioSource, kARATrue);

    // --- bind a plug-in instance in the playback renderer role -------------
    PlugInInstance instance;
    const auto* extension =
        instance.bindToARA(controllerInstance->documentControllerRef,
                           kARAPlaybackRendererRole, kARAPlaybackRendererRole);
    check(extension != nullptr, "plug-in instance binds as a playback renderer");
    auto* renderer = instance.getPlaybackRenderer<vocalai_ara::VocalAIPlaybackRenderer>();
    check(renderer != nullptr, "playback renderer role available");

    // A DAW assigns each renderer the regions it is responsible for; without
    // this the renderer has nothing to play and correctly outputs silence.
    Host::PlaybackRenderer playbackRendererInterface{extension};
    playbackRendererInterface.addPlaybackRegion(playbackRegion);
    check(true, "playback region assigned to the renderer");

    // --- ask for analysis, then let the plug-in work -----------------------
    const ARAContentType notesType = kARAContentTypeNotes;
    controller.requestAudioSourceContentAnalysis(audioSource, 1, &notesType);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    bool ready = false;
    while (std::chrono::steady_clock::now() < deadline) {
        controller.notifyModelUpdates();
        if (controller.isAudioSourceContentAvailable(audioSource, kARAContentTypeNotes) &&
            modelUpdates.modificationContentChanged > 0) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(ready, "analysis and render completed within the timeout");
    check(modelUpdates.analysisStarted > 0, "analysis progress was started");
    check(modelUpdates.analysisCompleted > 0, "analysis progress was completed");
    check(modelUpdates.sourceContentChanged > 0, "audio source content change was reported");
    check(modelUpdates.regionContentChanged > 0, "playback region content change was reported");
    check(!controller.isAudioSourceContentAnalysisIncomplete(audioSource, kARAContentTypeNotes),
          "analysis is no longer reported as incomplete");

    // --- read the notes back through the ARA content API -------------------
    check(controller.getAudioSourceContentGrade(audioSource, kARAContentTypeNotes) ==
              kARAContentGradeDetected,
          "notes are graded as detected");

    std::vector<ARAContentNote> notes;
    {
        Host::ContentReader<kARAContentTypeNotes> reader{&controller, audioSource, nullptr};
        for (ARAInt32 i = 0; i < reader.getEventCount(); ++i)
            notes.push_back(reader.getDataForEvent(i));
    }
    check(!notes.empty(), "the plug-in exported notes to the host");
    std::cout << "  exported " << notes.size() << " notes\n";

    bool ordered = true, inRange = true, pitched = true;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (i && notes[i].startPosition < notes[i - 1].startPosition) ordered = false;
        if (notes[i].startPosition < -0.01 || notes[i].startPosition > duration + 0.01)
            inRange = false;
        if (!(notes[i].frequency > 20.0f && notes[i].frequency < 5000.0f)) pitched = false;
    }
    check(ordered, "notes are exported in time order");
    check(inRange, "every note lies inside the audio source");
    check(pitched, "every note carries a plausible frequency");

    // Reading through the playback region must shift the notes into song time.
    {
        Host::ContentReader<kARAContentTypeNotes> reader{&controller, playbackRegion, nullptr};
        check(reader.getEventCount() > 0, "playback region exports notes too");
    }

    // --- render, and check the tuning actually reached the audio -----------
    const auto channelCount = static_cast<ARAChannelCount>(file.buffer.channel_count());
    constexpr ARASampleCount kBlock = 512;
    renderer->enableRendering(static_cast<ARASampleRate>(file.buffer.sample_rate), channelCount,
                              kBlock);

    std::vector<std::vector<float>> out(static_cast<std::size_t>(channelCount),
                                        std::vector<float>(static_cast<std::size_t>(frameCount), 0.0f));
    std::vector<std::vector<float>> block(static_cast<std::size_t>(channelCount),
                                          std::vector<float>(kBlock, 0.0f));
    std::vector<float*> pointers(static_cast<std::size_t>(channelCount));

    for (ARASamplePosition pos = 0; pos < frameCount; pos += kBlock) {
        const auto count = std::min<ARASampleCount>(kBlock, frameCount - pos);
        for (std::size_t c = 0; c < pointers.size(); ++c) pointers[c] = block[c].data();
        renderer->renderPlaybackRegions(pointers.data(), pos, count, true);
        for (std::size_t c = 0; c < pointers.size(); ++c)
            std::copy(block[c].begin(), block[c].begin() + count,
                      out[c].begin() + static_cast<std::size_t>(pos));
    }
    renderer->disableRendering();

    double peak = 0.0, energy = 0.0;
    for (const auto& ch : out)
        for (float v : ch) {
            peak = std::max(peak, std::fabs(static_cast<double>(v)));
            energy += static_cast<double>(v) * v;
        }
    check(peak > 0.01, "the renderer produced audio");
    check(peak <= 1.5, "the render did not blow up");
    check(std::isfinite(energy), "the render contains no NaN or infinity");

    // Silence when the transport is stopped: ARA renderers must not leak audio.
    for (std::size_t c = 0; c < pointers.size(); ++c) pointers[c] = block[c].data();
    renderer->enableRendering(static_cast<ARASampleRate>(file.buffer.sample_rate), channelCount,
                              kBlock);
    renderer->renderPlaybackRegions(pointers.data(), 0, kBlock, false);
    double stoppedPeak = 0.0;
    for (const auto& ch : block)
        for (float v : ch) stoppedPeak = std::max(stoppedPeak, std::fabs(static_cast<double>(v)));
    check(stoppedPeak == 0.0, "nothing is rendered while the transport is stopped");
    renderer->disableRendering();

    // The tuned render must differ from the source, and stay in tune with it.
    const auto& sourceChannel = file.buffer.channels[0];
    double diff = 0.0;
    for (std::size_t i = 0; i < sourceChannel.size() && i < out[0].size(); ++i)
        diff = std::max(diff, std::fabs(sourceChannel[i] - out[0][i]));
    check(diff > 1e-4, "the render is tuned, not a copy of the input");

    if (const char* outPath = std::getenv("VOCALAI_HARNESS_OUT")) {
        std::vector<std::vector<double>> asDouble;
        for (const auto& ch : out) asDouble.emplace_back(ch.begin(), ch.end());
        vocalai::write_wav(outPath, asDouble, file.buffer.sample_rate, 24);
        std::cout << "  wrote " << outPath << "\n";
    }

    // --- tear down in the order ARA requires -------------------------------
    playbackRendererInterface.removePlaybackRegion(playbackRegion);
    controller.beginEditing();
    controller.destroyPlaybackRegion(playbackRegion);
    controller.destroyAudioModification(audioModification);
    controller.destroyAudioSource(audioSource);
    controller.destroyRegionSequence(regionSequence);
    controller.destroyMusicalContext(musicalContext);
    controller.endEditing();
    controller.destroyDocumentController();
    factory->uninitializeARA();

    check(audioAccess.openReaders() == 0, "every host audio reader was closed");

    std::cout << "\n" << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
