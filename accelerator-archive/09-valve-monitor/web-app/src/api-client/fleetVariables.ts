/* eslint-disable import/prefer-default-export */
import axios from "axios";
import { services } from "../services/ServiceLocatorClient";

// generic function to pass all preformatted fleet env vars as objects for updates to Notehub
async function updateFleetEnvVar(fleetEnvVarToUpdate: object) {
  const endpoint = services().getUrlManager().setFleetValveMonitorConfig();

  const response = await axios.post<object>(endpoint, fleetEnvVarToUpdate);

  return response.data;
}

export async function updateValveMonitorFrequency(monitorFrequency: number) {
  const response = await updateFleetEnvVar({
    monitorFrequency,
  });
  return response;
}

export async function updateAlarmThreshold(
  minFlowThreshold: number | undefined,
  maxFlowThreshold: number | undefined
) {
  const response = await updateFleetEnvVar({
    minFlowThreshold,
    maxFlowThreshold,
  });
  return response;
}
