#!/usr/bin/env python

import rospy

from geometry_msgs.msg import Twist
from speed_control.msg import Speed

import sys, select, termios, tty

msg = """
Control Wheelchair
---------------------------
Moving around:
   u    i    o
   j    k    l
   m    ,    .

q/z : increase/decrease max speeds by 10%
w/x : increase/decrease only linear speed by 10%
e/c : increase/decrease only angular speed by 10%
space key, k : force stop
anything else : stop smoothly

maximum speed : 10
minimum speed : -10

CTRL-C to quit
"""

moveBindings = {
        'i':(1,0),
        'o':(1,0.5),
        'j':(0,-1),
        'l':(0,1),
        'u':(1,-0.5),
        ',':(-1,0),
        '.':(-1,1),
        'm':(-1,-1),
           }

#increase/decrease speed(voltage) by 2
speedBindings={
        'q':(2,2),
        'z':(-2,-2),
        'w':(1.1,1),
        'x':(.9,1),
        'e':(1,1.1),
        'c':(1,.9),
          }

def getKey():
    tty.setraw(sys.stdin.fileno())
    rlist, _, _ = select.select([sys.stdin], [], [], 0.1)
    if rlist:
        key = sys.stdin.read(1)
    else:                                                                                          
        key = ''

    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, settings)
    return key

speed = 2
turn = 2

def vels(speed,turn):
    return "currently:\tspeed %s\tturn %s " % (speed,turn)

if __name__=="__main__":
    settings = termios.tcgetattr(sys.stdin)
    
    rospy.init_node('wheelchair_teleop')
    pub = rospy.Publisher('/navigation_velocity_smoother/raw_cmd_vel', Twist, queue_size=5)

    x = 0
    th = 0
    status = 0
    count = 0
    acc = 0.1
    target_speed = 0
    target_turn = 0
    control_speed = 0
    control_turn = 0
    try:
        print msg
        print vels(speed,turn)
        while(1):
            key = getKey()
            if key in moveBindings.keys():
                x = moveBindings[key][0]
                th = moveBindings[key][1]
                count = 0
            elif key in speedBindings.keys():
                speed = min(speed + speedBindings[key][0], 10)
                turn = min(turn + speedBindings[key][1], 10)
                count = 0
                if speed < 0 or turn < 0:
                    print "Maximum speed/turn should be positive"
                    speed = 0
                    turn = 0

                print vels(speed,turn)
                if (status == 14):
                    print msg
                status = (status + 1) % 15
            elif key == ' ' or key == 'k' :
                x = 0
                th = 0
                control_speed = 0
                control_turn = 0
            else:
                count = count + 1
                if count > 4:
                    x = 0
                    th = 0
                if (key == '\x03'):
                    break

            target_speed = speed * x
            target_turn = turn * th

            #0.5 -> acceleration, need to be big enough to overcome the friction
            if target_speed > control_speed:
                control_speed = min( target_speed, control_speed + 1 )
            elif target_speed < control_speed:
                control_speed = max( target_speed, control_speed - 1 )
            else:
                control_speed = target_speed

            if target_turn > control_turn:
                control_turn = min( target_turn, control_turn + 01 )
            elif target_turn < control_turn:
                control_turn = max( target_turn, control_turn - 1 )
            else:
                control_turn = target_turn

            msg = Twist()
            msg.linear.x = control_speed
            msg.angular.z = control_turn
            pub.publish(msg)

    except Exception as e:
        print "Control error"
        print e

    finally:
        msg = Twist()
        msg.linear.x = 0.0
        msg.angular.z = 0.0
        pub.publish(msg)

    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, settings)

