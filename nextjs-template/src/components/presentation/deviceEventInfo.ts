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
    const filterEnvVarsByFleet = fleetEnvVars.filter(
      (envVar) => envVar.fleetUID === fleet.uid
    );

    // todo handle if there's no envVars as well to attach to obj?

    const updatedEnvVars = {
      envVars: filterEnvVarsByFleet[0].environment_variables,
    };

    const updatedFleetInfoObj = {
      ...fleet,
      ...updatedEnvVars,
    };

    return updatedFleetInfoObj;
  });

  return fullFleetInfo;
}
