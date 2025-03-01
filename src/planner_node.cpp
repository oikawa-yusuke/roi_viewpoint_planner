﻿#include <ros/ros.h>
#include <ros/package.h>

#include "roi_viewpoint_planner/viewpoint_planner.h"
#include <roi_viewpoint_planner_msgs/SaveOctomap.h>
#include <roi_viewpoint_planner_msgs/MoveToState.h>
#include <roi_viewpoint_planner_msgs/RandomizePlantPositions.h>
#include <roi_viewpoint_planner_msgs/ResetPlanner.h>
#include <roi_viewpoint_planner_msgs/LoadOctomap.h>
#include <roi_viewpoint_planner_msgs/StartEvaluator.h>
#include <std_srvs/Trigger.h>
#include <std_srvs/Empty.h>
#include <boost/algorithm/string/predicate.hpp>

#include <dynamic_reconfigure/server.h>
#include "roi_viewpoint_planner_msgs/PlannerConfig.h"

using namespace roi_viewpoint_planner;

ros::NodeHandle *nhp_pt;
ViewpointPlanner *planner;
dynamic_reconfigure::Server<roi_viewpoint_planner::PlannerConfig> *config_server;
boost::recursive_mutex config_mutex;
roi_viewpoint_planner::PlannerConfig current_config;

bool startEvaluator(roi_viewpoint_planner_msgs::StartEvaluator::Request &req, roi_viewpoint_planner_msgs::StartEvaluator::Response &res)
{
  if (req.episode_end_param >= static_cast<uint8_t>(roi_viewpoint_planner::EvalEpisodeEndParam::NUM_EPEND_PARAMS))
  {
    res.success = false;
  }
  else
  {
    roi_viewpoint_planner::EvalEpisodeEndParam epEndParam = static_cast<roi_viewpoint_planner::EvalEpisodeEndParam>(req.episode_end_param);
    res.success = planner->startEvaluator(req.num_evals, epEndParam, req.episode_duration, req.starting_index,
                  req.randomize_plants, octomap_vpp::pointToOctomath(req.min_point), octomap_vpp::pointToOctomath(req.max_point), req.min_dist);
  }
  return true;
}

bool randomizePlantPositions(roi_viewpoint_planner_msgs::RandomizePlantPositions::Request &req, roi_viewpoint_planner_msgs::RandomizePlantPositions::Response &res)
{
  res.success = planner->randomizePlantPositions(req.min_point, req.max_point, req.min_dist);
  return true;
}

bool saveTreeAsObj(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
{
  res.success = planner->saveTreeAsObj("planning_tree.obj");
  planner->saveROIsAsObj("roi_mesh.obj");
  return true;
}

bool saveOctomap(roi_viewpoint_planner_msgs::SaveOctomap::Request &req, roi_viewpoint_planner_msgs::SaveOctomap::Response &res)
{
  if (req.specify_filename)
    res.filename = planner->saveOctomap(req.name, req.name_is_prefix);
  else
    res.filename = planner->saveOctomap();

  res.success = res.filename != "";
  return true;
}

bool loadOctomap(roi_viewpoint_planner_msgs::LoadOctomap::Request &req, roi_viewpoint_planner_msgs::LoadOctomap::Response &res)
{
  int err_code = planner->loadOctomap(req.filename);
  res.success = (err_code == 0);
  if (err_code == -1) res.error_message = "Deserialization failed";
  else if (err_code == -2) res.error_message = "Wrong Octree type";
  return true;
}

bool resetOctomap(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res)
{
  planner->resetOctomap();
  return true;
}

bool resetPlanner(roi_viewpoint_planner_msgs::ResetPlanner::Request &req, roi_viewpoint_planner_msgs::ResetPlanner::Response &res)
{
  config_mutex.lock();
  current_config.mode = roi_viewpoint_planner::Planner_IDLE;
  planner->mode = (ViewpointPlanner::PlannerMode) current_config.mode;
  config_server->updateConfig(current_config);
  config_mutex.unlock();

  planner->resetOctomap();

  std::vector<double> joint_start_values;
  if(nhp_pt->getParam("initial_joint_values", joint_start_values))
  {
    res.success = planner->moveToState(joint_start_values, req.async, false);
  }
  else
  {
    ROS_WARN("No inital joint values set");
  }
  return true;
}

bool moveToState(roi_viewpoint_planner_msgs::MoveToState::Request &req, roi_viewpoint_planner_msgs::MoveToState::Response &res)
{
  res.success = planner->moveToState(req.joint_values, req.async, false);
  return true;
}

void reconfigureCallback(roi_viewpoint_planner::PlannerConfig &config, uint32_t level)
{
  ROS_INFO_STREAM("Reconfigure callback called");
  if (level & (1 << 0)) // change mode
  {
    planner->mode = static_cast<ViewpointPlanner::PlannerMode>(config.mode);
    planner->roi_sample_mode = static_cast<ViewpointPlanner::PlannerMode>(config.auto_roi_sampling);
    planner->expl_sample_mode = static_cast<ViewpointPlanner::PlannerMode>(config.auto_expl_sampling);

    planner->roiMaxSamples = config.roi_max_samples;
    planner->roiUtil = static_cast<roi_viewpoint_planner::UtilityType>(config.roi_util);
    planner->explMaxSamples = config.expl_max_samples;
    planner->explUtil = static_cast<roi_viewpoint_planner::UtilityType>(config.expl_util);
  }
  if (level & (1 << 1)) // activate execution
  {
    planner->execute_plan = config.activate_execution;
  }
  if (level & (1 << 2)) // request execution confirmation
  {
    planner->require_execution_confirmation = config.require_execution_confirmation;
  }
  if (level & (1 << 3)) // minimum sensor range
  {
    planner->sensor_min_range = config.sensor_min_range;
    if (planner->sensor_max_range < planner->sensor_min_range && !(level & (1 << 4)))
    {
      planner->sensor_max_range = planner->sensor_min_range;
      config.sensor_max_range = planner->sensor_max_range;
    }
  }
  if (level & (1 << 4)) // maximum sensor range
  {
    planner->sensor_max_range = config.sensor_max_range;
    if (planner->sensor_min_range > planner->sensor_max_range)
    {
      planner->sensor_min_range = planner->sensor_max_range;
      config.sensor_min_range = planner->sensor_min_range;
    }
  }
  if (level & (1 << 5)) // insert_scan_if_not_moved
  {
    planner->insert_scan_if_not_moved = config.insert_scan_if_not_moved;
  }
  if (level & (1 << 7)) // insert_scan_while_moving
  {
    planner->insert_scan_while_moving = config.insert_scan_while_moving;
  }
  if (level & (1 << 9)) // wait_for_scan
  {
    planner->wait_for_scan = config.wait_for_scan;
  }
  if (level & (1 << 11)) // publish_planning_state
  {
    planner->publish_planning_state = config.publish_planning_state;
  }
  if (level & (1 << 12)) // planner
  {
    planner->setPlannerId(config.planner);
  }
  if (level & (1 << 13)) // planning_time
  {
    planner->setPlanningTime(config.planning_time);
  }
  if (level & (1 << 14)) // use_cartesian_motion
  {
    planner->use_cartesian_motion = config.use_cartesian_motion;
  }
  if (level & (1 << 15)) // compute_ik_when_sampling
  {
    planner->compute_ik_when_sampling = config.compute_ik_when_sampling;
  }
  if (level & (1 << 16)) // velocity_scaling
  {
    planner->setMaxVelocityScalingFactor(config.velocity_scaling);
  }
  if (level & (1 << 17)) // record_map_updates
  {
    planner->record_map_updates = config.record_map_updates;
  }
  if (level & (1 << 18)) // record_viewpoints
  {
    planner->record_viewpoints = config.record_viewpoints;
  }
  if (level & (1 << 21)) // activate_move_to_see
  {
    planner->activate_move_to_see = config.activate_move_to_see;
    planner->move_to_see_exclusive = config.move_to_see_exclusive;
    planner->m2s_delta_thresh = config.m2s_delta_thresh;
    planner->m2s_max_steps = config.m2s_max_steps;
  }
  if (level & (1 << 22)) // publish_cluster_visualization
  {
    planner->publish_cluster_visualization = config.publish_cluster_visualization;
    planner->minimum_cluster_size = config.minimum_cluster_size;
    planner->cluster_neighborhood = static_cast<octomap_vpp::Neighborhood>(config.cluster_neighborhood);
  }
  current_config = config;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "roi_viewpoint_planner");
  ros::NodeHandle nh;
  ros::NodeHandle nhp("~");
  nhp_pt = &nhp;
  ros::AsyncSpinner spinner(4);
  spinner.start();

  /*trolley_remote::TrolleyRemote trm(nh, nhp);
  ROS_INFO_STREAM("Trolley status: " << trm.getStatus());
  ROS_INFO_STREAM("Current position/height: " << trm.getPosition() << ", " << trm.getHeight());*/

  double tree_resolution = 0.01;
  if (nhp.hasParam("tree_resolution"))
    nhp.getParam("tree_resolution", tree_resolution);
  else
    nhp.setParam("tree_resolution", 0.01);

  std::string arm_group;
  nhp.param<std::string>("arm_group", arm_group, "manipulator");

  std::string wstree_default_package = ros::package::getPath("ur_with_cam_gazebo");
  std::string wstree_file = nhp.param<std::string>("workspace_tree", wstree_default_package + "/workspace_trees/static/workspace_map.ot");
  std::string sampling_tree_file = nhp.param<std::string>("sampling_tree", wstree_default_package + "/workspace_trees/static/inflated_ws_tree.ot");
  std::string map_frame = nhp.param<std::string>("map_frame", "world");
  std::string ws_frame = nhp.param<std::string>("ws_frame", "arm_base_link");
  bool update_planning_tree = nhp.param<bool>("update_planning_tree", true);
  bool initialize_evaluator = nhp.param<bool>("initialize_evaluator", true);

  ROS_INFO("*-------- PARAM --------*");
  ROS_INFO("arm_group:%s", arm_group.c_str());
  ROS_INFO("wstree_file:%s", wstree_file.c_str());
  ROS_INFO("sampling_tree_file:%s", sampling_tree_file.c_str());
  ROS_INFO("map_frame:%s", map_frame.c_str());
  ROS_INFO("ws_frame:%s", ws_frame.c_str());

  planner = new ViewpointPlanner(nh, nhp, wstree_file, sampling_tree_file, tree_resolution, map_frame, ws_frame, update_planning_tree, initialize_evaluator, arm_group);
  ros::ServiceServer saveTreeAsObjService = nhp.advertiseService("save_tree_as_obj", saveTreeAsObj);
  ros::ServiceServer saveOctomapService = nhp.advertiseService("save_octomap", saveOctomap);
  ros::ServiceServer loadOctomapService = nhp.advertiseService("load_octomap", loadOctomap);
  ros::ServiceServer moveToStateService = nhp.advertiseService("move_to_state", moveToState);
  ros::ServiceServer randomizePlantPositionsService = nhp.advertiseService("randomize_plant_positions", randomizePlantPositions);
  ros::ServiceServer resetOctomapService = nhp.advertiseService("reset_octomap", resetOctomap);
  ros::ServiceServer resetPlannerService = nhp.advertiseService("reset_planner", resetPlanner);
  ros::ServiceServer startEvaluatorService = nhp.advertiseService("start_evaluator", startEvaluator);

  config_server = new dynamic_reconfigure::Server<roi_viewpoint_planner::PlannerConfig>(config_mutex, nhp);
  config_server->setCallback(reconfigureCallback);

  std::vector<double> joint_start_values;
  if(nhp.getParam("initial_joint_values", joint_start_values))
  {
    // planner->moveToState(joint_start_values, false, false);
  }
  else
  {
    ROS_WARN("No inital joint values set");
  }

  planner->plannerLoop();

  delete planner;
}
