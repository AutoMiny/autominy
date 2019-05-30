#!/usr/bin/env python
from EmergencyStopUtils import first_forward_emergency_stop_evaluation, calculate_break_distance, \
    second_forward_emergency_stop_evaluation, move_forward, get_break_distance, move_backward

PACKAGE = "emergency_stop"


class EmergencyStop:
    _emergencyStopConfig = None
    _currentSpeed = 0.0
    _wantedSpeed = 0
    _emergencyStop = True

    def __init__(self):
        pass

    def set_config(self, emergency_stop_config):
        self._emergencyStopConfig = emergency_stop_config

    def set_current_speed(self, speed):
        self._currentSpeed = speed

    def set_wanted_speed(self, speed):
        self._wantedSpeed = speed

    def get_safe_speed(self):
        # code
        return None

    def check_emergency_stop(self, laser_scan):

        config = self._emergencyStopConfig
        current_speed = self._currentSpeed
        break_distance = get_break_distance(config, current_speed)

        plan_is_to_move_forward = self._wantedSpeed >= 0

        if plan_is_to_move_forward:
            emergency_stop_is_necessary = move_forward(config, laser_scan, break_distance)
            if emergency_stop_is_necessary:
                self._emergencyStop = True
                return

        else:
            emergency_stop_is_necessary = move_backward(config, laser_scan, break_distance)

            if emergency_stop_is_necessary:
                self._emergencyStop = True
                return

        self._emergencyStop = False
