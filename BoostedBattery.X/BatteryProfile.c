#include "BatteryProfile.h"
#include "stdlib.h"
#include "main.h"
#include "stdbool.h"
#include "Serial.h"

volatile bool battery_profile_capturing = false;
/**
 * Perform a state-of-charge lookup from voltage using a piecewise-linear
 * approximation defined by the 11-point curve stored in the profile.
 */
float BatteryProfile_GetSOCFromVoltage(const BatteryProfile *profile, uint16_t voltage)
{
    if (profile == NULL) {
        return 0.0f;
    }

    const uint16_t *curve = profile->battery_curve;

    /* handle boundaries */
    if (voltage <= curve[0]) {
        return 0.0f;
    }
    if (voltage >= curve[10]) {
        return 100.0f;
    }

    /* find segment that contains the voltage */
    for (int i = 0; i < 10; ++i) {
        uint16_t v0 = curve[i];
        uint16_t v1 = curve[i + 1];
        if (voltage >= v0 && voltage <= v1) {
            /* percent at start of segment */
            float base_percentage = (float)i * 10.0f;
            float span = (float)(v1 - v0);
            if (span == 0) {
                return base_percentage;
            }
            float frac = (float)(voltage - v0) / span;
            return base_percentage + frac * 10.0f;
        }
    }

    /* shouldn't get here, but return 0 to be safe */
    return 0.0f;
}


/**
 * Perform a state-of-charge calculation from the currently consumed mAH using the historical capacity data stored in the profile.
 */
float BatteryProfile_GetSOCFromHistoricalCapacity(const BatteryProfile *profile, uint32_t consumed_mAH)
{
    if (profile == NULL) {
        return 0.0f;
    }

    const uint16_t *curve = profile->battery_curve;

    /* find segment that contains the voltage */
    uint64_t historical_mAh_capacity_average = 0;
    uint32_t valid_history_count = 0;
    for (int i = 0; i < BATTERY_PROFILE_HISTORY_COUNT; ++i) {
        if(profile->battery_historical_mAh[i] > 0){
            historical_mAh_capacity_average += profile->battery_historical_mAh[i];
            valid_history_count++;
        }
    }

    if(valid_history_count > 0){
        historical_mAh_capacity_average /= valid_history_count;
        if(consumed_mAH >= historical_mAh_capacity_average){
            return 0.0f;
        }
        else{
            float soc = (1.0f - ((float)consumed_mAH / (float)historical_mAh_capacity_average)) * 100.0f;
            if(soc > 100.0f) soc = 100.0f;
            return soc;
        }
    }

    /* shouldn't get here, but return 0 to be safe */
    return 0.0f;
}


/*
 * Periodic update used to capture an estimate of the true pack capacity
 * based on coulomb-counted mAh consumed.  See header comment for details.
 */
void BatteryProfile_UpdateCapacity(BatteryProfile *profile,
                                  uint32_t consumed_mAH,
                                  uint16_t voltage_lowest_cell,
                                  uint16_t voltage_highest_cell,
                                  float pack_current,
                                  uint32_t pack_current_idle)
{
    static uint32_t start_consumed = 0;

    /* rolling buffers for averaging */
    static float soc_buf_low[BATTERY_PROFILE_SOC_AVG_COUNT];
    static float soc_buf_high[BATTERY_PROFILE_SOC_AVG_COUNT];
    static float current_buf[BATTERY_PROFILE_SOC_AVG_COUNT];
    static uint8_t buf_index = 0;
    static uint8_t buf_count = 0;

    if (profile == NULL) {
        return;
    }

    /* compute instantaneous SOC from voltage for lowest and highest cells */
    float soc_lowest = BatteryProfile_GetSOCFromVoltage(profile, voltage_lowest_cell);
    float soc_highest = BatteryProfile_GetSOCFromVoltage(profile, voltage_highest_cell);

    /* update circular buffers and compute averages */
    soc_buf_low[buf_index] = soc_lowest;
    soc_buf_high[buf_index] = soc_highest;
    current_buf[buf_index] = pack_current;
    buf_index = (buf_index + 1) % BATTERY_PROFILE_SOC_AVG_COUNT;
    if (buf_count < BATTERY_PROFILE_SOC_AVG_COUNT) {
        buf_count++;
    }

    float soc_avg_low = 0.0f;
    float soc_avg_high = 0.0f;
    float curr_avg = 0.0f;
    for (uint8_t i = 0; i < buf_count; ++i) {
        soc_avg_low += soc_buf_low[i];
        soc_avg_high += soc_buf_high[i];
        curr_avg += current_buf[i];
    }
    if (buf_count > 0) {
        soc_avg_low /= buf_count;
        soc_avg_high /= buf_count;
        curr_avg /= buf_count;
    }

    if (!battery_profile_capturing) {
        if (soc_avg_high >= BATTERY_FULL_CHARGE_SOC_THRESHOLD &&
            curr_avg < (float)pack_current_idle &&
            curr_avg > -(float)pack_current_idle) {
            battery_profile_capturing = true;
            Serial_println("Enable Capturing of Pack Capacity");
            start_consumed = consumed_mAH;
        }
    } else {
        if (soc_avg_low <= BATTERY_EMPTY_SOC_THRESHOLD &&
            curr_avg < (float)pack_current_idle &&
            curr_avg > -(float)pack_current_idle) {
            uint32_t used = consumed_mAH - start_consumed;
            uint32_t estimate = (uint32_t)((float)used * 1.05f + 0.5f);

            /* shift history array down and insert new value at index 0 */
            for (int i = BATTERY_PROFILE_HISTORY_COUNT - 1; i > 0; --i) {
                profile->battery_historical_mAh[i] =
                    profile->battery_historical_mAh[i - 1];
            }
            profile->battery_historical_mAh[0] = estimate;

            battery_profile_capturing = false; /* ready for next full‑cycle capture */
        }
    }
}


/**
 * Convert SOC percentage to consumed mAh using the average capacity from
 * the stored history.  If no history is available, returns 0.
 */
uint32_t BatteryProfile_ConsumedFromSOC(const BatteryProfile *profile,
                                        float soc)
{
    if (profile == NULL) {
        return 0u;
    }

    uint64_t total = 0;
    uint32_t count = 0;
    for (int i = 0; i < BATTERY_PROFILE_HISTORY_COUNT; ++i) {
        uint32_t val = profile->battery_historical_mAh[i];
        if (val > 0) {
            total += val;
            count++;
        }
    }
    if (count == 0) {
        return 0u;
    }

    uint32_t avg_capacity = (uint32_t)(total / count);
    if (soc <= 0.0f) {
        return avg_capacity;
    }
    if (soc >= 100.0f) {
        return 0u;
    }

    float remaining = soc / 100.0f;
    float consumed = (1.0f - remaining) * (float)avg_capacity;
    return (uint32_t)(consumed + 0.5f);
}
