/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { services } from "../services/ServiceLocatorClient";

export async function updateLiveTrackerStatus(newTrackerConfig: boolean) {
  // todo ask team if it makes more sense to have one fuction to handle all fleet updates or multiple functions handling just one particular kind
  // update the entire trackerconfig obj with whatever new fleet value needs to change
  const updateLiveFleetTracking = {
    live: newTrackerConfig,
  };

  const endpoint = services()
    .getUrlManager()
    .setFleetTrackerConfig(updateLiveFleetTracking);

  const response: AxiosResponse = await axios.post(
    endpoint,
    updateLiveFleetTracking
  );
  console.log("Response back from update tracker status! ", response);
  return response.data;
}
