import { ProjectID, DeviceID, FleetID } from "./DomainModel";

export interface IDBuilder {
  buildProjectID(projectUID: string): ProjectID;
  buildDeviceID(deviceUID: string): DeviceID;
  buildFleetID(fleetUID: string): FleetID;
}

class SimpleProjectID implements ProjectID {
  constructor(
    public readonly projectUID: string,
    public readonly type: "ProjectID" = "ProjectID"
  ) {}
}

class SimpleDeviceID implements DeviceID {
  constructor(
    public readonly deviceUID: string,
    public readonly type: "DeviceID" = "DeviceID"
  ) {}
}

class SimpleFleetID implements FleetID {
  constructor(
    public readonly fleetUID: string,
    public readonly type: "FleetID" = "FleetID"
  ) {}
}

export class SimpleIDBuilder implements IDBuilder {
  buildDeviceID(deviceUID: string): DeviceID {
    return { ...new SimpleDeviceID(deviceUID) };
  }

  buildProjectID(projectUID: string): ProjectID {
    return { ...new SimpleProjectID(projectUID) };
  }

  buildFleetID(fleetUID: string): FleetID {
    return { ...new SimpleFleetID(fleetUID) };
  }
}

const builder = new SimpleIDBuilder();

export default builder;
