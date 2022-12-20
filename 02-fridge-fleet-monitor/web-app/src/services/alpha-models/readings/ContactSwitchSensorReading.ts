import Reading from "./Reading";
import ContactSwitchSensorSchema from "./ContactSwitchSensorSchema";

class ContactSwitchSensorReading implements Reading<string> {
  schema: ContactSwitchSensorSchema;

  value: string;

  captured: string;

  constructor(options: { value: string; captured: string }) {
    this.schema = ContactSwitchSensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default ContactSwitchSensorReading;
