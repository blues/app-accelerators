/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { services } from "../services/ServiceLocatorClient";

async function updateFleetEnvVar(fleetEnvVarToUpdate: object) {
  const endpoint = services()
    .getUrlManager()
    .setFleetTrackerConfig(fleetEnvVarToUpdate);

  const response: AxiosResponse = await axios.post(
    endpoint,
    fleetEnvVarToUpdate
  );

  return response.data as object;
}

export async function updateLiveTrackerStatus(liveTrackerStatus: boolean) {
  const updateLiveFleetTrackingObj = {
    live: liveTrackerStatus,
  };

  const response = await updateFleetEnvVar(updateLiveFleetTrackingObj);

  return response;
}
