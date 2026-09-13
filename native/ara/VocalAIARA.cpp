//! \file VocalAIARA.cpp
//! ARA 2 plug-in implementation for the VocalAI engine.

#include "VocalAIARA.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "vocalai/key.h"
#include "vocalai/notes.h"
#include "vocalai/psola.h"
#include "vocalai/tuner.h"

namespace vocalai_ara {
namespace {

constexpr const char* kFactoryID = "com.vocalai.aratuner.factory";
constexpr const char* kPlugInName = "VocalAI";
constexpr const char* kManufacturer = "VocalAI";
constexpr const char* kInformationURL = "https://github.com/d2245g8wsj-star/Claude";
constexpr const char* kVersion = "1.0.0";
constexpr const char* kDocumentArchiveID = "com.vocalai.aratuner.archive.v1";

constexpr std::array<ARA::ARAContentType, 1> kAnalyzeableContentTypes{
    {ARA::kARAContentTypeNotes}};

//! Turn one of our notes into ARA's note vocabulary.
ARA::ARAContentNote toARANote(const vocalai::Note& note, double tuning_hz, double timeOffset) {
    ARA::ARAContentNote out{};
    const double hz = note.has_target ? note.target_frequency(tuning_hz) : note.center_hz;
    out.frequency = static_cast<float>(hz);
    out.pitchNumber = hz > 0.0
                          ? static_cast<ARA::ARAPitchNumber>(std::lround(
                                69.0 + 12.0 * std::log2(hz / tuning_hz)))
                          : ARA::kARAInvalidPitchNumber;
    // ARA wants linear volume; our notes carry dBFS.
    out.volume = static_cast<float>(std::pow(10.0, note.amplitude_db / 20.0));
    out.startPosition = note.start + timeOffset;
    out.attackDuration = std::max(note.core_start - note.start, 0.0);
    out.noteDuration = note.duration();
    out.signalDuration = note.duration();
    return out;
}

//! Exports detected notes to the host.  The host uses these to draw the notes,
//! to follow the melody, and to hand them to other plug-ins.
class NoteContentReader : public ARA::PlugIn::ContentReader {
   public:
    NoteContentReader(const vocalai::NoteSequence& notes, double tuning_hz, double timeOffset,
                      const ARA::ARAContentTimeRange* range) {
        for (const auto& note : notes.notes) {
            const double start = note.start + timeOffset;
            const double end = start + note.duration();
            if (range && (end <= range->start || range->start + range->duration <= start)) continue;
            _notes.push_back(toARANote(note, tuning_hz, timeOffset));
        }
    }

    ARA::ARAInt32 getEventCount() noexcept override {
        return static_cast<ARA::ARAInt32>(_notes.size());
    }
    const void* getDataForEvent(ARA::ARAInt32 index) noexcept override {
        return &_notes[static_cast<std::size_t>(index)];
    }

   private:
    std::vector<ARA::ARAContentNote> _notes;
};

class VocalAIFactoryConfig : public ARA::PlugIn::FactoryConfig {
   public:
    ARA::ARAAPIGeneration getHighestSupportedApiGeneration() const noexcept override {
        return ARA::kARAAPIGeneration_2_0_Final;
    }
    const char* getFactoryID() const noexcept override { return kFactoryID; }
    const char* getPlugInName() const noexcept override { return kPlugInName; }
    const char* getManufacturerName() const noexcept override { return kManufacturer; }
    const char* getInformationURL() const noexcept override { return kInformationURL; }
    const char* getVersion() const noexcept override { return kVersion; }
    const char* getDocumentArchiveID() const noexcept override { return kDocumentArchiveID; }

    ARA::ARASize getAnalyzeableContentTypesCount() const noexcept override {
        return kAnalyzeableContentTypes.size();
    }
    const ARA::ARAContentType* getAnalyzeableContentTypes() const noexcept override {
        return kAnalyzeableContentTypes.data();
    }
};

vocalai::TuneConfig configFor(const TuningSettings& settings) {
    try {
        return vocalai::TuneConfig::style(settings.style);
    } catch (const std::exception&) {
        return vocalai::TuneConfig::style("natural");
    }
}

}  // namespace

/*******************************************************************************/

bool VocalAIAudioSource::readAllSamples(ARA::PlugIn::HostAudioReader& reader,
                                        const std::atomic<bool>* cancel) {
    const auto total = getSampleCount();
    const auto channels = getChannelCount();
    if (total <= 0 || channels <= 0) return false;

    _samples.assign(static_cast<std::size_t>(channels),
                    std::vector<float>(static_cast<std::size_t>(total), 0.0f));

    // Read in blocks: hosts are not obliged to serve an entire take at once.
    constexpr ARA::ARASampleCount kBlock = 1 << 15;
    std::vector<std::vector<float>> block(static_cast<std::size_t>(channels),
                                          std::vector<float>(static_cast<std::size_t>(kBlock)));
    std::vector<void*> pointers(static_cast<std::size_t>(channels));

    for (ARA::ARASamplePosition pos = 0; pos < total;) {
        if (cancel && cancel->load()) return false;
        const auto count = std::min<ARA::ARASampleCount>(kBlock, total - pos);
        for (std::size_t c = 0; c < static_cast<std::size_t>(channels); ++c)
            pointers[c] = block[c].data();
        if (!reader.readAudioSamples(pos, count, pointers.data())) {
            _samples.clear();
            return false;
        }
        for (std::size_t c = 0; c < static_cast<std::size_t>(channels); ++c)
            std::copy(block[c].begin(), block[c].begin() + count,
                      _samples[c].begin() + static_cast<std::size_t>(pos));
        pos += count;
    }
    return true;
}

std::shared_ptr<const SourceAnalysis> VocalAIAudioSource::analysis() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _analysis;
}

void VocalAIAudioSource::setAnalysis(std::shared_ptr<const SourceAnalysis> analysis) {
    std::lock_guard<std::mutex> lock(_mutex);
    _analysis = std::move(analysis);
}

/*******************************************************************************/

TuningSettings VocalAIAudioModification::settings() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _settings;
}

bool VocalAIAudioModification::setSettings(const TuningSettings& settings) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_settings == settings) return false;
    _settings = settings;
    _render.reset();
    return true;
}

std::shared_ptr<const ModificationRender> VocalAIAudioModification::render() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _render;
}

void VocalAIAudioModification::setRender(std::shared_ptr<const ModificationRender> render) {
    std::lock_guard<std::mutex> lock(_mutex);
    _render = std::move(render);
}

void VocalAIAudioModification::invalidateRender() {
    std::lock_guard<std::mutex> lock(_mutex);
    _render.reset();
}

/*******************************************************************************/

struct VocalAIDocumentController::Job {
    enum class Kind { Analysis, Render };

    Kind kind = Kind::Analysis;
    VocalAIAudioSource* source = nullptr;
    VocalAIAudioModification* modification = nullptr;

    std::thread thread;
    std::atomic<bool> finished{false};
    std::atomic<bool> cancelled{false};
    std::atomic<float> progress{0.0f};
    bool progressStarted = false;
    bool succeeded = false;

    std::shared_ptr<const SourceAnalysis> analysisResult;
    std::shared_ptr<const ModificationRender> renderResult;

    ~Job() {
        cancelled = true;
        if (thread.joinable()) thread.join();
    }
};

const ARA::ARAFactory* VocalAIDocumentController::getARAFactory() noexcept {
    return ARA::PlugIn::PlugInEntry::getPlugInEntry<VocalAIFactoryConfig,
                                                    VocalAIDocumentController>()
        ->getFactory();
}

bool VocalAIDocumentController::rendererWillAccessModelGraph(
    VocalAIPlaybackRenderer* /*renderer*/) noexcept {
    ++_countOfRenderersCurrentlyAccessingModelGraph;
    return _renderersCanAccessModelGraph;
}

void VocalAIDocumentController::rendererDidAccessModelGraph(
    VocalAIPlaybackRenderer* /*renderer*/) noexcept {
    --_countOfRenderersCurrentlyAccessingModelGraph;
}

void VocalAIDocumentController::willBeginEditing() noexcept {
    // Shut the renderers out of the model, then wait for any that are inside to
    // leave.  Editing the graph underneath a running audio callback is the one
    // mistake ARA cannot protect a plug-in from.
    _renderersCanAccessModelGraph = false;
    while (_countOfRenderersCurrentlyAccessingModelGraph.load() > 0)
        std::this_thread::yield();
}

void VocalAIDocumentController::didEndEditing() noexcept {
    _renderersCanAccessModelGraph = true;
    pumpCompletedJobs();
}

void VocalAIDocumentController::willNotifyModelUpdates() noexcept { pumpCompletedJobs(); }

ARA::PlugIn::AudioSource* VocalAIDocumentController::doCreateAudioSource(
    ARA::PlugIn::Document* document, ARA::ARAAudioSourceHostRef hostRef) noexcept {
    return new VocalAIAudioSource(document, hostRef);
}

void VocalAIDocumentController::doUpdateMusicalContextContent(
    ARA::PlugIn::MusicalContext* /*musicalContext*/, const ARA::ARAContentTimeRange* /*range*/,
    ARA::ContentUpdateScopes /*scopeFlags*/) noexcept {}

ARA::PlugIn::PlaybackRenderer* VocalAIDocumentController::doCreatePlaybackRenderer() noexcept {
    return new VocalAIPlaybackRenderer(this);
}

ARA::PlugIn::AudioModification* VocalAIDocumentController::doCreateAudioModification(
    ARA::PlugIn::AudioSource* audioSource, ARA::ARAAudioModificationHostRef hostRef,
    const ARA::PlugIn::AudioModification* optionalModificationToClone) noexcept {
    auto* modification =
        new VocalAIAudioModification(audioSource, hostRef, optionalModificationToClone);
    if (optionalModificationToClone) {
        const auto* source =
            static_cast<const VocalAIAudioModification*>(optionalModificationToClone);
        modification->setSettings(const_cast<VocalAIAudioModification*>(source)->settings());
    }
    return modification;
}

void VocalAIDocumentController::willEnableAudioSourceSamplesAccess(
    ARA::PlugIn::AudioSource* audioSource, bool enable) noexcept {
    if (!enable) cancelJobsFor(audioSource);
    // Renderers read the cached render, not the host's samples, but the model
    // is about to change either way.
    _renderersCanAccessModelGraph = false;
    while (_countOfRenderersCurrentlyAccessingModelGraph.load() > 0) std::this_thread::yield();
}

void VocalAIDocumentController::didEnableAudioSourceSamplesAccess(
    ARA::PlugIn::AudioSource* audioSource, bool enable) noexcept {
    _renderersCanAccessModelGraph = true;
    if (enable) {
        auto* source = static_cast<VocalAIAudioSource*>(audioSource);
        if (!source->analysis()) startAnalysis(source);
    }
}

void VocalAIDocumentController::willDestroyAudioSource(
    ARA::PlugIn::AudioSource* audioSource) noexcept {
    cancelJobsFor(audioSource);
}

void VocalAIDocumentController::doUpdateAudioSourceContent(
    ARA::PlugIn::AudioSource* audioSource, const ARA::ARAContentTimeRange* /*range*/,
    ARA::ContentUpdateScopes scopeFlags) noexcept {
    if (!scopeFlags.affectSamples()) return;
    // The recording itself changed, so everything read from it is stale.
    cancelJobsFor(audioSource);
    auto* source = static_cast<VocalAIAudioSource*>(audioSource);
    source->setAnalysis(nullptr);
    source->releaseSamples();
    for (auto* modification : source->getAudioModifications())
        static_cast<VocalAIAudioModification*>(modification)->invalidateRender();
}

/*******************************************************************************/

bool VocalAIDocumentController::doIsAudioSourceContentAvailable(
    const ARA::PlugIn::AudioSource* audioSource, ARA::ARAContentType type) noexcept {
    if (type != ARA::kARAContentTypeNotes) return false;
    const auto analysis =
        static_cast<const VocalAIAudioSource*>(audioSource)->analysis();
    return analysis && analysis->valid;
}

ARA::ARAContentGrade VocalAIDocumentController::doGetAudioSourceContentGrade(
    const ARA::PlugIn::AudioSource* /*audioSource*/, ARA::ARAContentType /*type*/) noexcept {
    // Detected, not authored by a person: "detected" is exactly what this means.
    return ARA::kARAContentGradeDetected;
}

ARA::PlugIn::ContentReader* VocalAIDocumentController::doCreateAudioSourceContentReader(
    ARA::PlugIn::AudioSource* audioSource, ARA::ARAContentType type,
    const ARA::ARAContentTimeRange* range) noexcept {
    if (type != ARA::kARAContentTypeNotes) return nullptr;
    const auto analysis = static_cast<VocalAIAudioSource*>(audioSource)->analysis();
    if (!analysis || !analysis->valid) return nullptr;
    return new NoteContentReader(analysis->track.notes, analysis->track.key.tuning_hz, 0.0, range);
}

bool VocalAIDocumentController::doIsAudioModificationPreservingAudioSourceSignal(
    ARA::PlugIn::AudioModification* modification) noexcept {
    // Tuning changes the signal; saying otherwise would let the host skip us.
    return !static_cast<VocalAIAudioModification*>(modification)->settings().enabled;
}

bool VocalAIDocumentController::doIsAudioModificationContentAvailable(
    const ARA::PlugIn::AudioModification* modification, ARA::ARAContentType type) noexcept {
    if (type != ARA::kARAContentTypeNotes) return false;
    const auto render = static_cast<const VocalAIAudioModification*>(modification)->render();
    if (render && render->valid) return true;
    return doIsAudioSourceContentAvailable(modification->getAudioSource(), type);
}

ARA::ARAContentGrade VocalAIDocumentController::doGetAudioModificationContentGrade(
    const ARA::PlugIn::AudioModification* /*modification*/,
    ARA::ARAContentType /*type*/) noexcept {
    return ARA::kARAContentGradeDetected;
}

ARA::PlugIn::ContentReader* VocalAIDocumentController::doCreateAudioModificationContentReader(
    ARA::PlugIn::AudioModification* modification, ARA::ARAContentType type,
    const ARA::ARAContentTimeRange* range) noexcept {
    if (type != ARA::kARAContentTypeNotes) return nullptr;
    // After tuning, the notes the host should see are the corrected ones.
    const auto render = static_cast<VocalAIAudioModification*>(modification)->render();
    if (render && render->valid)
        return new NoteContentReader(render->notes, render->tuning_hz, 0.0, range);
    return doCreateAudioSourceContentReader(modification->getAudioSource(), type, range);
}

bool VocalAIDocumentController::doIsPlaybackRegionContentAvailable(
    const ARA::PlugIn::PlaybackRegion* playbackRegion, ARA::ARAContentType type) noexcept {
    return doIsAudioModificationContentAvailable(playbackRegion->getAudioModification(), type);
}

ARA::ARAContentGrade VocalAIDocumentController::doGetPlaybackRegionContentGrade(
    const ARA::PlugIn::PlaybackRegion* /*playbackRegion*/, ARA::ARAContentType /*type*/) noexcept {
    return ARA::kARAContentGradeDetected;
}

ARA::PlugIn::ContentReader* VocalAIDocumentController::doCreatePlaybackRegionContentReader(
    ARA::PlugIn::PlaybackRegion* playbackRegion, ARA::ARAContentType type,
    const ARA::ARAContentTimeRange* range) noexcept {
    if (type != ARA::kARAContentTypeNotes) return nullptr;
    auto* modification =
        static_cast<VocalAIAudioModification*>(playbackRegion->getAudioModification());
    const auto render = modification->render();

    // A region shows a window onto the modification, shifted into playback time.
    const double offset = playbackRegion->getStartInPlaybackTime() -
                          playbackRegion->getStartInAudioModificationTime();
    ARA::ARAContentTimeRange clipped{playbackRegion->getStartInPlaybackTime(),
                                     playbackRegion->getDurationInPlaybackTime()};
    if (range) {
        const double start = std::max(range->start, clipped.start);
        const double end =
            std::min(range->start + range->duration, clipped.start + clipped.duration);
        clipped.start = start;
        clipped.duration = std::max(end - start, 0.0);
    }
    ARA::ARAContentTimeRange inModification{clipped.start - offset, clipped.duration};

    if (render && render->valid)
        return new NoteContentReader(render->notes, render->tuning_hz, offset, &inModification);
    const auto analysis =
        static_cast<VocalAIAudioSource*>(modification->getAudioSource())->analysis();
    if (!analysis || !analysis->valid) return nullptr;
    return new NoteContentReader(analysis->track.notes, analysis->track.key.tuning_hz, offset,
                                 &inModification);
}

/*******************************************************************************/

bool VocalAIDocumentController::doIsAudioSourceContentAnalysisIncomplete(
    const ARA::PlugIn::AudioSource* audioSource, ARA::ARAContentType type) noexcept {
    if (type != ARA::kARAContentTypeNotes) return false;
    const auto analysis = static_cast<const VocalAIAudioSource*>(audioSource)->analysis();
    return !(analysis && analysis->valid);
}

void VocalAIDocumentController::doRequestAudioSourceContentAnalysis(
    ARA::PlugIn::AudioSource* audioSource,
    std::vector<ARA::ARAContentType> const& contentTypes) noexcept {
    const bool wantsNotes = std::find(contentTypes.begin(), contentTypes.end(),
                                      ARA::kARAContentTypeNotes) != contentTypes.end();
    if (!wantsNotes) return;
    auto* source = static_cast<VocalAIAudioSource*>(audioSource);
    if (source->analysis()) return;
    startAnalysis(source);
}

ARA::ARAInt32 VocalAIDocumentController::doGetProcessingAlgorithmsCount() noexcept { return 0; }

const ARA::ARAProcessingAlgorithmProperties*
VocalAIDocumentController::doGetProcessingAlgorithmProperties(
    ARA::ARAInt32 /*algorithmIndex*/) noexcept {
    return nullptr;
}

bool VocalAIDocumentController::doRestoreObjectsFromArchive(
    ARA::PlugIn::HostArchiveReader* /*archiveReader*/,
    const ARA::PlugIn::RestoreObjectsFilter* /*filter*/) noexcept {
    // Persistency is not implemented yet: returning true would claim the state
    // was restored when nothing was read, so a reopened session would look
    // fine while silently having lost every edit.  Analysis simply re-runs.
    return false;
}

bool VocalAIDocumentController::doStoreObjectsToArchive(
    ARA::PlugIn::HostArchiveWriter* /*archiveWriter*/,
    const ARA::PlugIn::StoreObjectsFilter* /*filter*/) noexcept {
    return false;
}

/*******************************************************************************/

void VocalAIDocumentController::startAnalysis(VocalAIAudioSource* source) {
    if (!source->isSampleAccessEnabled()) return;

    std::lock_guard<std::mutex> lock(_jobMutex);
    for (const auto& job : _jobs)
        if (job->source == source && job->kind == Job::Kind::Analysis) return;

    auto job = std::make_unique<Job>();
    job->kind = Job::Kind::Analysis;
    job->source = source;

    // The reader is made here, on the model thread; reading itself happens on
    // the worker, which ARA permits while sample access stays enabled.
    auto reader = std::make_shared<ARA::PlugIn::HostAudioReader>(source);
    Job* raw = job.get();
    const int sampleRate = static_cast<int>(source->getSampleRate());
    const std::string name = source->getName() ? source->getName() : "audio source";

    job->thread = std::thread([raw, reader, sampleRate, name]() {
        auto result = std::make_shared<SourceAnalysis>();
        if (!raw->source->readAllSamples(*reader, &raw->cancelled) || raw->cancelled.load()) {
            raw->finished = true;
            return;
        }
        raw->progress = 0.25f;
        if (raw->cancelled.load()) {
            raw->finished = true;
            return;
        }

        const auto& channels = raw->source->samples();
        std::vector<double> mono(channels.empty() ? 0 : channels[0].size(), 0.0);
        for (const auto& ch : channels)
            for (std::size_t i = 0; i < mono.size() && i < ch.size(); ++i) mono[i] += ch[i];
        if (!channels.empty())
            for (double& v : mono) v /= static_cast<double>(channels.size());

        result->track = vocalai::analyze_track(mono, sampleRate, name);
        result->track.key = vocalai::estimate_key(result->track.notes);
        result->track.key_known = true;
        result->valid = true;
        raw->progress = 1.0f;
        raw->analysisResult = std::move(result);
        raw->succeeded = true;
        raw->finished = true;
    });

    _jobs.push_back(std::move(job));
}

void VocalAIDocumentController::scheduleRender(VocalAIAudioModification* modification) {
    auto* source = static_cast<VocalAIAudioSource*>(modification->getAudioSource());
    const auto analysis = source->analysis();
    if (!analysis || !analysis->valid) return;

    std::lock_guard<std::mutex> lock(_jobMutex);
    for (const auto& job : _jobs)
        if (job->modification == modification && job->kind == Job::Kind::Render) return;

    auto job = std::make_unique<Job>();
    job->kind = Job::Kind::Render;
    job->source = source;
    job->modification = modification;
    Job* raw = job.get();
    const TuningSettings settings = modification->settings();

    job->thread = std::thread([raw, analysis, settings]() {
        auto render = std::make_shared<ModificationRender>();
        // Work on a copy: the shared analysis must stay untouched, since other
        // modifications of the same source read it too.
        vocalai::TrackAnalysis work = analysis->track;
        vocalai::KeyEstimate key = work.key;
        if (!settings.key.empty()) {
            try {
                key = vocalai::key_from_name(settings.key, key.tuning_hz);
            } catch (const std::exception&) {
            }
        }
        if (settings.tuning_hz > 0.0) key.tuning_hz = settings.tuning_hz;

        const auto cfg = configFor(settings);
        const auto result = vocalai::tune(work.notes, work.pitch, key, cfg);
        render->notes = work.notes;
        render->tuning_hz = key.tuning_hz;
        render->key_name = key.name();

        const auto& channels = raw->source->samples();
        render->channels.resize(channels.size());
        for (std::size_t c = 0; c < channels.size(); ++c) {
            if (raw->cancelled.load()) {
                raw->finished = true;
                return;
            }
            std::vector<double> input(channels[c].begin(), channels[c].end());
            std::vector<double> tuned =
                settings.enabled ? vocalai::render_correction(work, result.ratio, &input) : input;
            render->channels[c].assign(tuned.begin(), tuned.end());
            raw->progress = static_cast<float>(c + 1) / static_cast<float>(channels.size());
        }
        render->valid = true;
        raw->renderResult = std::move(render);
        raw->succeeded = true;
        raw->finished = true;
    });

    _jobs.push_back(std::move(job));
}

void VocalAIDocumentController::pumpCompletedJobs() {
    std::vector<std::unique_ptr<Job>> completed;
    {
        std::lock_guard<std::mutex> lock(_jobMutex);
        for (auto& job : _jobs) {
            if (job->kind == Job::Kind::Analysis && !job->progressStarted) {
                job->progressStarted = true;
                notifyAudioSourceAnalysisProgressStarted(job->source);
            }
        }
        auto it = _jobs.begin();
        while (it != _jobs.end()) {
            if ((*it)->finished.load()) {
                completed.push_back(std::move(*it));
                it = _jobs.erase(it);
            } else {
                if ((*it)->kind == Job::Kind::Analysis)
                    notifyAudioSourceAnalysisProgressUpdated((*it)->source,
                                                             (*it)->progress.load());
                ++it;
            }
        }
    }

    for (auto& job : completed) {
        if (job->thread.joinable()) job->thread.join();

        if (job->kind == Job::Kind::Analysis) {
            if (job->progressStarted) notifyAudioSourceAnalysisProgressCompleted(job->source);
            if (job->succeeded && !job->cancelled.load()) {
                job->source->setAnalysis(job->analysisResult);
                notifyAudioSourceContentChanged(
                    // Analysis reads the recording; it does not alter its samples.
                    job->source, ARA::ContentUpdateScopes::notesAreAffected());
                // The notes are known; now produce the audio they imply.
                for (auto* modification : job->source->getAudioModifications())
                    scheduleRender(static_cast<VocalAIAudioModification*>(modification));
            }
        } else if (job->succeeded && !job->cancelled.load()) {
            job->modification->setRender(job->renderResult);
            notifyAudioModificationContentChanged(
                job->modification, ARA::ContentUpdateScopes::notesAreAffected() +
                                       ARA::ContentUpdateScopes::samplesAreAffected());
            for (auto* region : job->modification->getPlaybackRegions())
                notifyPlaybackRegionContentChanged(
                    region, ARA::ContentUpdateScopes::notesAreAffected() +
                                ARA::ContentUpdateScopes::samplesAreAffected());
        }
    }
}

void VocalAIDocumentController::cancelJobsFor(const ARA::PlugIn::AudioSource* audioSource) {
    std::vector<std::unique_ptr<Job>> doomed;
    {
        std::lock_guard<std::mutex> lock(_jobMutex);
        auto it = _jobs.begin();
        while (it != _jobs.end()) {
            if ((*it)->source == audioSource) {
                (*it)->cancelled = true;
                doomed.push_back(std::move(*it));
                it = _jobs.erase(it);
            } else {
                ++it;
            }
        }
    }
    doomed.clear();  // joins each worker in ~Job
}

void VocalAIDocumentController::updateTuningSettings(VocalAIAudioModification* modification,
                                                     const TuningSettings& settings) {
    if (!modification->setSettings(settings)) return;
    scheduleRender(modification);
}

/*******************************************************************************/

void VocalAIPlaybackRenderer::enableRendering(ARA::ARASampleRate sampleRate,
                                              ARA::ARAChannelCount channelCount,
                                              ARA::ARASampleCount maxSamplesToRender) noexcept {
    _sampleRate = sampleRate;
    _channelCount = channelCount;
    _maxSamplesToRender = maxSamplesToRender;
}

void VocalAIPlaybackRenderer::disableRendering() noexcept {}

void VocalAIPlaybackRenderer::renderPlaybackRegions(float* const* outputs,
                                                    ARA::ARASamplePosition samplePosition,
                                                    ARA::ARASampleCount samplesToRender,
                                                    bool isPlayingBack) {
    for (auto c = 0; c < _channelCount; ++c)
        std::memset(outputs[c], 0, sizeof(float) * static_cast<std::size_t>(samplesToRender));
    if (!isPlayingBack) return;

    auto* controller = getDocumentController<VocalAIDocumentController>();
    if (!controller->rendererWillAccessModelGraph(this)) {
        // The count is taken before access is granted or refused, so it has to
        // be released either way; leaking it would stall the next edit forever.
        controller->rendererDidAccessModelGraph(this);
        return;
    }

    const auto sampleEnd = samplePosition + samplesToRender;
    for (const auto& region : getPlaybackRegions()) {
        auto* modification = static_cast<VocalAIAudioModification*>(region->getAudioModification());
        auto* source = static_cast<const VocalAIAudioSource*>(modification->getAudioSource());
        if (source->getSampleRate() != _sampleRate) continue;

        const auto render = modification->render();
        if (!render || !render->valid || render->channels.empty()) continue;

        const auto regionStart = region->getStartInPlaybackSamples(_sampleRate);
        const auto regionEnd = region->getEndInPlaybackSamples(_sampleRate);
        if (sampleEnd <= regionStart || regionEnd <= samplePosition) continue;

        // Map song time to a position inside the modification's render.
        const auto offsetToRegion = region->getStartInAudioModificationSamples() - regionStart;
        const auto available = static_cast<ARA::ARASamplePosition>(render->channels[0].size());
        auto start = std::max(regionStart, samplePosition);
        auto end = std::min(regionEnd, sampleEnd);
        start = std::max(start, std::max<ARA::ARASamplePosition>(
                                    0, region->getStartInAudioModificationSamples()) -
                                    offsetToRegion);
        end = std::min(end, std::min(available, region->getEndInAudioModificationSamples()) -
                                offsetToRegion);
        if (end <= start) continue;

        const auto sourceChannels = static_cast<ARA::ARAChannelCount>(render->channels.size());
        for (auto pos = start; pos < end; ++pos) {
            const auto inBuffer = static_cast<std::size_t>(pos - samplePosition);
            const auto inSource = static_cast<std::size_t>(pos + offsetToRegion);
            if (sourceChannels == _channelCount) {
                for (auto c = 0; c < sourceChannels; ++c)
                    outputs[c][inBuffer] +=
                        render->channels[static_cast<std::size_t>(c)][inSource];
            } else {
                float monoSum = 0.0f;
                for (auto c = 0; c < sourceChannels; ++c)
                    monoSum += render->channels[static_cast<std::size_t>(c)][inSource];
                if (sourceChannels > 1) monoSum /= static_cast<float>(sourceChannels);
                for (auto c = 0; c < _channelCount; ++c) outputs[c][inBuffer] += monoSum;
            }
        }
    }
    controller->rendererDidAccessModelGraph(this);
}

}  // namespace vocalai_ara
