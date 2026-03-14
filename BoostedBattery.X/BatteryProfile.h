#ifndef BATTERYPROFILE_H
#define BATTERYPROFILE_H

#include <stdint.h>
#include <stdbool.h>

#define BATTERY_PROFILE_HISTORY_COUNT           3
#define BATTERY_PROFILE_SOC_AVG_COUNT           10  /* number of samples for SOC rolling average */
#define BATTERY_FULL_CHARGE_SOC_THRESHOLD       95.0f
#define BATTERY_EMPTY_SOC_THRESHOLD             5.0f
#define BATTERY_EXTRAPOLATED_CAPACITY_FACTOR    1.05f


#ifdef __cplusplus
extern "C" {
#endif

/**
 * 
 * Battery voltage curve representation.
 * The array contains 11 entries, each corresponding to a 10% increment
 * of state of charge.  Values are in millivolts and must be strictly
 * increasing.
 *
 */
typedef struct {
    uint16_t battery_curve[11];
    uint32_t battery_historical_mAh[BATTERY_PROFILE_HISTORY_COUNT]; /* historical mAh capacity of the battery, used for reporting to the ESC */
    uint32_t battery_designed_capacity_mAh; /* designed capacity of the battery, used for reporting of health over time */
} BatteryProfile;

/**
 * Estimate the state of charge (0.0-100.0) from a pack voltage using the
 * provided profile.  Linear interpolation is used between the 10% entries.
 *
 * @param profile pointer to the profile data
 * @param voltage pack voltage in millivolts
 * @return estimated state-of-charge, 0.0 to 100.0
 */
float BatteryProfile_GetSOCFromVoltage(const BatteryProfile *profile, uint16_t voltage);

/**
 * Estimate the state of charge (0.0-100.0) from the pack mAH consumed
 * using the series of historical data points collected representing
 * the overall pack capacity.
 *
 * @param profile pointer to the profile data
 * @param consumed_mAH number of milliamp-hours consumed from the pack, derived from coulomb counting
 * @return estimated state-of-charge, 0.0 to 100.0
 */
float BatteryProfile_GetSOCFromHistoricalCapacity(const BatteryProfile *profile, uint32_t consumed_mAH);

/**
 * Periodically called helper that maintains a rolling history of pack
 * capacity derived from coulomb-counted consumption.  The process works
 * as follows:
 *
 *   * when called with a voltage high enough that the voltage-based SOC of
 *     the highest cell in the pack is >= 95% (average over the last 
 *     BATTERY_PROFILE_SOC_AVG_COUNT samples)
 *     and the pack current is within ±pack_current_idle, the algorithm
 *     enters a capture state and remembers the current consumed_mAH value.
 *   * while in capture state, calls continue doing nothing until the
 *     averaged SOC of the lowest cell falls to <= 5% and the averaged current
 *     is within ±pack_current_idle.
 *   * once the end condition is met, the algorithm computes the amount of
 *     mAh used since the start of the capture, multiplies by 1.05 to
 *     slightly over‑estimate the pack capacity, shifts existing history
 *     entries down and stores the new capacity in
 *     battery_historical_mAh[0], then exits capture state.
 *
 * The caller should invoke this function regularly (e.g. once per second)
 * passing the latest observed pack voltage, total consumed mAh, and
 * instantaneous pack current.
 */
void BatteryProfile_UpdateCapacity(BatteryProfile *profile,
                                  uint32_t consumed_mAH,
                                  uint16_t voltage_lowest_cell,
                                  uint16_t voltage_highest_cell,
                                  float pack_current,
                                  uint32_t pack_current_idle);

/**
 * Convert a state‑of‑charge percentage into an estimated mAh consumed
 * using the average of the stored historical capacity values.  If there
 * is no history, zero is returned.
 *
 * @param profile profile containing history
 * @param soc fractional percentage 0.0..100.0
 * @return estimated mAh consumed so far
 */
uint32_t BatteryProfile_ConsumedFromSOC(const BatteryProfile *profile,
                                        float soc);

#ifdef __cplusplus
}
#endif

#endif /* BATTERYPROFILE_H */
