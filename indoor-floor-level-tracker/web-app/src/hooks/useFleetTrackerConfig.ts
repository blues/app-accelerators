/* eslint-disable import/prefer-default-export */
import { useQuery } from "react-query";
import { TrackerConfig } from "../services/ClientModel";
import { getFleetTrackerConfig } from "../api-client/fleetVariables";

export function useFleetTrackerConfig() {
  return useQuery<TrackerConfig, Error>("getFleetTrackerConfig", () =>
    getFleetTrackerConfig()
  );
}
