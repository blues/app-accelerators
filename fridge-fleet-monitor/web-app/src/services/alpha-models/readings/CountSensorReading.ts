import Reading from "./Reading";
import CountSensorSchema from "./CountSensorSchema";

class CountSensorReading implements Reading<number> {
  schema: CountSensorSchema;

  value: number;

  captured: string;

  constructor(options: { value: number; captured: string }) {
    this.schema = CountSensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default CountSensorReading;
