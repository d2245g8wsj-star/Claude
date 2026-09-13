//! \file VocalAIARA.h
//! ARA 2 plug-in implementation for the VocalAI engine.
//!
//! ARA exists for plug-ins that are closer to a sample editor than to a
//! realtime processor, which is exactly this engine's shape: key detection,
//! note segmentation and the Viterbi decode over the melody all need the whole
//! take before they can decide anything.  ARA hands the plug-in the host's
//! audio at will, so none of that has to be faked with a transfer step.
//!
//! The model follows ARA's own division of labour:
//!   - the audio source owns what is *true of the recording*: its samples, its
//!     pitch track, its notes.  That analysis is expensive and is shared.
//!   - the audio modification owns what the *user decided*: tuning style, key,
//!     and per-note edits, along with the rendered result of those decisions.
//!     Two modifications of one source can be tuned differently.
//!   - the playback renderer only reads the finished render.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ARA_Library/PlugIn/ARAPlug.h"

#include "vocalai/engine.h"

namespace vocalai_ara {

//! Settings a user can change per audio modification.
struct TuningSettings {
    std::string style = "natural";
    std::string key;              //!< empty means "detect it"
    double tuning_hz = 0.0;       //!< 0 means "detect it"
    bool enabled = true;

    bool operator== (const TuningSettings& other) const noexcept {
        return style == other.style && key == other.key && tuning_hz == other.tuning_hz &&
               enabled == other.enabled;
    }
};

//! The immutable result of analysing one audio source.
struct SourceAnalysis {
    vocalai::TrackAnalysis track;
    bool valid = false;
};

//! The immutable result of tuning one audio modification.
struct ModificationRender {
    std::vector<std::vector<float>> channels;  //!< the tuned audio, ready to play
    vocalai::NoteSequence notes;               //!< notes with their targets decided
    double tuning_hz = 440.0;
    std::string key_name;
    bool valid = false;
};

class VocalAIDocumentController;

/*******************************************************************************/
//! Holds the recording's samples and the analysis read from them.
class VocalAIAudioSource : public ARA::PlugIn::AudioSource {
   public:
    using ARA::PlugIn::AudioSource::AudioSource;

    //! Pull every sample from the host.  Only legal while sample access is
    //! enabled, and never from the audio thread.
    bool readAllSamples (ARA::PlugIn::HostAudioReader& reader,
                         const std::atomic<bool>* cancel = nullptr);

    const std::vector<std::vector<float>>& samples() const noexcept { return _samples; }
    void releaseSamples () { _samples.clear(); _samples.shrink_to_fit(); }
    bool hasSamples () const noexcept { return !_samples.empty(); }

    std::shared_ptr<const SourceAnalysis> analysis () const;
    void setAnalysis (std::shared_ptr<const SourceAnalysis> analysis);

   private:
    std::vector<std::vector<float>> _samples;
    mutable std::mutex _mutex;
    std::shared_ptr<const SourceAnalysis> _analysis;
};

/*******************************************************************************/
//! Holds the user's tuning decisions and the audio they produce.
class VocalAIAudioModification : public ARA::PlugIn::AudioModification {
   public:
    using ARA::PlugIn::AudioModification::AudioModification;


    TuningSettings settings() const;
    //! Returns true when the settings actually changed, so callers know to re-render.
    bool setSettings(const TuningSettings& settings);

    std::shared_ptr<const ModificationRender> render() const;
    void setRender(std::shared_ptr<const ModificationRender> render);
    void invalidateRender();

   private:
    mutable std::mutex _mutex;
    TuningSettings _settings;
    std::shared_ptr<const ModificationRender> _render;
};

/*******************************************************************************/
class VocalAIPlaybackRenderer : public ARA::PlugIn::PlaybackRenderer {
   public:
    using PlaybackRenderer::PlaybackRenderer;

    void renderPlaybackRegions(float* const* outputs, ARA::ARASamplePosition samplePosition,
                               ARA::ARASampleCount samplesToRender, bool isPlayingBack);

    void enableRendering(ARA::ARASampleRate sampleRate, ARA::ARAChannelCount channelCount,
                         ARA::ARASampleCount maxSamplesToRender) noexcept;
    void disableRendering() noexcept;

   private:
    ARA::ARASampleRate _sampleRate{44100.0};
    ARA::ARAChannelCount _channelCount{1};
    ARA::ARASampleCount _maxSamplesToRender{4096};
};

/*******************************************************************************/
class VocalAIDocumentController : public ARA::PlugIn::DocumentController {
   public:
    using ARA::PlugIn::DocumentController::DocumentController;

    static const ARA::ARAFactory* getARAFactory() noexcept;

    //! Guards the model against being edited while a renderer is reading it.
    bool rendererWillAccessModelGraph(VocalAIPlaybackRenderer* renderer) noexcept;
    void rendererDidAccessModelGraph(VocalAIPlaybackRenderer* renderer) noexcept;

    //! Change the tuning of one modification and schedule the re-render.
    void updateTuningSettings(VocalAIAudioModification* modification,
                              const TuningSettings& settings);

   protected:
    void willBeginEditing() noexcept override;
    void didEndEditing() noexcept override;
    void willNotifyModelUpdates() noexcept override;

    ARA::PlugIn::AudioSource* doCreateAudioSource(
        ARA::PlugIn::Document* document, ARA::ARAAudioSourceHostRef hostRef) noexcept override;
    //! The host owns tempo, key and time signature; this plug-in reads none of
    //! them yet, so there is nothing to invalidate when they change.
    void doUpdateMusicalContextContent(ARA::PlugIn::MusicalContext* musicalContext,
                                       const ARA::ARAContentTimeRange* range,
                                       ARA::ContentUpdateScopes scopeFlags) noexcept override;

    ARA::PlugIn::PlaybackRenderer* doCreatePlaybackRenderer() noexcept override;
    ARA::PlugIn::AudioModification* doCreateAudioModification(
        ARA::PlugIn::AudioSource* audioSource, ARA::ARAAudioModificationHostRef hostRef,
        const ARA::PlugIn::AudioModification* optionalModificationToClone) noexcept override;

    void willEnableAudioSourceSamplesAccess(ARA::PlugIn::AudioSource* audioSource,
                                            bool enable) noexcept override;
    void didEnableAudioSourceSamplesAccess(ARA::PlugIn::AudioSource* audioSource,
                                           bool enable) noexcept override;
    void willDestroyAudioSource(ARA::PlugIn::AudioSource* audioSource) noexcept override;
    void doUpdateAudioSourceContent(ARA::PlugIn::AudioSource* audioSource,
                                    const ARA::ARAContentTimeRange* range,
                                    ARA::ContentUpdateScopes scopeFlags) noexcept override;

    // Content export: the notes we detected, in ARA's own vocabulary.
    bool doIsAudioSourceContentAvailable(const ARA::PlugIn::AudioSource* audioSource,
                                         ARA::ARAContentType type) noexcept override;
    ARA::ARAContentGrade doGetAudioSourceContentGrade(const ARA::PlugIn::AudioSource* audioSource,
                                                      ARA::ARAContentType type) noexcept override;
    ARA::PlugIn::ContentReader* doCreateAudioSourceContentReader(
        ARA::PlugIn::AudioSource* audioSource, ARA::ARAContentType type,
        const ARA::ARAContentTimeRange* range) noexcept override;

    bool doIsAudioModificationPreservingAudioSourceSignal(
        ARA::PlugIn::AudioModification* modification) noexcept override;
    bool doIsAudioModificationContentAvailable(
        const ARA::PlugIn::AudioModification* modification,
        ARA::ARAContentType type) noexcept override;
    ARA::ARAContentGrade doGetAudioModificationContentGrade(
        const ARA::PlugIn::AudioModification* modification,
        ARA::ARAContentType type) noexcept override;
    ARA::PlugIn::ContentReader* doCreateAudioModificationContentReader(
        ARA::PlugIn::AudioModification* modification, ARA::ARAContentType type,
        const ARA::ARAContentTimeRange* range) noexcept override;

    bool doIsPlaybackRegionContentAvailable(const ARA::PlugIn::PlaybackRegion* playbackRegion,
                                            ARA::ARAContentType type) noexcept override;
    ARA::ARAContentGrade doGetPlaybackRegionContentGrade(
        const ARA::PlugIn::PlaybackRegion* playbackRegion,
        ARA::ARAContentType type) noexcept override;
    ARA::PlugIn::ContentReader* doCreatePlaybackRegionContentReader(
        ARA::PlugIn::PlaybackRegion* playbackRegion, ARA::ARAContentType type,
        const ARA::ARAContentTimeRange* range) noexcept override;

    // Analysis
    bool doIsAudioSourceContentAnalysisIncomplete(const ARA::PlugIn::AudioSource* audioSource,
                                                  ARA::ARAContentType type) noexcept override;
    void doRequestAudioSourceContentAnalysis(
        ARA::PlugIn::AudioSource* audioSource,
        std::vector<ARA::ARAContentType> const& contentTypes) noexcept override;
    ARA::ARAInt32 doGetProcessingAlgorithmsCount() noexcept override;
    const ARA::ARAProcessingAlgorithmProperties* doGetProcessingAlgorithmProperties(
        ARA::ARAInt32 algorithmIndex) noexcept override;

    bool doRestoreObjectsFromArchive(
        ARA::PlugIn::HostArchiveReader* archiveReader,
        const ARA::PlugIn::RestoreObjectsFilter* filter) noexcept override;
    bool doStoreObjectsToArchive(ARA::PlugIn::HostArchiveWriter* archiveWriter,
                                 const ARA::PlugIn::StoreObjectsFilter* filter) noexcept override;

   private:
    struct Job;

    void startAnalysis(VocalAIAudioSource* audioSource);
    void scheduleRender(VocalAIAudioModification* modification);
    void pumpCompletedJobs();
    void cancelJobsFor(const ARA::PlugIn::AudioSource* audioSource);

    std::vector<std::unique_ptr<Job>> _jobs;
    std::mutex _jobMutex;

    std::atomic<bool> _renderersCanAccessModelGraph{true};
    std::atomic<int> _countOfRenderersCurrentlyAccessingModelGraph{0};
};

}  // namespace vocalai_ara
