import { normalizeSparrowEvent } from "../../../../src/services/notehub/SparrowEvents";

describe("SparrowEvents", () => {
  it("normalizes sensor provisioning events", () => {
    const body = {
      method: "sensor-provision",
      text: "20323746323650050018000d",
    };
    const result = normalizeSparrowEvent("_health.qo", undefined, body);

    expect(body).toHaveProperty("provisioned", 1);
    expect(result).toEqual({ nodeID: body.text, eventName: "_health.qo" });
  });
});
