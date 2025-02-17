#!/usr/bin/env python

## Simple myo demo that listens to std_msgs/UInt8 poses published 
## to the 'myo_gest' topic

import rospy, math
from std_msgs.msg import UInt8, String
from geometry_msgs.msg import Twist, Vector3

from sensor_msgs.msg import Imu
from ros_myo.msg import MyoArm, EmgArray

########## Data Enums ###########
# MyoArm.arm___________________ #
#    UNKNOWN        = 0 	#
#    RIGHT          = 1		#
#    Left           = 2		#
# MyoArm.xdir___________________#
#    UNKNOWN        = 0		#
#    X_TOWARD_WRIST = 1		#
#    X_TOWARD_ELBOW = 2		#
# myo_gest UInt8________________#
#    REST           = 0		#
#    FIST           = 1		#
#    WAVE_IN        = 2		#
#    WAVE_OUT       = 3		#
#    FINGERS_SPREAD = 4		#
#    THUMB_TO_PINKY = 5		#
#    UNKNOWN        = 255	#
#################################


if __name__ == '__main__':

    global armState
    global xDirState
    armState = 0;
    rospy.init_node('turtlesim_driver', anonymous=True)

    turtlesimPub = rospy.Publisher("directs", String, queue_size=10)
    tsPub = rospy.Publisher("turtle1/cmd_vel", Twist, queue_size=10)


    # set the global arm states
    def setArm(data):
        global armState
        global xDirState

        armState = data.arm
        xDirState = data.xdir
        rospy.sleep(2.0)


    # Use the calibrated Myo gestures to drive the turtle
    def drive(gest):
        if gest.data == 1:  # FIST
            turtlesimPub.publish("go back")
            tsPub.publish(Twist(Vector3(-1.0, 0, 0), Vector3(0, 0, 0)))
        elif gest.data == 2 and armState == 1:  # WAVE_IN, RIGHT arm
            turtlesimPub.publish("go left")
            tsPub.publish(Twist(Vector3(0, 0, 0), Vector3(0, 0, 1.0)))
        elif gest.data == 2 and armState == 2:  # WAVE_IN, LEFT arm
            turtlesimPub.publish("go right")
            tsPub.publish(Twist(Vector3(0, 0, 0), Vector3(0, 0, -1.0)))
        elif gest.data == 3 and armState == 1:  # WAVE_OUT, RIGHT arm
            turtlesimPub.publish("go right")
            tsPub.publish(Twist(Vector3(0, 0, 0), Vector3(0, 0, -1.0)))
        elif gest.data == 3 and armState == 2:  # WAVE_OUT, LEFT arm
            turtlesimPub.publish("go left")
            tsPub.publish(Twist(Vector3(0, 0, 0), Vector3(0, 0, 1.0)))
        elif gest.data == 4:  # FINGERS_SPREAD
            turtlesimPub.publish("go forward")
            tsPub.publish(Twist(Vector3(1.0, 0, 0), Vector3(0, 0, 0)))
    
    def strength(emgArr1):
        emgArr = emgArr1.data
        # Define proportional control constant:
        K = 0.005
        # Get the average muscle activation of the left, right, and all sides
        aveRight = (emgArr[0] + emgArr[1] + emgArr[2] + emgArr[3]) / 4
        aveLeft = (emgArr[4] + emgArr[5] + emgArr[6] + emgArr[7]) / 4
        ave = (aveLeft + aveRight) / 2
        # If all muscles activated, drive forward exponentially
        if ave > 500:
            tsPub.publish(Twist(Vector3(0.1 * math.exp(K * ave), 0, 0), Vector3(0, 0, 0)))
        # If only left muscles activated, rotate proportionally
        elif aveLeft > (aveRight + 200):
            tsPub.publish(Twist(Vector3(0, 0, 0), Vector3(0, 0, K * ave)))
        # If only right muscles activated, rotate proportionally
        elif aveRight > (aveLeft + 200):
            tsPub.publish(Twist(Vector3(0, 0, 0), Vector3(0, 0, -K * ave)))
        # If not very activated, don't move (high-pass filter)
        else:
            tsPub.publish(Twist(Vector3(0, 0, 0), Vector3(0, 0, 0)))


    # rospy.Subscriber("myo_emg", EmgArray, strength)

    rospy.Subscriber("myo_arm", MyoArm, setArm)
    rospy.Subscriber("myo_gest", UInt8, drive)
    # rospy.Subscriber("myo_imu", UInt8, imuFunc)
    rospy.loginfo('Please sync the Myo')

    # spin() simply keeps python from exiting until this node is stopped
    rospy.spin()
