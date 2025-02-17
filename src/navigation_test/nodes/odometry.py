#!/usr/bin/env python

import roslib
roslib.load_manifest('navigation_test')

import rospy
from nav_msgs.msg import Odometry
from geometry_msgs.msg import Twist
from geometry_msgs.msg import PoseWithCovarianceStamped
from geometry_msgs.msg import PoseStamped
from sensor_msgs.msg import Imu
import tf
import math

speed_x = 0.0
speed_r = 0.0
distance_x = 0.0
distance_y = 0.0
direction_z = 0.0
direction_z_q0 = 0.0
direction_z_q1 = 0.0
direction_z_q2 = 0.0
direction_z_q3 = 1.0
t = rospy.Time()

# get twist value and integrate
def callback(msg):
    global t
    global distance_x
    global distance_y
    global speed_x
    global speed_r
    global direction_z_q0
    global direction_z_q1
    global direction_z_q2
    global direction_z_q3
    global direction_z
    time = rospy.Time.now()
    speed_x = msg.linear.x
    speed_r = msg.angular.z
    if t.secs == 0:
        dt = rospy.Time(0)
    else:
        dt = time - t
    t = time
    direction_z += speed_r *dt.secs + speed_r * dt.nsecs/1000000000
    distance_x +=  math.cos(direction_z)*(speed_x * dt.secs + speed_x * dt.nsecs/1000000000)
    distance_y +=  math.sin(direction_z)*(speed_x * dt.secs + speed_x * dt.nsecs/1000000000)

    quat = tf.transformations.quaternion_from_euler(0, 0, direction_z)
    direction_z_q0 = quat[0]
    direction_z_q1 = quat[1]
    direction_z_q2 = quat[2]
    direction_z_q3 = quat[3]
      
# when set new goal, reset time parameter
def callback_newgoal(data):
    global t
    
    t = rospy.Time()


# set initial pose
def callback_initial(data):
    global distance_x
    global distance_y
    global speed_x
    global speed_r
    global direction_z_q0
    global direction_z_q1
    global direction_z_q2
    global direction_z_q3


    distance_x = data.pose.pose.position.x
    distance_y = data.pose.pose.position.y
    direction_z_q0 = data.pose.pose.orientation.x
    direction_z_q1 = data.pose.pose.orientation.y
    direction_z_q2 = data.pose.pose.orientation.z
    direction_z_q3 = data.pose.pose.orientation.w


# publish odometry_msg
def odometry_talker():
    global distance_x
    global distance_y
    global speed_x
    global speed_r
    global direction_z_q0
    global direction_z_q1
    global direction_z_q2
    global direction_z_q3

    rospy.Subscriber('/navigation_velocity_smoother/raw_cmd_vel', Twist, callback)  
    rospy.Subscriber('/initialpose', PoseWithCovarianceStamped, callback_initial)
    rospy.Subscriber('/move_base_simple/goal', PoseStamped, callback_newgoal)
    br = tf.TransformBroadcaster()
    Odom = Odometry()
    pub_odom = rospy.Publisher('odom', Odometry, queue_size =5)

    while not rospy.is_shutdown(): 
	br.sendTransform((distance_x, distance_y, 0),
                         (direction_z_q0, direction_z_q1, direction_z_q2, direction_z_q3),
                         rospy.Time.now(),
                         "base_footprint",
                         "odom")

	Odom.header.stamp = rospy.Time.now()
    	Odom.header.frame_id = "odom"
    	Odom.child_frame_id = "base_footprint"
    	Odom.pose.pose.position.x = distance_x 
    	Odom.pose.pose.position.y = distance_y
    	Odom.pose.pose.position.z = 0.0
    	Odom.twist.twist.linear.x = speed_x
    	Odom.twist.twist.angular.z = speed_r
    	Odom.pose.pose.orientation.x = direction_z_q0
    	Odom.pose.pose.orientation.y = direction_z_q1
    	Odom.pose.pose.orientation.z = direction_z_q2
    	Odom.pose.pose.orientation.w = direction_z_q3
	pub_odom.publish(Odom)

    rospy.spin()


    

if __name__ == '__main__':
    try:
        rospy.init_node('odom_talker')
        odometry_talker()
    except rospy.ROSInterruptException:
        pass
        
