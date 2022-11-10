// todo wip function to combine devices with their data
export function getCombinedDeviceEventsInfo(devices, deviceEvents) {
  const deviceEventInfo = devices.map((device) => {
    // consider also filtering out alarm.qo events as well to keep the data cleaner and more uniform
    const filterEventsByDevice = deviceEvents
      .filter((event) => event.deviceUID.deviceUID === device.id.deviceUID)
      // sort events newest to oldest
      .sort((a, b) => new Date(b.when) - new Date(a.when));
    const updatedEventList = {
      eventList: filterEventsByDevice,
    };
    // reassemble each device with its list of associated events
    const updatedDeviceEventsObject = {
      deviceID: device.id.deviceUID,
      ...updatedEventList,
    };

    return updatedDeviceEventsObject;
  });

  return deviceEventInfo;
}

// todo wip function to combine fleets with their env vars
export function getCombinedFleetInfo(fleetsForProject, fleetEnvVars) {
  const fullFleetInfo = fleetsForProject.fleets.map((fleet) => {
    let updatedFleetInfoObj;
    if (fleetEnvVars.fleetUID == fleet.uid) {
      updatedFleetInfoObj = {
        ...fleet,
        ...fleetEnvVars,
      };
    } else {
      updatedFleetInfoObj = fleet;
    }
    return updatedFleetInfoObj;
  });

  return fullFleetInfo;
}
