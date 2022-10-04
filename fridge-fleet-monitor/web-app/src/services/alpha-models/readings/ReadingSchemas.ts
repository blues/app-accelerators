import HumiditySensorSchema from "./HumiditySensorSchema";
import PressureSensorSchema from "./PressureSensorSchema";
import TemperatureSensorSchema from "./TemperatureSensorSchema";
import VoltageSensorSchema from "./VoltageSensorSchema";
import CountSensorSchema from "./CountSensorSchema";
import TotalSensorSchema from "./TotalSensorSchema";
import DoorSwitchSensorSchema from "./DoorSwitchSensorSchema";

const ReadingSchemas = {
  humidity: HumiditySensorSchema,
  pressure: PressureSensorSchema,
  temperature: TemperatureSensorSchema,
  voltage: VoltageSensorSchema,
  count: CountSensorSchema,
  total: TotalSensorSchema,
  doorStatus: DoorSwitchSensorSchema,
};

export default ReadingSchemas;
