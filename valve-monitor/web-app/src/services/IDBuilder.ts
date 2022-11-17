import { ProjectID, DeviceID, EventID } from "./DomainModel";

export interface IDBuilder {
  buildProjectID(projectUID: string): ProjectID;
  buildDeviceID(deviceUID: string): DeviceID;
  buildEventID(eventUID: string): EventID;
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

class SimpleEventID implements EventID {
  constructor(
    public readonly eventUID: string,
    public readonly type: "EventID" = "EventID"
  ) {}
}

export class SimpleIDBuilder implements IDBuilder {
  buildDeviceID(deviceUID: string): DeviceID {
    return { ...new SimpleDeviceID(deviceUID) };
  }

  buildProjectID(projectUID: string): ProjectID {
    return { ...new SimpleProjectID(projectUID) };
  }

  buildEventID(eventUID: string): EventID {
    return { ...new SimpleEventID(eventUID) };
  }
}

const builder = new SimpleIDBuilder();

export default builder;
