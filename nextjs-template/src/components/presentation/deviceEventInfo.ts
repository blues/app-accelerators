// todo wip function to present the data well

export function getCombinedDeviceEventsInfo(devices, deviceEvents) {
  const deviceEventInfo = devices.map((device) => {
    const filterEventsByDevice = deviceEvents
      .filter((event) => event.deviceUID.deviceUID === device.id.deviceUID)
      .sort((a, b) => new Date(b.when) - new Date(a.when));
    const updatedEventList = {
      eventList: filterEventsByDevice,
    };

    const updatedDeviceEventsObject = {
      deviceID: device.id.deviceUID,
      ...updatedEventList,
    };

    return updatedDeviceEventsObject;
  });

  return deviceEventInfo;
}
