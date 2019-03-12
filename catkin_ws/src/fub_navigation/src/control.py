#!/usr/bin/env python2
import math
import numpy as np
import rospkg
import rospy
from autominy_msgs.msg import NormalizedSteeringCommand, NormalizedSpeedCommand
from geometry_msgs.msg import PointStamped
from nav_msgs.msg import Odometry
from std_msgs.msg import Float32
from tf.transformations import euler_from_quaternion


class VectorfieldController:
    def __init__(self):
        rospy.init_node('VectorfieldController')
        self.map_size_x = 600  # cm
        self.map_size_y = 430  # cm
        self.resolution = 1  # cm
        self.lane = 2
        self.speed_value = 0.3
        self.last_angle = -1.0
        self.Kp = 2.0
        self.Kd = 0.4
        self.Ki = 0.0
        self.last_time = rospy.Time.now()
        self.integral_error = 0.0

        print("speed", self.speed_value)
        rospack = rospkg.RosPack()
        self.file_path = rospack.get_path('fub_navigation') + '/src/'
        if self.lane == 1:
            self.matrix = np.load(self.file_path + 'matrix25cm_lane1.npy')
        else:
            self.matrix = np.load(self.file_path + 'matrix25cm_lane2.npy')

        self.pub_speed = rospy.Publisher("/control/command/normalized_wanted_speed", NormalizedSpeedCommand,
                                         queue_size=100, latch=True)
        rospy.on_shutdown(self.shutdown)

        self.shutdown_ = False
        self.pub = rospy.Publisher("/control/command/normalized_wanted_steering", NormalizedSteeringCommand,
                                   queue_size=1)
        self.pub_yaw = rospy.Publisher("/desired_yaw", Float32, queue_size=100, latch=True)
        self.sub_odom = rospy.Subscriber("/localization/odometry/filtered_map", Odometry, self.callback, queue_size=1)

    def callback(self, data):
        dt = (data.header.stamp - self.last_time).to_sec()
        self.last_time = data.header.stamp
        x = data.pose.pose.position.x
        y = data.pose.pose.position.y
        orientation_q = data.pose.pose.orientation
        orientation_list = [orientation_q.x, orientation_q.y, orientation_q.z, orientation_q.w]
        (roll, pitch, yaw) = euler_from_quaternion(orientation_list)

        x_index_floor = int(math.floor(x * (100.0 / self.resolution)))
        y_index_floor = int(math.floor(y * (100.0 / self.resolution)))

        x_index_ceil = x_index_floor + 1
        y_index_ceil = y_index_floor + 1

        ceil_ratio_x = x * (100.0 / self.resolution) - x_index_floor
        ceil_ratio_y = y * (100.0 / self.resolution) - y_index_floor

        if x_index_floor < 0:
            x_index_floor = 0
        if x_index_floor > self.map_size_x / self.resolution - 1:
            x_index_floor = self.map_size_x / self.resolution - 1

        if y_index_floor < 0:
            y_index_floor = 0
        if y_index_floor > self.map_size_y / self.resolution - 1:
            y_index_floor = self.map_size_y / self.resolution - 1

        if x_index_ceil < 0:
            x_index_ceil = 0
        if x_index_ceil > self.map_size_x / self.resolution - 1:
            x_index_ceil = self.map_size_x / self.resolution - 1

        if y_index_ceil < 0:
            y_index_ceil = 0
        if y_index_ceil > self.map_size_y / self.resolution - 1:
            y_index_ceil = self.map_size_y / self.resolution - 1

        x3_floor, y3_floor = self.matrix[x_index_floor, y_index_floor, :]
        x3_ceil, y3_ceil = self.matrix[x_index_ceil, y_index_ceil, :]
        x3 = x3_floor * (1.0 - ceil_ratio_x) + x3_ceil * ceil_ratio_x
        y3 = y3_floor * (1.0 - ceil_ratio_y) + y3_ceil * ceil_ratio_y
        f_x = np.cos(yaw) * x3 + np.sin(yaw) * y3
        f_y = -np.sin(yaw) * x3 + np.cos(yaw) * y3

        angle = np.arctan2(f_y, f_x)
        if self.last_angle < 0:
            self.last_angle = angle

        self.integral_error = self.integral_error + angle * dt
        steering = self.Kp * angle + self.Kd * ((angle - self.last_angle) / dt) + self.Ki * self.integral_error
        self.last_angle = angle
        yaw = np.arctan2(f_y, f_x)
        self.pub_yaw.publish(Float32(yaw))

        if f_x > 0:
            speed = -self.speed_value
        else:
            speed = self.speed_value
            if f_y > 0:
                steering = -np.pi / 2
            if f_y < 0:
                steering = np.pi / 2

        if steering > np.pi / 2:
            steering = np.pi / 2

        if steering < - np.pi / 2:
            steering = -np.pi / 2
        if f_x > 0:
            speed = max(self.speed_value, speed * ((np.pi / 3) / (abs(steering) + 1)))

        # print(steering)

        steerMsg = NormalizedSteeringCommand()
        steerMsg.value = steering
        self.pub.publish(steerMsg)
        if not self.shutdown_:
            msg = NormalizedSpeedCommand()
            msg.value = speed
            self.pub_speed.publish(msg)

    def shutdown(self):
        print("shutdown!")
        self.shutdown_ = True
        msg = NormalizedSpeedCommand()
        msg.value = 0
        self.pub_speed.publish(msg)
        rospy.sleep(1)


def main():
    try:
        VectorfieldController()
        rospy.spin()
    except rospy.ROSInterruptException:
        rospy.loginfo("VectorfieldController node terminated.")


if __name__ == '__main__':
    main()
