import HumiditySensorSchema from "./HumiditySensorSchema";
import PressureSensorSchema from "./PressureSensorSchema";
import TemperatureSensorSchema from "./TemperatureSensorSchema";
import VoltageSensorSchema from "./VoltageSensorSchema";
import ContactSwitchSensorSchema from "./ContactSwitchSensorSchema";

const ReadingSchemas = {
  humidity: HumiditySensorSchema,
  pressure: PressureSensorSchema,
  temperature: TemperatureSensorSchema,
  voltage: VoltageSensorSchema,
  contactSwitch: ContactSwitchSensorSchema,
};

export default ReadingSchemas;
