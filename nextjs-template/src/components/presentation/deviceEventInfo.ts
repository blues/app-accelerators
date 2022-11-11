/* eslint-disable import/prefer-default-export */
import { Device, Event } from "../../services/DomainModel";

// todo wip function to combine devices with their data and env vars
export function getNormalizedDeviceData(
  devices: Device[],
  deviceEvents: Event[],
  deviceEnvVars: any[],
  fleetEnvVars: any[]
) {
  const deviceEventInfo = devices.map((device) => {
    // consider also filtering out alarm.qo events as well to keep the data cleaner and more uniform
    const filterEventsByDevice = deviceEvents
      .filter((event) => event.deviceUID.deviceUID === device.id.deviceUID)
      // sort events newest to oldest
      .sort((a, b) => new Date(b.when) - new Date(a.when));
    const updatedEventList = {
      eventList: filterEventsByDevice,
    };

    // filter correct device env vars
    const filteredDeviceEnvVars = deviceEnvVars.filter(
      (deviceEnvVar) => deviceEnvVar.deviceID === device.id.deviceUID
    );

    // filter correct fleet env vars
    /* operating under the assumption a device will only be assigned to one fleet at a time */
    const filteredFleetEnvVars: any[] = [];
    if (fleetEnvVars.length) {
      fleetEnvVars.filter(
        (fleetEnvVar) => fleetEnvVar.fleetUID === device.fleetUIDs[0]
      );
    }

    // reassemble each device with its events and env vars
    const updatedDeviceEventsObject = {
      deviceID: device.id.deviceUID,
      deviceEnvVars: filteredDeviceEnvVars[0].environment_variables,
      fleetEnvVars: filteredFleetEnvVars.length
        ? filteredFleetEnvVars[0].environment_variables
        : {},
      ...updatedEventList,
    };

    return updatedDeviceEventsObject;
  });

  return deviceEventInfo;
}
