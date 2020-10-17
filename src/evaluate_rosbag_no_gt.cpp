#include <ros/ros.h>
#include "evaluator.h"

int main(int argc, char **argv)
{
  ros::init(argc, argv, "evaluate_planner_no_gt");
  ros::NodeHandle nh;
  ros::NodeHandle nhp("~");
  ros::AsyncSpinner spinner(4);
  spinner.start();

  Evaluator evaluator(nh, nhp, false, "", 0.01, roi_viewpoint_planner::Planner_MAP_ONLY);

  const std::string resultsFileName = "planner_results.csv";
  std::ofstream resultsFile(resultsFileName);
  resultsFile << "Time (s), Total ROI cluster, ROI key count" << std::endl;

  bool planner_active = false;
  for(ros::Rate rate(1); ros::ok() && !planner_active; rate.sleep())
  {
    planner_active = evaluator.activatePlanner();
    if (planner_active)
      ROS_INFO("Planner activated");
    else
      ROS_INFO("Planner could not be activated, retrying in 1 second...");
  }

  ros::Time plannerStartTime = ros::Time::now();

  for (ros::Rate rate(1); ros::ok(); rate.sleep())
  {
    ros::Time currentTime = ros::Time::now();

    const EvaluationParameters &res = evaluator.processDetectedRois();

    double passed_time = (currentTime - plannerStartTime).toSec();

    resultsFile << passed_time << ", " << res.total_roi_clusters << ", " << res.roi_key_count << std::endl;
  }

  resultsFile.close();

}