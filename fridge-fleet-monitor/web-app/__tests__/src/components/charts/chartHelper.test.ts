import {
  GLOBAL_CHART_OPTIONS,
  getChartOptions,
  getTooltipDisplayText,
} from "../../../../src/components/charts/chartHelper";
import HumiditySensorSchema from "../../../../src/services/alpha-models/readings/HumiditySensorSchema";
import PressureSensorSchema from "../../../../src/services/alpha-models/readings/PressureSensorSchema";
import TemperatureSensorSchema from "../../../../src/services/alpha-models/readings/TemperatureSensorSchema";
import VoltageSensorSchema from "../../../../src/services/alpha-models/readings/VoltageSensorSchema";

describe("Chart options handling", () => {
  it("Returns the chart options when they're called", () => {
    const options = getChartOptions();
    expect(options?.interaction?.mode).toBe(
      GLOBAL_CHART_OPTIONS?.interaction?.mode
    );
  });

  it("Allows users to override the chart defaults", () => {
    const customValue = "y";
    const options = getChartOptions({
      interaction: {
        mode: customValue,
      },
    });
    expect(options?.interaction?.mode).toBe(customValue);
  });
});

describe("Tooltip handling", () => {
  it("Generates correct temperature tooltip text", () => {
    const text = getTooltipDisplayText(
      "Temperature",
      TemperatureSensorSchema,
      22.123456
    );
    expect(text).toBe("Temperature: 22.12°C");
  });

  it("Generates correct humidity tooltip text", () => {
    const text = getTooltipDisplayText(
      "Humidity",
      HumiditySensorSchema,
      12.3456789
    );
    expect(text).toBe("Humidity: 12.35%");
  });

  it("Generates correct voltage tooltip text", () => {
    const text = getTooltipDisplayText("Voltage", VoltageSensorSchema, 3.98765);
    expect(text).toBe("Voltage: 3.99V");
  });

  it("Generates correct pressure tooltip text", () => {
    const text = getTooltipDisplayText(
      "Pressure",
      PressureSensorSchema,
      99.999999
    );
    expect(text).toBe("Pressure: 100.00 kPa");
  });
});
