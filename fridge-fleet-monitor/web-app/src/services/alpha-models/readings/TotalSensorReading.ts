import Reading from "./Reading";
import TotalSensorSchema from "./TotalSensorSchema";

class TotalSensorReading implements Reading<number> {
  schema: TotalSensorSchema;

  value: number;

  captured: string;

  constructor(options: { value: number; captured: string }) {
    this.schema = TotalSensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default TotalSensorReading;
