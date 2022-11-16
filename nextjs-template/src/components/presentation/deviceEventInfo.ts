/* eslint-disable import/prefer-default-export */
import {
  Device,
  DeviceEnvVars,
  Event,
  FleetEnvVars,
} from "../../services/DomainModel";

function assembleDeviceEventsObject(
  deviceID: string,
  deviceEnvVars: DeviceEnvVars[],
  fleetEnvVars: FleetEnvVars[],
  eventList: Event[]
) {
  return {
    deviceID,
    deviceEnvVars: deviceEnvVars[0].environment_variables,
    fleetEnvVars: fleetEnvVars.length
      ? fleetEnvVars[0].environment_variables
      : {},
    eventList,
  };
}

// function to combine devices with their event data and env vars
export function getNormalizedDeviceData(
  devices: Device[],
  deviceEvents: Event[],
  deviceEnvVars: DeviceEnvVars[],
  fleetEnvVars: FleetEnvVars[]
) {
  const deviceEventInfo = devices.map((device) => {
    // consider also filtering out certain event types (like alarms or notification events) as well to keep the data cleaner and more uniform
    const filterEventsByDevice = deviceEvents
      .filter((event) => event.deviceUID.deviceUID === device.id.deviceUID)
      // sort events newest to oldest
      .sort((a, b) => Number(new Date(b.when)) - Number(new Date(a.when)));

    const updatedEventList = [...filterEventsByDevice];

    // filter correct device env vars
    const filteredDeviceEnvVars = deviceEnvVars.filter(
      (deviceEnvVar) => deviceEnvVar.deviceID === device.id.deviceUID
    );

    // filter correct fleet env vars
    /* operating under the assumption a device will only be assigned to one fleet at a time */
    let filteredFleetEnvVars: FleetEnvVars[] = [];
    if (fleetEnvVars.length) {
      filteredFleetEnvVars = fleetEnvVars.filter(
        (fleetEnvVar) => fleetEnvVar.fleetUID === device.fleetUIDs[0]
      );
    }

    // reassemble each device with its events and env vars
    const updatedDeviceEventsObject = assembleDeviceEventsObject(
      device.id.deviceUID,
      filteredDeviceEnvVars,
      filteredFleetEnvVars,
      updatedEventList
    );

    return updatedDeviceEventsObject;
  });

  return deviceEventInfo;
}
