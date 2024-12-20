/**
Software License Agreement (BSD)

\authors   Mike Purvis <mpurvis@clearpathrobotics.com>
\copyright Copyright (c) 2014, Clearpath Robotics, Inc., All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Clearpath Robotics nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WAR-
RANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, IN-
DIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string>
#include <map>

#include "geometry_msgs/Twist.h"
#include "ros/ros.h"
#include "sensor_msgs/Joy.h"
#include "teleop_joy/teleop_joy.h"

#include "mavros_msgs/CommandBool.h"


namespace teleop_joy
{

    struct TeleopJoy::Impl
    {
        void joyCallback(const sensor_msgs::Joy::ConstPtr &joy);
        void sendArmDisarm(const bool arm);
        void sendCmdVelMsg(const sensor_msgs::Joy::ConstPtr &joy_msg);
        void sendDepthTwistMsg(const sensor_msgs::Joy::ConstPtr &joy_msg);

        ros::Subscriber joy_sub;
        ros::Publisher cmd_vel_pub;
        ros::Publisher twist_depth_pub;
        ros::ServiceClient arming_client;

        std::map<std::string, int> axis_linear_map;
        std::map<std::string, double> scale_linear_map;
        std::map<std::string, double> offset_linear_map;

        std::map<std::string, int> axis_angular_map;
        std::map<std::string, double> scale_angular_map;
        std::map<std::string, double> offset_angular_map;

        int arming_button;
        bool armed = false;
        bool arming_button_released = true;

        bool depth_enabled = false;
        bool cmd_vel_enabled = false;
        bool arming_enabled = false;
    };

    /**
     * Constructs TeleopJoy.
     * \param nh NodeHandle to use for setting up the publisher and subscriber.
     * \param nh_param NodeHandle to use for searching for configuration parameters.
     */
    TeleopJoy::TeleopJoy(ros::NodeHandle *nh, ros::NodeHandle *nh_param)
    {
        pimpl_ = new Impl;
        pimpl_->joy_sub = nh->subscribe<sensor_msgs::Joy>("joy", 1, &TeleopJoy::Impl::joyCallback, pimpl_);

        // Linear axes
        if (nh_param->getParam("axis_linear", pimpl_->axis_linear_map))
        {
            nh_param->getParam("scale_linear", pimpl_->scale_linear_map);
            nh_param->getParam("offset_linear", pimpl_->offset_linear_map);
        }
        
        for (std::map<std::string, int>::iterator it = pimpl_->axis_linear_map.begin();
             it != pimpl_->axis_linear_map.end(); ++it)
        {
            ROS_INFO_NAMED("teleop_joy", "Linear axis %s on %i at scale %f with offset %f.",
                           it->first.c_str(), it->second, pimpl_->scale_linear_map[it->first], pimpl_->offset_linear_map[it->first]);
        }

        // Angular axes
        if (nh_param->getParam("axis_angular", pimpl_->axis_angular_map))
        {
            nh_param->getParam("scale_angular", pimpl_->scale_angular_map);
            nh_param->getParam("offset_angular", pimpl_->offset_angular_map);
        }
        
        for (std::map<std::string, int>::iterator it = pimpl_->axis_angular_map.begin();
             it != pimpl_->axis_angular_map.end(); ++it)
        {
            ROS_INFO_NAMED("teleop_joy", "Angular axis %s on %i at scale %f with offset %f.",
                           it->first.c_str(), it->second, pimpl_->scale_angular_map[it->first], pimpl_->offset_angular_map[it->first]);
        }

        // Buttons
        if (nh_param->param<int>("arming_button", pimpl_->arming_button, -1))
        {
            ROS_INFO_NAMED("teleop_joy", "Arming button %i.", pimpl_->arming_button);
        }

        pimpl_->cmd_vel_enabled = pimpl_->axis_linear_map.find("x") != pimpl_->axis_linear_map.end() && pimpl_->axis_linear_map.at("x") >= 0;
        pimpl_->depth_enabled = pimpl_->axis_linear_map.find("depth_up") != pimpl_->axis_linear_map.end() && pimpl_->axis_linear_map.at("depth_up") >= 0;
        pimpl_->arming_enabled = pimpl_->arming_button >= 0;
        ROS_INFO_NAMED("teleop_joy", "x/y/yaw control is %s.", pimpl_->cmd_vel_enabled ? "enabled" : "disabled");
        ROS_INFO_NAMED("teleop_joy", "Depth control is %s.", pimpl_->depth_enabled ? "enabled" : "disabled");
        ROS_INFO_NAMED("teleop_joy", "Arming control is %s.", pimpl_->arming_enabled ? "enabled" : "disabled");

        if (pimpl_->cmd_vel_enabled)
        {
            pimpl_->cmd_vel_pub = nh->advertise<geometry_msgs::Twist>("cmd_vel", 1, true);
        }
        if (pimpl_->depth_enabled)
        {
            pimpl_->twist_depth_pub = nh->advertise<geometry_msgs::Twist>("twist_depth", 1, true);
        }
        if (pimpl_->arming_enabled)
        {
            pimpl_->arming_client = nh->serviceClient<mavros_msgs::CommandBool>("mavros/cmd/arming");
        }
    }

    double getVal(const sensor_msgs::Joy::ConstPtr &joy_msg, const std::map<std::string, int> &axis_map,
                  const std::map<std::string, double> &scale_map, const std::map<std::string, double> &offset_map, const std::string &fieldname)
    {
        if (axis_map.find(fieldname) == axis_map.end() ||
            scale_map.find(fieldname) == scale_map.end() ||
            joy_msg->axes.size() <= axis_map.at(fieldname))
        {
            return 0.0;
        }

        return (joy_msg->axes[axis_map.at(fieldname)] + offset_map.at(fieldname)) * scale_map.at(fieldname);
    }

    void TeleopJoy::Impl::sendArmDisarm(const bool arm)
    {
        mavros_msgs::CommandBool arm_cmd;
        arm_cmd.request.value = arm;
        if (arming_client.call(arm_cmd))
        {
            ROS_INFO_NAMED("teleop_joy", "Vehicle %s.", arm ? "armed" : "disarmed");
            armed = arm;
        }
        else
        {
            ROS_INFO_NAMED("teleop_joy", "Failed to %s vehicle.", arm ? "arm" : "disarm");
        }
    }

    void TeleopJoy::Impl::sendCmdVelMsg(const sensor_msgs::Joy::ConstPtr &joy_msg)
    {
        geometry_msgs::Twist msg;
        msg.linear.x = getVal(joy_msg, axis_linear_map, scale_linear_map, offset_linear_map, "x");
        msg.linear.y = getVal(joy_msg, axis_linear_map, scale_linear_map, offset_linear_map, "y");
        msg.linear.z = 0;

        msg.angular.x = 0;
        msg.angular.y = 0;
        msg.angular.z = getVal(joy_msg, axis_angular_map, scale_angular_map, offset_angular_map, "yaw");

        cmd_vel_pub.publish(msg);
    }

    void TeleopJoy::Impl::sendDepthTwistMsg(const sensor_msgs::Joy::ConstPtr &joy_msg)
    {
        double up = getVal(joy_msg, axis_linear_map, scale_linear_map, offset_linear_map, "depth_up");
        double down = getVal(joy_msg, axis_linear_map, scale_linear_map, offset_linear_map, "depth_down");
        geometry_msgs::Twist msg;
        msg.linear.x = 0;
        msg.linear.y = 0;
        msg.linear.z = up - down;

        msg.angular.y = 0;
        msg.angular.z = 0;
        msg.angular.x = 0;

        twist_depth_pub.publish(msg);
    }
    
    void TeleopJoy::Impl::joyCallback(const sensor_msgs::Joy::ConstPtr &joy_msg)
    {
        if (arming_enabled >= 0 && joy_msg->buttons.size() > arming_button)
        {
            if ( joy_msg->buttons[arming_button] == 0)
            {
                arming_button_released = true;
            }
            else if (arming_button_released)
            {
                sendArmDisarm(!armed);
                arming_button_released = false;
            }
        }
        if (cmd_vel_enabled)
        {
            sendCmdVelMsg(joy_msg);
        }
        if (depth_enabled)
        {
            sendDepthTwistMsg(joy_msg);
        }
    }

} // namespace teleop_joy