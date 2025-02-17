#define M_PI           3.14159265358979323846  /* pi */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <iostream>
#include <exception>

#include <boost/asio.hpp>

#include "orientus_sdk_c/an_packet_protocol.h"
#include "orientus_sdk_c/orientus_packets.h"

#include "ros/ros.h"
#include "sensor_msgs/Imu.h"
#include "sensor_msgs/MagneticField.h"
#include "sensor_msgs/Temperature.h"
#include "geometry_msgs/Quaternion.h"
#include "geometry_msgs/Vector3.h"
#include "diagnostic_updater/diagnostic_updater.h"
#include "diagnostic_updater/DiagnosticStatusWrapper.h"

class OrientusNode {
private:
  ros::NodeHandle nh_;
  ros::NodeHandle pnh_;
  ros::Rate loop_rate_;
  std::string frame_id_;
  ros::Publisher imu_pub_;
  ros::Publisher imu_euler_pub;
  ros::Publisher imu_raw_pub_;
  ros::Publisher magnetic_field_pub_;
  ros::Publisher temperature_pub_;
  diagnostic_updater::Updater diagnostic_;

  boost::asio::io_service io_service_;
  std::string port_name_;
  boost::asio::serial_port port_;
  an_decoder_t an_decoder_;

  euler_orientation_packet_t euler_orientation_packet_;
  quaternion_orientation_standard_deviation_packet_t quaternion_std_packet_;
  acceleration_packet_t acceleration_packet_;
  quaternion_orientation_packet_t quaternion_packet_;
  angular_velocity_packet_t angular_velocity_packet_;
  raw_sensors_packet_t raw_sensors_packet_;
  status_packet_t status_packet_;
  running_time_packet_t running_time_packet_;
  device_information_packet_t device_information_packet_;
  bool euler_orientation_received;
  bool quaternion_orientation_std_received_;
  bool quaternion_orientation_received_;
  bool acceleration_received_;
  bool angular_velocity_received_;
  bool raw_sensors_received_;
  bool status_received_;
  bool running_time_received_;
  bool device_information_received_;

public:
  OrientusNode(ros::NodeHandle nh, ros::NodeHandle pnh)
    : nh_(nh), pnh_(pnh), loop_rate_(100),
      diagnostic_(nh_, pnh_), port_(io_service_){
    //TODO: Find a way to detect which USB port is being used 
    pnh_.param("port", port_name_, std::string("/dev/ttyUSB0"));
    pnh_.param("frame_id", frame_id_, std::string("imu"));

    try{
      port_.open(port_name_);
      port_.set_option(boost::asio::serial_port_base::baud_rate(115200));
    }
    catch(const std::exception& e)
    {
      std::cerr << "Cannot Open Port:"<<port_name_<<std::endl;
    }

    // Reset IMU
    an_packet_t *an_packet;
    // an_packet = encode_reset_packet();
    // an_packet_encode_and_send(an_packet);
    // an_packet_free(&an_packet);

    // Request device information
    an_packet = encode_request_packet(packet_id_device_information);
    an_packet_encode_and_send(an_packet);
    an_packet_free(&an_packet);

    // Setup packet rate timer
    packet_timer_period_packet_t packet_timer_period_packet;
    packet_timer_period_packet.permanent = 0;
    packet_timer_period_packet.packet_timer_period = 1000; // 1 ms
    an_packet = encode_packet_timer_period_packet(&packet_timer_period_packet);
    an_packet_encode_and_send(an_packet);
    an_packet_free(&an_packet);

    // Configure packet rates
    packet_periods_packet_t packet_periods_packet;
    packet_periods_packet.permanent = 0;
    packet_periods_packet.clear_existing_packets = 1;
    packet_periods_packet.packet_periods[0].packet_id = packet_id_quaternion_orientation_standard_deviation;
    packet_periods_packet.packet_periods[0].period = 20; // 50 Hz
    packet_periods_packet.packet_periods[1].packet_id = packet_id_quaternion_orientation;
    packet_periods_packet.packet_periods[1].period = 20; // 50 Hz
    packet_periods_packet.packet_periods[2].packet_id = packet_id_acceleration;
    packet_periods_packet.packet_periods[2].period = 20; // 50 Hz
    packet_periods_packet.packet_periods[3].packet_id = packet_id_angular_velocity;
    packet_periods_packet.packet_periods[3].period = 20; // 50 Hz
    packet_periods_packet.packet_periods[4].packet_id = packet_id_raw_sensors;
    packet_periods_packet.packet_periods[4].period = 20; // 50 Hz
    packet_periods_packet.packet_periods[5].packet_id = packet_id_euler_orientation;
    packet_periods_packet.packet_periods[5].period = 20; // 50 Hz

    packet_periods_packet.packet_periods[6].packet_id = packet_id_status;
    packet_periods_packet.packet_periods[6].period = 200; // 5 Hz
    packet_periods_packet.packet_periods[7].packet_id = packet_id_running_time;
    packet_periods_packet.packet_periods[7].period = 200; // 5 Hz

    packet_periods_packet.packet_periods[8].packet_id = 0;
    an_packet = encode_packet_periods_packet(&packet_periods_packet);
    an_packet_encode_and_send(an_packet);
    an_packet_free(&an_packet);


    // Setup ROS topic
    imu_pub_ = nh.advertise<sensor_msgs::Imu>("imu/data", 10);
    imu_euler_pub = nh.advertise<geometry_msgs::Vector3>("imu/attitude",10);
    imu_raw_pub_ = nh.advertise<sensor_msgs::Imu>("imu/raw", 10);
    magnetic_field_pub_ = nh.advertise<sensor_msgs::MagneticField>("imu/mag", 10);
    temperature_pub_ = nh.advertise<sensor_msgs::Temperature>("imu/temp", 10);


    // Setup diagnostics
    diagnostic_.setHardwareID("orientus");
    diagnostic_.add("IMU Status", this, &OrientusNode::deviceStatus);
    diagnostic_.add("Accelerometer Status", this, &OrientusNode::accelerometerStatus);
    diagnostic_.add("Gyroscope Status", this, &OrientusNode::gyroscopeStatus);
    diagnostic_.add("Magnetometer Status", this, &OrientusNode::magnetometerStatus);
    diagnostic_.add("Temperature Status", this, &OrientusNode::temperatureStatus);
    diagnostic_.add("Voltage Status", this, &OrientusNode::voltageStatus);
    diagnostic_.add("Filter Status", this, &OrientusNode::filterStatus);


    an_decoder_initialise(&an_decoder_);

    euler_orientation_received = false;
    quaternion_orientation_std_received_ = false;
    quaternion_orientation_received_ = false;
    acceleration_received_ = false;
    raw_sensors_received_ = false;
    status_received_ = false;
    running_time_received_ = false;
    device_information_received_ = false;
  }
  void setZero() {
    //Reset orientation every time
    an_packet_t *an_packet;
    an_packet = encode_reset_packet();
    an_packet_encode_and_send(an_packet);
    an_packet_free(&an_packet);

    // zero_alignment_packet_t zero_alignment_packet;
    // zero_alignment_packet.permanent = 0;

    // an_packet_t *an_packet;
    // an_packet = encode_zero_alignment_packet(&zero_alignment_packet);
    // an_packet_encode_and_send(an_packet);
    // an_packet_free(&an_packet);

  }
  void spin() {
    while (ros::ok()) {
      while(receive_next_packet()){
    //std::cout<<"test"<<std::endl;
	if(euler_orientation_received && quaternion_orientation_std_received_ && quaternion_orientation_received_
	   && acceleration_received_ && angular_velocity_received_) {
    //ROS_WARN("TEST2");
	  publish_imu_msg();
    euler_orientation_received = false;
	  quaternion_orientation_std_received_ = false;
	  quaternion_orientation_received_ = false;
	  acceleration_received_ = false;
	  angular_velocity_received_ = false;
	}
	if(raw_sensors_received_) {
	  publish_imu_raw_msg();
	  publish_magnetics_msg();
	  publish_temperature_msg();
	  raw_sensors_received_ = false;
	}

	if(status_received_ && running_time_received_ && device_information_received_){
	  diagnostic_.update();
	  status_received_ = false;
	  running_time_received_ = false;
	}
      }

      ros::spinOnce();
      loop_rate_.sleep();
    }
  }

private:
  int an_packet_encode_and_send(an_packet_t* packet) {
    an_packet_encode(packet);
    // need cast to prevent autosizing of packet
    return boost::asio::write(port_, boost::asio::buffer((const void*)an_packet_pointer(packet), an_packet_size(packet)));
  }
  bool receive_next_packet(){
    an_packet_t *an_packet;
    int bytes_received;

    if ((bytes_received = port_.read_some(boost::asio::buffer(an_decoder_pointer(&an_decoder_), an_decoder_size(&an_decoder_)))) > 0) {
      /* increment the decode buffer length by the number of bytes received */
      an_decoder_increment(&an_decoder_, bytes_received);

      /* decode all the packets in the buffer */
      if((an_packet = an_packet_decode(&an_decoder_)) != NULL) {
	if(an_packet->id == packet_id_acknowledge) {
	  acknowledge_packet_t acknowledge_packet;
	  if(decode_acknowledge_packet(&acknowledge_packet, an_packet) != 0) {
	    ROS_WARN("Acknowledge packet decode failure");
	  }
	  else if(acknowledge_packet.acknowledge_result){
	    ROS_WARN("Acknowledge Failure: %d", acknowledge_packet.acknowledge_result);
	  }
	}
	else if(an_packet->id == packet_id_status) {
	  if(decode_status_packet(&status_packet_, an_packet) != 0) {
	    ROS_WARN("Status packet decode failure");
	  }
	  else{
	    status_received_ = true;
	  }
	}
	else if(an_packet->id == packet_id_running_time) {
	  if(decode_running_time_packet(&running_time_packet_, an_packet) != 0) {
	    ROS_WARN("Running time packet decode failure");
	  }
	  else{
	    running_time_received_ = true;
	  }
	}
	else if(an_packet->id == packet_id_device_information) {
	  if(decode_device_information_packet(&device_information_packet_, an_packet) != 0) {
	    ROS_WARN("Device information decode failure");
	  }
	  else{
	    device_information_received_ = true;
	  }
	}
  else if(an_packet->id == packet_id_quaternion_orientation_standard_deviation) {
	  if(decode_quaternion_orientation_standard_deviation_packet(&quaternion_std_packet_, an_packet) != 0) {
	    ROS_WARN("Quaternion orientation standard deviation packet decode failure");
	  }
	  else{
      //ROS_WARN("TEST2");
	    quaternion_orientation_std_received_ = true;
	  }
	}
  else if(an_packet->id == packet_id_euler_orientation) {
    if(decode_euler_orientation_packet(&euler_orientation_packet_, an_packet) != 0) {
      ROS_WARN("Euler orientation packet decode failure");
    }
    else {
      //ROS_WARN("TEST2");
      euler_orientation_received = true;
    }
  }
	else if(an_packet->id == packet_id_quaternion_orientation) {
	  if(decode_quaternion_orientation_packet(&quaternion_packet_, an_packet) != 0) {
	    ROS_WARN("Quaternion packet decode failure");
	  }
	  else {
      // ROS_WARN("TEST2");
	    quaternion_orientation_received_ = true;
	  }
	}
	else if(an_packet->id == packet_id_acceleration) {
	  if(decode_acceleration_packet(&acceleration_packet_, an_packet) != 0) {
	    ROS_WARN("Acceleration packet decode failure");
	  }
	  else {
      // ROS_WARN("TEST2");
	    acceleration_received_ = true;
	  }
	}
	else if(an_packet->id == packet_id_angular_velocity) {
	  if(decode_angular_velocity_packet(&angular_velocity_packet_, an_packet) != 0) {
	    ROS_WARN("Angular velocity packet decode failure");
	  }
	  else {
      // ROS_WARN("TEST2");
	    angular_velocity_received_ = true;
	  }
	}
	else if(an_packet->id == packet_id_raw_sensors) {
	  if(decode_raw_sensors_packet(&raw_sensors_packet_, an_packet) != 0) {
	    ROS_WARN("Raw sensors packet decode failure");
	  }
	  else {
	    raw_sensors_received_ = true;
	  }
	}
	else {
	  ROS_WARN("Unknown packet id: %d of length: %d ", an_packet->id, an_packet->length);
	}

	an_packet_free(&an_packet);
      }
    }
  //ROS_WARN("TEST");
  return true;
  }

  void deviceStatus(diagnostic_updater::DiagnosticStatusWrapper &status) {
    if(status_packet_.system_status.b.system_failure)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::ERROR, "System Failure");
    else if(status_packet_.system_status.b.serial_port_overflow_alarm)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "Serial Port Overflow");
    else
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::OK, "IMU is OK");

    status.add("Device", port_name_);
    status.add("TF frame", frame_id_);
    double running_time = (1.0e-6) * running_time_packet_.microseconds + running_time_packet_.seconds;
    status.add("Running time", running_time);
    status.add("Device ID", device_information_packet_.device_id);

    std::ostringstream software_version_stream;
    software_version_stream << std::fixed << std::setprecision(3);
    software_version_stream << device_information_packet_.software_version/1000.0;
    status.add("Software Version", software_version_stream.str());

    std::ostringstream hardware_revision_stream;
    hardware_revision_stream << std::fixed << std::setprecision(3);
    hardware_revision_stream << device_information_packet_.hardware_revision/1000.0;
    status.add("Hardware Revision", hardware_revision_stream.str());

    std::stringstream serial_number_stream;
    serial_number_stream << std::hex << std::setfill('0') << std::setw(8);
    serial_number_stream << device_information_packet_.serial_number[0];
    serial_number_stream << device_information_packet_.serial_number[1];
    serial_number_stream << device_information_packet_.serial_number[2];
    status.add("Serial Number", serial_number_stream.str());
  }
  void accelerometerStatus(diagnostic_updater::DiagnosticStatusWrapper &status) {
    if(status_packet_.system_status.b.accelerometer_sensor_failure)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::ERROR, "Accelerometer Sensor Failure");
    else if(status_packet_.system_status.b.accelerometer_over_range)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "Accelerometer Sensor Over Range");
    else
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::OK, "Accelerometer OK");
  }
  void gyroscopeStatus(diagnostic_updater::DiagnosticStatusWrapper &status) {
    if(status_packet_.system_status.b.gyroscope_sensor_failure)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::ERROR, "Gyroscope Sensor Failure");
    else if(status_packet_.system_status.b.gyroscope_over_range)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "Gyroscope Sensor Over Range");
    else
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::OK, "Gyroscope OK");
  }
  void magnetometerStatus(diagnostic_updater::DiagnosticStatusWrapper &status) {
    if(status_packet_.system_status.b.magnetometer_sensor_failure)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::ERROR, "Magnetometer Sensor Failure");
    else if(status_packet_.system_status.b.magnetometer_over_range)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "Magnetometer Sensor Over Range");
    else
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::OK, "Magnetometer OK");
  }
  void temperatureStatus(diagnostic_updater::DiagnosticStatusWrapper &status) {
    if(status_packet_.system_status.b.minimum_temperature_alarm)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "Minimum Temperature Alarm");
    else if(status_packet_.system_status.b.maximum_temperature_alarm)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "Maximum Temperature Alarm");
    else
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::OK, "Temperature OK");
  }
  void voltageStatus(diagnostic_updater::DiagnosticStatusWrapper &status) {
    if(status_packet_.system_status.b.low_voltage_alarm)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "Low Voltage");
    else if(status_packet_.system_status.b.high_voltage_alarm)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::WARN, "High Voltage");
    else
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::OK, "Voltage OK");
  }
  void filterStatus(diagnostic_updater::DiagnosticStatusWrapper &status) {
    if(status_packet_.filter_status.b.orientation_filter_initialised)
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::OK, "Filter OK");
    else
      status.summary(diagnostic_updater::DiagnosticStatusWrapper::ERROR, "Filter Not Initialised");

    status.add("Heading Initialised", (bool)status_packet_.filter_status.b.heading_initialised);
    status.add("Magnetometers Enabled", (bool)status_packet_.filter_status.b.magnetometers_enabled);
    status.add("Velocity Heading Enabled", (bool)status_packet_.filter_status.b.velocity_heading_enabled);
    status.add("External Position Active", (bool)status_packet_.filter_status.b.external_position_active);
    status.add("External Velocity Active", (bool)status_packet_.filter_status.b.external_velocity_active);
    status.add("External Heading Active", (bool)status_packet_.filter_status.b.external_heading_active);
  }

  void publish_imu_msg() {
    sensor_msgs::Imu imu_msg;

    geometry_msgs::Vector3 euler_orientation;

    euler_orientation.x = euler_orientation_packet_.orientation[0] * 180 / M_PI; //roll(degree)
    euler_orientation.y = euler_orientation_packet_.orientation[1] * 180 / M_PI; //pitch(degree)
    euler_orientation.z = euler_orientation_packet_.orientation[2] * 180 / M_PI; //heading(degree)

    float orientation_covariance[9] = {pow((quaternion_std_packet_.standard_deviation[0]), 2.0), 0, 0,
				       0, pow((quaternion_std_packet_.standard_deviation[1]), 2.0), 0,
				       0, 0, pow((quaternion_std_packet_.standard_deviation[2]), 2.0)};

    imu_msg.header.stamp = ros::Time::now();
    imu_msg.header.frame_id = frame_id_;

    imu_msg.orientation.x = quaternion_packet_.orientation[0];
    imu_msg.orientation.y = quaternion_packet_.orientation[1];
    imu_msg.orientation.z = quaternion_packet_.orientation[2];
    imu_msg.orientation.w = quaternion_packet_.orientation[3];
    imu_msg.orientation_covariance[0] = orientation_covariance[0];
    imu_msg.orientation_covariance[4] = orientation_covariance[4];
    imu_msg.orientation_covariance[8] = orientation_covariance[8];

    imu_msg.angular_velocity.x = angular_velocity_packet_.angular_velocity[0];
    imu_msg.angular_velocity.y = angular_velocity_packet_.angular_velocity[1];
    imu_msg.angular_velocity.z = angular_velocity_packet_.angular_velocity[2];
    imu_msg.angular_velocity_covariance[0] = -1;

    imu_msg.linear_acceleration.x = acceleration_packet_.acceleration[0];
    imu_msg.linear_acceleration.y = acceleration_packet_.acceleration[1];
    imu_msg.linear_acceleration.z = acceleration_packet_.acceleration[2];
    imu_msg.linear_acceleration_covariance[0] = -1;

    imu_pub_.publish(imu_msg);
    imu_euler_pub.publish(euler_orientation);
  }
  void publish_imu_raw_msg() {
    sensor_msgs::Imu imu_msg;
    geometry_msgs::Vector3 angular_velocity;
    geometry_msgs::Vector3 linear_acceleration;

    imu_msg.header.stamp = ros::Time::now();
    imu_msg.header.frame_id = frame_id_;

    imu_msg.orientation_covariance[0] = -1;

    imu_msg.angular_velocity.x = raw_sensors_packet_.gyroscopes[0];
    imu_msg.angular_velocity.y = raw_sensors_packet_.gyroscopes[1];
    imu_msg.angular_velocity.z = raw_sensors_packet_.gyroscopes[2];
    imu_msg.angular_velocity_covariance[0] = -1;

    imu_msg.linear_acceleration.x = raw_sensors_packet_.accelerometers[0];
    imu_msg.linear_acceleration.y = raw_sensors_packet_.accelerometers[1];
    imu_msg.linear_acceleration.z = raw_sensors_packet_.accelerometers[2];
    imu_msg.linear_acceleration_covariance[0] = -1;

    imu_raw_pub_.publish(imu_msg);
  }
  void publish_magnetics_msg() {
    sensor_msgs::MagneticField magnetic_field_msg;

    magnetic_field_msg.header.stamp = ros::Time::now();
    magnetic_field_msg.header.frame_id = frame_id_;

    magnetic_field_msg.magnetic_field.x = raw_sensors_packet_.magnetometers[0] * (1.0e-7);
    magnetic_field_msg.magnetic_field.y = raw_sensors_packet_.magnetometers[1] * (1.0e-7);
    magnetic_field_msg.magnetic_field.z = raw_sensors_packet_.magnetometers[2] * (1.0e-7);

    magnetic_field_pub_.publish(magnetic_field_msg);
  }
  void publish_temperature_msg() {
    sensor_msgs::Temperature temp_msg;

    temp_msg.header.stamp = ros::Time::now();
    temp_msg.header.frame_id = frame_id_;

    temp_msg.temperature = raw_sensors_packet_.imu_temperature;

    temperature_pub_.publish(temp_msg);
  }

};

int main(int argc, char *argv[]) {
  ros::init(argc, argv, "orientus_node");
  ros::NodeHandle nh;
  ros::NodeHandle pnh("~");

  try {
    OrientusNode node(nh, pnh);

    //node.setZero();
    node.spin();
  } catch(std::exception& e){
    ROS_FATAL_STREAM("Exception thrown: " << e.what());
  }

  return 0;

}
