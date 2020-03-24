/******************************************************************************
 * Copyright 2019 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/storytelling/story_tellers/close_to_junction_teller.h"

#include <memory>
#include <string>
#include <vector>

#include "modules/common/adapters/adapter_gflags.h"
#include "modules/map/hdmap/hdmap_util.h"
#include "modules/planning/proto/planning.pb.h"
#include "modules/storytelling/common/storytelling_gflags.h"
#include "modules/storytelling/frame_manager.h"

namespace apollo {
namespace storytelling {
namespace {

using apollo::hdmap::HDMapUtil;
using apollo::common::PathPoint;
using apollo::hdmap::JunctionInfo;
using apollo::hdmap::PNCJunctionInfo;
using apollo::planning::ADCTrajectory;

bool IsPointInPNCJunction(const PathPoint& point,
                          std::string* pnc_junction_id) {
  common::PointENU hdmap_point;
  hdmap_point.set_x(point.x());
  hdmap_point.set_y(point.y());
  std::vector<std::shared_ptr<const PNCJunctionInfo>> pnc_junctions;
  HDMapUtil::BaseMap().GetPNCJunctions(hdmap_point,
                                       FLAGS_search_radius,
                                       &pnc_junctions);
  if (pnc_junctions.empty() || pnc_junctions.front() == nullptr) {
    return false;
  }
  const auto& pnc_junction_info = pnc_junctions.front();
  if (pnc_junction_info != nullptr &&
      pnc_junction_info->pnc_junction().polygon().point_size() >= 3) {
    *pnc_junction_id = pnc_junction_info->id().id();
    return true;
  }
  return false;
}

bool IsPointInJunction(const PathPoint& point,
                       std::string* junction_id) {
  common::PointENU hdmap_point;
  hdmap_point.set_x(point.x());
  hdmap_point.set_y(point.y());
  std::vector<std::shared_ptr<const JunctionInfo>> junctions;
  HDMapUtil::BaseMap().GetJunctions(hdmap_point,
                                    FLAGS_search_radius,
                                    &junctions);
  if (junctions.empty() || junctions.front() == nullptr) {
    return false;
  }
  const auto& junction_info = junctions.front();
  if (junction_info != nullptr &&
      junction_info->junction().polygon().point_size() >= 3) {
    *junction_id = junction_info->id().id();
    return true;
  }
  return false;
}

/**
 * @brief Get distance to the nearest junction within search radius.
 * @return negative if no junction ahead, 0 if in junction, or positive which is
 *         the distance to the nearest junction ahead.
 */
void GetNearestJunction(const ADCTrajectory& adc_trajectory,
                        std::string* id,
                        CloseToJunction::JunctionType* type,
                        double* distance) {
  *id = "";
  *distance = -1;
  static std::string overlapping_junction_id;

  const double s_start =
      adc_trajectory.trajectory_point(0).path_point().s();
  for (const auto& point : adc_trajectory.trajectory_point()) {
    const auto& path_point = point.path_point();
    if (path_point.s() > FLAGS_adc_trajectory_search_distance) {
      break;
    }
    std::string junction_id;
    std::string pnc_junction_id;
    const double junction = IsPointInJunction(path_point, &junction_id);
    const double pnc_junction = IsPointInPNCJunction(path_point,
                                                     &pnc_junction_id);
    if (pnc_junction) {
      // in PNC_JUNCTION (including overlapping with JUNCTION)
      *id = pnc_junction_id;
      *type = CloseToJunction::PNC_JUNCTION;
      *distance = path_point.s() - s_start;
      overlapping_junction_id = junction ? junction_id : "";
      break;
    } else if (junction) {
      // in JUNCTION only
      if (junction_id != overlapping_junction_id) {
        // not in JUNCTION overlapping with a PNC_JUNCTION
        *id = junction_id;
        *type = CloseToJunction::JUNCTION;
        *distance = path_point.s() - s_start;
      }
      break;
    } else {
      overlapping_junction_id.clear();
    }
  }
}

}  // namespace

void CloseToJunctionTeller::Init() {
  auto* manager = FrameManager::Instance();
  manager->CreateOrGetReader<ADCTrajectory>(FLAGS_planning_trajectory_topic);
}

void CloseToJunctionTeller::Update(Stories* stories) {
  auto* manager = FrameManager::Instance();
  static auto planning_reader = manager->CreateOrGetReader<ADCTrajectory>(
      FLAGS_planning_trajectory_topic);
  const auto trajectory = planning_reader->GetLatestObserved();
  if (trajectory == nullptr || trajectory->trajectory_point().empty()) {
    AERROR << "Planning trajectory not ready.";
    return;
  }

  std::string junction_id;
  CloseToJunction::JunctionType type;
  double distance;
  GetNearestJunction(*trajectory, &junction_id, &type, &distance);
  const bool close_to_junction = distance >= 0;
  if (close_to_junction) {
    if (!stories->has_close_to_junction()) {
      AINFO << "Enter CloseToJunction story";
    }
    auto* story = stories->mutable_close_to_junction();
    story->set_id(junction_id);
    story->set_type(type);
    story->set_distance(distance);
  } else if (stories->has_close_to_junction()) {
    AINFO << "Exit CloseToJunction story";
    stories->clear_close_to_junction();
  }
}

}  // namespace storytelling
}  // namespace apollo
