#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/log.h"

#include <cstdio>

namespace gui::common
{

static constexpr const char* TAG = "ChannelTelemetry";

ChannelTelemetryViewModel::ChannelTelemetryViewModel()
{
    clear();
}

void ChannelTelemetryViewModel::setUpdateCallback(UpdateCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    updateCallback_ = callback;
}

void ChannelTelemetryViewModel::updateFromBatchTelemetry(const midi2pwm::pwm::BatchTelemetry &batch)
{
    const auto *channelEntries = batch.channels();
    if (!channelEntries) {
        GUI_LOG_WARNING(TAG, "BatchTelemetry has no channels");
        return;
    }

    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto *telemetry : *channelEntries) {
            if (!telemetry) {
                continue;
            }

            const std::uint16_t channelNumber = telemetry->channel_number();
            if (channelNumber >= CHANNEL_COUNT) {
                GUI_LOG_ERROR(TAG, "Invalid channel number: %u", channelNumber);
                continue;
            }

            ChannelData &channelData = channels_[channelNumber];

            float newVoltage = telemetry->voltage();
            float newCurrentMa = telemetry->current() * 1000.0F;
            bool newIsActive = (telemetry->status() == midi2pwm::pwm::ChannelStatus::Active);
            auto newFault = statusToString(telemetry->status(), telemetry->had_fault());
            float newDutyCycle = telemetry->duty_cycle() * 100.0F;
            int newPolarity = static_cast<int>(telemetry->polarity());

            if (channelData.voltage != newVoltage
                || channelData.currentMa != newCurrentMa
                || channelData.isActive != newIsActive
                || channelData.fault != newFault
                || channelData.dutyCyclePercent != newDutyCycle
                || channelData.polarity != newPolarity) {

                channelData.voltage = newVoltage;
                channelData.currentMa = newCurrentMa;
                channelData.isActive = newIsActive;
                channelData.fault = newFault;
                channelData.dutyCyclePercent = newDutyCycle;
                channelData.polarity = newPolarity;
                channelDirty_[channelNumber] = true;
            }
        }

        float newInputVoltage = batch.input_voltage();
        float totalMa = 0.0f;
        for (const auto &ch : channels_) {
            totalMa += ch.currentMa;
        }
        float newTotalCurrentAmps = totalMa / 1000.0f;

        if (inputVoltage_ != newInputVoltage || totalCurrentAmps_ != newTotalCurrentAmps) {
            inputVoltage_ = newInputVoltage;
            totalCurrentAmps_ = newTotalCurrentAmps;
            globalDirty_ = true;
        }

        callback = updateCallback_;
    }

    version_.fetch_add(1, std::memory_order_release);

    if (callback.is_valid()) {
        callback(channels_);
    }
}

void ChannelTelemetryViewModel::applyConfigToChannel(const midi2pwm::pwm::ChannelConfig &config)
{
    const std::uint16_t channelNumber = config.channel_number();

    if (channelNumber >= CHANNEL_COUNT) {
        GUI_LOG_ERROR(TAG, "Invalid channel number in config: %u", channelNumber);
        return;
    }

    ChannelData &channelData = channels_[channelNumber];
    channelData.note = midiNoteToString(config.note());
    channelData.noteNumber = static_cast<int>(config.note());
    channelData.mode_type = static_cast<std::uint8_t>(config.output_mode());
    channelData.polarity = static_cast<int>(config.polarity());
    channelData.release_action = static_cast<int>(config.release_action());

    const auto modeParamsType = config.mode_params_type();
    GUI_LOG_INFO(TAG, "Config ch=%u: mode=%u params_type=%u note=%u",
                 channelNumber, static_cast<unsigned>(config.output_mode()),
                 static_cast<unsigned>(modeParamsType), config.note());

    if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::InstantModeParams) {
        const auto *p = config.mode_params_as_InstantModeParams();
        if (p) {
            channelData.instant_data.on_level = deviceToUiPercent(p->on_level());
            channelData.instant_data.velocity_sensitive = p->velocity_sensitive();
            GUI_LOG_INFO(TAG, "Instant ch=%u: on_level=%.1f%% vel=%d",
                         channelNumber, channelData.instant_data.on_level,
                         channelData.instant_data.velocity_sensitive);
        }
    } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::RampedModeParams) {
        const auto *p = config.mode_params_as_RampedModeParams();
        if (p) {
            channelData.ramped_data.on_level = deviceToUiPercent(p->on_level());
            channelData.ramped_data.velocity_sensitive = p->velocity_sensitive();
            channelData.ramped_data.attack_time_ms = p->attack_time_ms();
            channelData.ramped_data.release_time_ms = p->release_time_ms();
        }
    } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::PulseModeParams) {
        const auto *p = config.mode_params_as_PulseModeParams();
        if (p) {
            channelData.pulse_data.on_level = deviceToUiPercent(p->on_level());
            channelData.pulse_data.velocity_sensitive = p->velocity_sensitive();
            channelData.pulse_data.attack_time_ms = p->attack_time_ms();
            channelData.pulse_data.hold_time_ms = p->hold_time_ms();
            channelData.pulse_data.release_time_ms = p->release_time_ms();
        }
    } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::ToggleModeParams) {
        const auto *p = config.mode_params_as_ToggleModeParams();
        if (p) {
            channelData.toggle_data.on_level = deviceToUiPercent(p->on_level());
            channelData.toggle_data.velocity_sensitive = p->velocity_sensitive();
            channelData.toggle_data.debounce_delay_ms = p->debounce_delay_ms();
        }
    } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::ADSRModeParams) {
        const auto *p = config.mode_params_as_ADSRModeParams();
        if (p) {
            channelData.adsr_data.attack_level = deviceToUiPercent(p->attack_level());
            channelData.adsr_data.sustain_level = deviceToUiPercent(p->sustain_level());
            channelData.adsr_data.velocity_sensitive = p->velocity_sensitive();
            channelData.adsr_data.attack_time_ms = p->attack_time_ms();
            channelData.adsr_data.decay_time_ms = p->decay_time_ms();
            channelData.adsr_data.release_time_ms = p->release_time_ms();
            GUI_LOG_INFO(TAG, "ADSR ch=%u: atk_lvl=%.1f%% sus=%.1f%% atk=%u dec=%u rel=%u",
                         channelNumber, channelData.adsr_data.attack_level,
                         channelData.adsr_data.sustain_level,
                         channelData.adsr_data.attack_time_ms,
                         channelData.adsr_data.decay_time_ms,
                         channelData.adsr_data.release_time_ms);
        } else {
            GUI_LOG_WARNING(TAG, "ADSR params null for ch=%u", channelNumber);
        }
    } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::CCControlModeParams) {
        const auto *p = config.mode_params_as_CCControlModeParams();
        if (p) {
            channelData.cc_data.cc_number = p->cc_number();
            channelData.cc_data.center_value = p->center_value();
            channelData.cc_data.left_max_pwm = deviceToUiPercent(p->left_max_pwm());
            channelData.cc_data.right_max_pwm = deviceToUiPercent(p->right_max_pwm());
            channelData.cc_data.deadband_range = p->deadband_range();
        }
    } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::PitchBendModeParams) {
        const auto *p = config.mode_params_as_PitchBendModeParams();
        if (p) {
            channelData.pitchbend_data.base_level = deviceToUiPercent(p->base_level());
            channelData.pitchbend_data.bend_range = deviceToUiPercent(p->bend_range());
            channelData.pitchbend_data.unipolar = p->unipolar();
            channelData.pitchbend_data.velocity_sensitive = p->velocity_sensitive();
        }
    }

    channelDirty_[channelNumber] = true;

    GUI_LOG_DEBUG(TAG, "Config updated for channel %u: mode=%u, note=%s",
                  channelNumber, channelData.mode_type, channelData.note.c_str());
}

void ChannelTelemetryViewModel::updateFromConfig(const midi2pwm::pwm::ChannelConfig &config)
{
    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        applyConfigToChannel(config);
        callback = updateCallback_;
    }

    version_.fetch_add(1, std::memory_order_release);

    if (callback.is_valid()) {
        callback(channels_);
    }
}

void ChannelTelemetryViewModel::updateFromBatchConfig(const midi2pwm::pwm::BatchConfig &batch)
{
    const auto *channelEntries = batch.channels();
    if (!channelEntries) {
        GUI_LOG_WARNING(TAG, "BatchConfig has no channels");
        return;
    }

    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto *config : *channelEntries) {
            if (!config) {
                continue;
            }
            applyConfigToChannel(*config);
        }

        callback = updateCallback_;
    }

    version_.fetch_add(1, std::memory_order_release);

    if (callback.is_valid()) {
        callback(channels_);
    }
}

void ChannelTelemetryViewModel::clear()
{
    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        channelDirty_.fill(true);
        globalDirty_ = true;

        for (auto &channelData : channels_) {
            channelData.note = "---";
            channelData.voltage = 0.0F;
            channelData.currentMa = 0.0F;
            channelData.dutyCyclePercent = 0.0F;
            channelData.isActive = false;
            channelData.fault = "";
            channelData.mode_type = 0;
            channelData.polarity = 0;
            channelData.release_action = 0;
            channelData.instant_data = {100.0F, true};
            channelData.ramped_data = {100.0F, true, 100, 100};
            channelData.pulse_data = {100.0F, true, 10, 100, 50};
            channelData.toggle_data = {100.0F, false, 50};
            channelData.adsr_data = {100.0F, deviceToUiPercent(200), true, 50, 100, 200};
            channelData.cc_data = {1, 64, 100.0F, 100.0F, 2};
            channelData.pitchbend_data = {deviceToUiPercent(128), deviceToUiPercent(64), false, true};
        }

        callback = updateCallback_;
    }

    version_.fetch_add(1, std::memory_order_release);

    if (callback.is_valid()) {
        callback(channels_);
    }
}

ChannelTelemetryViewModel::ChannelsArray ChannelTelemetryViewModel::channels() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_;
}

ChannelData ChannelTelemetryViewModel::channel(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_[index];
}

bool ChannelTelemetryViewModel::isChannelDirty(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return channelDirty_[index];
}

void ChannelTelemetryViewModel::clearChannelDirty(std::size_t index)
{
    std::lock_guard<std::mutex> lock(mutex_);
    channelDirty_[index] = false;
}

float ChannelTelemetryViewModel::inputVoltage() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return inputVoltage_;
}

float ChannelTelemetryViewModel::totalCurrentAmps() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return totalCurrentAmps_;
}

bool ChannelTelemetryViewModel::isGlobalDirty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return globalDirty_;
}

void ChannelTelemetryViewModel::clearGlobalDirty()
{
    std::lock_guard<std::mutex> lock(mutex_);
    globalDirty_ = false;
}

etl::string<16> ChannelTelemetryViewModel::midiNoteToString(std::uint16_t noteNumber)
{
    static constexpr const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    if (noteNumber > 127U) {
        return "---";
    }

    const int octave = static_cast<int>(noteNumber / 12U) - 1;
    const std::size_t semitone = noteNumber % 12U;

    etl::string<16> result;
    result.append(NOTE_NAMES[semitone]);

    char octaveBuffer[8];
    const int written = std::snprintf(octaveBuffer, sizeof(octaveBuffer), "%d", octave);
    if (written > 0) {
        octaveBuffer[sizeof(octaveBuffer) - 1] = '\0';
        result.append(octaveBuffer);
    }

    return result;
}

etl::string<32> ChannelTelemetryViewModel::statusToString(midi2pwm::pwm::ChannelStatus status, bool hadFault)
{
    if (status == midi2pwm::pwm::ChannelStatus::Fault) {
        return "FAULT";
    }

    if (hadFault) {
        return "HAD_FAULT";
    }

    switch (status) {
    case midi2pwm::pwm::ChannelStatus::Active:
        return "Active";
    case midi2pwm::pwm::ChannelStatus::Inactive:
        return "Inactive";
    default:
        return "";
    }
}

} // namespace gui::common
