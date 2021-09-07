/*
   VL53L5cx class library implementation

   Copyright (c) 2021 Simon D. Levy

   MIT License
 */

#include "VL53L5cx.h"
#include "Debugger.hpp"

#include "st/vl53l5cx_plugin_motion_indicator.h"
#include "st/vl53l5cx_plugin_xtalk.h"

VL53L5cx::VL53L5cx(
        uint8_t lpnPin,
        uint8_t deviceAddress,
        resolution_t resolution,
        target_order_t targetOrder,
        uint8_t rangingFrequency)
{
    _lpn_pin = lpnPin;

    _dev.platform.address = deviceAddress;

    _resolution = resolution;
    _target_order = targetOrder;

    _ranging_frequency = rangingFrequency;
}
        
void VL53L5cx::begin(void)
{
    init();
    start_ranging();
}

void VL53L5cx::init(void)
{
    // Bozo filter for ranging frequency
    check_ranging_frequency(RESOLUTION_4X4, 60, "4X4");
    check_ranging_frequency(RESOLUTION_8X8, 15, "8X8");

    // Reset the sensor by toggling the LPN pin
    Reset_Sensor(_lpn_pin);

    // Make sure there is a VL53L5CX sensor connected
    uint8_t isAlive = 0;
    uint8_t error = vl53l5cx_is_alive(&_dev, &isAlive);
    if(!isAlive || error) {
        Debugger::reportForever("VL53L5CX not detected at requested address");
    }

    // Init VL53L5CX sensor
    error = vl53l5cx_init(&_dev);
    if(error) {
        Debugger::reportForever("VL53L5CX ULD Loading failed");
    }

    // Set resolution
    vl53l5cx_set_resolution(&_dev,
            _resolution == RESOLUTION_4X4 ?
            VL53L5CX_RESOLUTION_4X4 :
            VL53L5CX_RESOLUTION_8X8);

    // Set target order
    vl53l5cx_set_target_order(&_dev,
            _target_order == TARGET_ORDER_STRONGEST ?
            VL53L5CX_TARGET_ORDER_STRONGEST :
            VL53L5CX_TARGET_ORDER_CLOSEST);

} // init

void VL53L5cx::check_ranging_frequency(resolution_t resolution,
                                       uint8_t maxval,
                                       const char *label)
{
    if (_ranging_frequency < 1 || _ranging_frequency > maxval) {
        Debugger::reportForever("Ranging frequency for %s resolution " 
                "must be at least 1 and no more than %d", maxval);
    }
}


void VL53L5cx::start_ranging(void)
{
    checkStatus(vl53l5cx_start_ranging(&_dev), "start error = 0x%02X");
}

bool VL53L5cx::isReady(void)
{
    uint8_t is_ready = false;

    vl53l5cx_check_data_ready(&_dev, &is_ready);

    if (is_ready) {
        vl53l5cx_get_ranging_data(&_dev, &_results);
        return true;
    }

    return false;
}

uint8_t VL53L5cx::getStreamCount(void)
{
    return _dev.streamcount;
}

uint8_t VL53L5cx::getTargetStatus(uint8_t zone, uint8_t target)
{
    return _results.target_status[VL53L5CX_NB_TARGET_PER_ZONE * zone + target];
}

uint8_t VL53L5cx::getDistance(uint8_t zone, uint8_t target)
{
    return _results.distance_mm[VL53L5CX_NB_TARGET_PER_ZONE * zone + target];
}

uint8_t VL53L5cx::getSignalPerSpad(uint8_t zone, uint8_t target)
{
    return _results.signal_per_spad[VL53L5CX_NB_TARGET_PER_ZONE * zone + target];
}

uint8_t VL53L5cx::getRangeSigma(uint8_t zone, uint8_t target)
{
    return _results.range_sigma_mm[VL53L5CX_NB_TARGET_PER_ZONE * zone + target];
}

uint8_t VL53L5cx::getNbTargetDetected(uint8_t zone)
{
    return _results.nb_target_detected[zone];
}

uint8_t VL53L5cx::getAmbientPerSpad(uint8_t zone)
{
    return _results.ambient_per_spad[zone];
}

uint8_t VL53L5cx::getNbSpadsEnabled(uint8_t zone)
{
    return _results.nb_spads_enabled[zone];
}

void VL53L5cx::stop(void)
{
    vl53l5cx_stop_ranging(&_dev);
}

uint32_t VL53L5cx::getIntegrationTimeMsec(void)
{
    uint32_t integration_time_ms = 0;
    checkStatus(
            vl53l5cx_get_integration_time_ms(&_dev, &integration_time_ms),
            "vl53l5cx_get_integration_time_ms failed, status %u\n");
    return integration_time_ms;
}

void VL53L5cx::addMotionIndicator(uint16_t distanceMin, uint16_t distanceMax)
{
    // Create motion indicator
    VL53L5CX_Motion_Configuration motion_config = {};
    checkStatus(vl53l5cx_motion_indicator_init(&_dev, &motion_config, 
                _resolution == RESOLUTION_4X4 ?
                VL53L5CX_RESOLUTION_4X4 :
                VL53L5CX_RESOLUTION_8X8),
            "Motion indicator init failed with status : %u");

    if (distanceMin > 0 && distanceMax > 0) {

        bozoFilter(distanceMin < 400,
                "Motion indicator minimum distance must be at least 400mm");

        bozoFilter(distanceMax < distanceMin,
                "Motion indicator maximum distance must be greater than minimum");

        bozoFilter(distanceMax-distanceMin > 1500,
                "Motion indicator maximum distance can be no greater than 1500mm above minimum distance");

        checkStatus(vl53l5cx_motion_indicator_set_distance_motion( &_dev, &motion_config, distanceMin, distanceMax),
                   "Motion indicator set distance failed with status : %u\n");
    }
}

void VL53L5cx::calibrateXtalk(uint8_t reflectancePercent, uint8_t samples, uint16_t distance)
{
    rangeFilter(reflectancePercent, 1, 99,  "Reflectance perecent");
    rangeFilter(samples, 1, 16, "Number of samples");
    rangeFilter(distance, 600, 3000, "Distance");

    checkStatus(vl53l5cx_calibrate_xtalk(&_dev, reflectancePercent, samples, distance),
            "vl53l5cx_calibrate_xtalk failed, status %u");
}

void VL53L5cx::getXtalkCalibrationData(VL53L5cx::XtalkCalibrationData & data)
{
    vl53l5cx_get_caldata_xtalk(&_dev, data.data);
}

void VL53L5cx::setXtalkCalibrationData(VL53L5cx::XtalkCalibrationData & data)
{
}

VL53L5cxAutonomous::VL53L5cxAutonomous(
        uint8_t lpnPin,
        uint32_t integrationTimeMsec,
        uint8_t deviceAddress,
        resolution_t resolution,
        target_order_t targetOrder)
    : VL53L5cxAutonomous::VL53L5cx(
            lpnPin,
            deviceAddress,
            resolution,
            targetOrder)
{
    _integration_time_msec = integrationTimeMsec;
}

void VL53L5cxAutonomous::begin(void)
{
    init();

    // Set ranging mode autonomous  
    checkStatus(vl53l5cx_set_ranging_mode(&_dev, VL53L5CX_RANGING_MODE_AUTONOMOUS),
            "vl53l5cx_set_ranging_mode failed, status %u\n");

    // Using autonomous mode, the integration time can be updated (not possible
    // using continuous)
    vl53l5cx_set_integration_time_ms(&_dev, _integration_time_msec);

    start_ranging();
}

void VL53L5cx::bozoFilter(bool cond, const char * msg)
{
    if (cond) {
        Debugger::reportForever(msg);
    }
}

void VL53L5cx::checkStatus(uint8_t error, const char * fmt)
{
    if (error) {
        Debugger::reportForever(fmt, error);
    }
}

void VL53L5cx::rangeFilter(uint16_t val, uint16_t minval, uint16_t maxval, const char * valname)
{
    if (val < minval || val > maxval) {
        Debugger::reportForever("%s must be between %d and %d", valname, minval, maxval);
    }
}
