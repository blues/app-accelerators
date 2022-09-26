/* eslint-disable import/prefer-default-export */
import axios from "axios";
import { services } from "../services/ServiceLocatorClient";

// generic function to pass all preformatted fleet env vars as objects for updates to Notehub
async function updateFleetEnvVar(fleetEnvVarToUpdate: object) {
  const endpoint = services().getUrlManager().setFleetTrackerConfig();

  const response = await axios.post<object>(endpoint, fleetEnvVarToUpdate);

  return response.data;
}

export async function updateLiveTrackerStatus(liveTrackerStatus: boolean) {
  const updateLiveFleetTrackingObj = {
    live: liveTrackerStatus,
  };

  const response = await updateFleetEnvVar(updateLiveFleetTrackingObj);

  return response;
}

export async function updateFloorHeightConfig(newFloorHeight: number) {
  const updateFloorHeightConfigObj = {
    floorHeight: newFloorHeight,
  };

  const response = await updateFleetEnvVar(updateFloorHeightConfigObj);

  return response;
}
